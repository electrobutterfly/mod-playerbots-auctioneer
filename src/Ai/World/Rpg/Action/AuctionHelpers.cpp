/*
 * This file is part of the mod-playerbots-auctioneer subsystem for AzerothCore.
 *
 * Copyright (C) 2026 Klaus Simon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
 
#include "AiObjectContext.h"
#include "AuctionHelpers.h"
#include "AuctionHouseMgr.h"
#include "AuctionBidTracker.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace PlayerbotAuction
{

Creature* FindAuctioneerInRange(PlayerbotAI* botAI)
{
    if (!botAI)
        return nullptr;

    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;

    GuidVector const& npcs =
        botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();

    for (ObjectGuid const& guid : npcs)
    {
        Creature* creature = botAI->GetCreature(guid);
        if (!creature)
            continue;
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER))
            continue;
        if (bot->GetDistance(creature) > INTERACTION_DISTANCE)
            continue;
        return creature;
    }

    return nullptr;
}

Item* FindTradeableItemInBags(PlayerbotAI* botAI, uint32 itemId)
{
    if (!botAI || !itemId)
        return nullptr;

    for (Item* item : botAI->GetInventoryItems())
    {
        if (!item)
            continue;
        if (item->GetEntry() != itemId)
            continue;
        if (!item->CanBeTraded())
            continue;
        return item;
    }

    return nullptr;
}

bool SendPlaceBid(Player* bot, Creature* auctioneer, uint32 auctionId, uint32 priceCopper)
{
    if (!bot || !auctioneer || !auctionId || !priceCopper)
        return false;

    WorldPacket packet(CMSG_AUCTION_PLACE_BID);
    packet << auctioneer->GetGUID();
    packet << uint32(auctionId);
    packet << uint32(priceCopper);
    packet.rpos(0);
    bot->GetSession()->HandleAuctionPlaceBid(packet);
    return true;
}

bool SendSellItem(Player* bot, Creature* auctioneer, Item* item,
                  uint32 bidCopper, uint32 buyoutCopper, uint32 durationMinutes)
{
    if (!bot || !auctioneer || !item)
        return false;

    if (durationMinutes != 720 && durationMinutes != 1440 && durationMinutes != 2880)
        return false;

    if (!item->CanBeTraded())
        return false;

    WorldPacket packet(CMSG_AUCTION_SELL_ITEM);
    packet << auctioneer->GetGUID();
    packet << uint32(1);                       // one item stack
    packet << item->GetGUID();                 // item GUID
    packet << uint32(item->GetCount());        // count to list
    packet << uint32(bidCopper);
    packet << uint32(buyoutCopper);
    packet << uint32(durationMinutes);
        packet.rpos(0);
    bot->GetSession()->HandleAuctionSellItem(packet);
    return true;
}

uint32 CountActiveListings(Player* bot)
{
    if (!bot)
        return 0;

    ObjectGuid const botGuid = bot->GetGUID();
    uint32 count = 0;

    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
    {
        for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
        {
            AuctionEntry* entry = it->second;
            if (entry && entry->owner == botGuid)
                ++count;
        }
    }
    return count;
}

uint32 RemoveAllBotAuctions(Player* bot)
{
    if (!bot)
        return 0;

    ObjectGuid const botGuid = bot->GetGUID();
    AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction());
    if (!ahObj)
        return 0;

    // Drop every persisted bid for this bot too. The bot is about to be
    // wiped and re-randomized, so a stale bid list from a previous life is
    // not something the re-evaluation pass should be trying to defend.
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        PlayerbotAuction::RemoveAllTrackedBids(botAI);

    // Drop every persisted bid for this bot too. The bot is about to be
    // wiped and re-randomized, so a stale bid list from a previous life is
    // not something the re-evaluation pass should be trying to defend.
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        PlayerbotAuction::RemoveAllTrackedBids(botAI);

    std::vector<AuctionEntry*> toRemove;
    for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
    {
        AuctionEntry* entry = it->second;
        if (entry && entry->owner == botGuid)
            toRemove.push_back(entry);
    }

    if (toRemove.empty())
        return 0;

    auto trans = CharacterDatabase.BeginTransaction();
    for (AuctionEntry* entry : toRemove)
    {
        if (!entry->bidder.IsEmpty())
            sAuctionMgr->SendAuctionCancelledToBidderMail(entry, trans);
        entry->DeleteFromDB(trans);
        sAuctionMgr->RemoveAItem(entry->item_guid, true, &trans);
        ahObj->RemoveAuction(entry);
    }
    CharacterDatabase.CommitTransaction(trans);
    return static_cast<uint32>(toRemove.size());
}

uint32 CleanupBotMail(Player* bot)
{
    if (!bot)
        return 0;

    ObjectGuid::LowType const botLow = bot->GetGUID().GetCounter();

    std::vector<uint32> ids;
    for (PlayerMails::const_iterator it = bot->GetMails().begin(); it != bot->GetMails().end(); ++it)
    {
        Mail* m = *it;
        if (m && m->state != MAIL_STATE_DELETED)
            ids.push_back(m->messageID);
    }

    for (uint32 id : ids)
        bot->RemoveMail(id);

    CharacterDatabase.Execute("DELETE FROM mail WHERE receiver = {}", botLow);
    CharacterDatabase.Execute("DELETE FROM mail_items WHERE receiver = {}", botLow);

    return static_cast<uint32>(ids.size());
}

} // namespace PlayerbotAuction
