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
 
#include "AuctionBidTracker.h"
#include "AiObjectContext.h"
#include "DatabaseEnv.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotsDatabase.h"
#include "PlayerbotAIConfig.h"

namespace PlayerbotAuction
{

bool IsUsageWanted(ItemUsage usage)
{
    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_QUEST:
        case ITEM_USAGE_SKILL:
        case ITEM_USAGE_USE:
        case ITEM_USAGE_AMMO:
        case ITEM_USAGE_DISENCHANT:
            return true;
        default:
            return false;
    }
}

void LoadTrackedBids(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    if (botAI->trackedBidsLoaded)
        return;
    botAI->trackedBidsLoaded = true;

    botAI->trackedAuctions.clear();

    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT auction_id, item_template_id, item_count, max_willing, "
        "       UNIX_TIMESTAMP(tracked_since) "
        "FROM auctionhouse_bot_bids WHERE bot_guid = {}",
        bot->GetGUID().GetCounter());

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        PlayerbotAI::TrackedAuction t;
        t.auctionId      = fields[0].Get<uint32>();
        t.itemTemplateId = fields[1].Get<uint32>();
        t.itemCount      = fields[2].Get<uint32>();
        t.maxWilling     = fields[3].Get<uint32>();
        t.trackedSince   = time_t(fields[4].Get<uint64>());

        botAI->trackedAuctions.push_back(t);
    } while (result->NextRow());

    LOG_DEBUG("playerbots",
              "[AH-TRACE] {} loaded {} persisted tracked bids",
              bot->GetName(), botAI->trackedAuctions.size());
}

void UpsertTrackedBid(PlayerbotAI* botAI, PlayerbotAI::TrackedAuction const& bid)
{
    if (!botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    PlayerbotsDatabase.Execute(
        "INSERT INTO auctionhouse_bot_bids "
        "  (bot_guid, auction_id, item_template_id, item_count, max_willing) "
        "VALUES ({}, {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "  item_template_id = VALUES(item_template_id), "
        "  item_count       = VALUES(item_count), "
        "  max_willing      = VALUES(max_willing)",
        bot->GetGUID().GetCounter(),
        bid.auctionId,
        bid.itemTemplateId,
        bid.itemCount,
        bid.maxWilling);
}

void RemoveTrackedBid(PlayerbotAI* botAI, uint32 auctionId)
{
    if (!botAI || !auctionId)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    PlayerbotsDatabase.Execute(
        "DELETE FROM auctionhouse_bot_bids "
        "WHERE bot_guid = {} AND auction_id = {}",
        bot->GetGUID().GetCounter(), auctionId);
}

void RemoveAllTrackedBids(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    PlayerbotsDatabase.Execute(
        "DELETE FROM auctionhouse_bot_bids WHERE bot_guid = {}",
        bot->GetGUID().GetCounter());

    botAI->trackedAuctions.clear();
}

}  // namespace PlayerbotAuction
