#include "AuctionHelpers.h"
#include "AiObjectContext.h"
#include "Creature.h"
#include "Item.h"
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

} // namespace PlayerbotAuction
