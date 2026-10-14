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

#include "BuyContainerAction.h"
#include "AiObjectContext.h"
#include "AuctionHelpers.h"
#include "AuctionHouseMgr.h"
#include "BudgetValues.h"
#include "Creature.h"
#include "Event.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include <cmath>

namespace
{
    // Size of the smallest equipped bag. Returns 0 when any bag slot is
    // empty, meaning "any size bag fits" and the caller should treat any
    // bag-like container as an upgrade.
    uint32 SmallestEquippedBagSize(Player* bot)
    {
        uint32 smallest = 0;
        bool anyEmpty = false;

        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                uint32 const size = bag->GetBagSize();
                if (smallest == 0 || size < smallest)
                    smallest = size;
            }
            else
            {
                anyEmpty = true;
            }
        }

        return anyEmpty ? 0 : smallest;
    }

    bool IsBagLikeContainer(ItemTemplate const* proto)
    {
        if (!proto || proto->Class != ITEM_CLASS_CONTAINER)
            return false;

        return proto->SubClass == ITEM_SUBCLASS_CONTAINER ||
               proto->SubClass == ITEM_SUBCLASS_QUIVER ||
               proto->SubClass == ITEM_SUBCLASS_AMMO_POUCH;
    }
}

bool BuyContainerAction::Execute(Event event)
{
    std::string const source = event.getParam();

    if (source == "vendor")
    {
        GuidVector const& vendors =
            botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();

        for (ObjectGuid const& guid : vendors)
        {
            Creature* vendor = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_VENDOR);
            if (!vendor)
                continue;

            if (BuyContainerFromVendor(vendor))
                return true;
        }
        return false;
    }

    if (source == "ah")
    {
        Creature* auctioneer = PlayerbotAuction::FindAuctioneerInRange(botAI);
        if (!auctioneer)
            return false;

        return BuyContainerFromAuctioneer(auctioneer);
    }

    return false;
}

bool BuyContainerAction::BuyContainerFromVendor(Creature* vendor)
{
    if (!vendor)
        return false;

    VendorItemData const* tItems = vendor->GetVendorItems();
    if (!tItems || tItems->GetItemCount() == 0)
        return false;

    uint32 const smallest = SmallestEquippedBagSize(bot);

    uint32 bestItemId = 0;
    uint32 bestSlots = 0;
    uint32 bestSlotIndex = 0;
    uint32 bestPrice = 0;

    for (uint32 slot = 0; slot < tItems->GetItemCount(); ++slot)
    {
        VendorItem const* vi = tItems->GetItem(slot);
        if (!vi || !vi->item)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(vi->item);
        if (!IsBagLikeContainer(proto))
            continue;

        uint32 const size = proto->ContainerSlots;
        if (size == 0)
            continue;

        if (smallest != 0 && size <= smallest)
            continue;

        if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        uint32 const price =
            uint32(std::floor(proto->BuyPrice * bot->GetReputationPriceDiscount(vendor)));

        if (size > bestSlots || (size == bestSlots && (bestPrice == 0 || price < bestPrice)))
        {
            bestItemId = proto->ItemId;
            bestSlots = size;
            bestSlotIndex = slot;
            bestPrice = price;
        }
    }

    if (bestItemId == 0)
        return false;

    if (!botAI->HasCheat(BotCheatMask::gold))
    {
        uint32 const freeMoney =
            AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::gear);
        if (freeMoney < bestPrice)
            return false;
    }

    uint32 const oldCount = bot->GetItemCount(bestItemId, false);

    bot->BuyItemFromVendorSlot(vendor->GetGUID(), bestSlotIndex, bestItemId, 1, NULL_BAG, NULL_SLOT);

    uint32 const newCount = bot->GetItemCount(bestItemId, false);
    if (newCount > oldCount)
    {
        LOG_DEBUG("playerbots",
                  "[BAG] {} bought [{}] ({} slots) from vendor {} for {} copper",
                  bot->GetName(), bestItemId, bestSlots, vendor->GetName(), bestPrice);
        return true;
    }

    return false;
}

bool BuyContainerAction::BuyContainerFromAuctioneer(Creature* auctioneer)
{
    if (!auctioneer)
        return false;

    AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
    if (!ahObj)
        return false;

    uint32 const smallest = SmallestEquippedBagSize(bot);

    // Only buyout auctions are considered. Bag upgrades are small, frequent
    // purchases and a bot that bids will wait days for the auction to expire
    // before it can equip the bag -- by which point it has likely found
    // another one. Buyouts are instant and are the correct behaviour here.
    AuctionEntry* bestAuction = nullptr;
    uint32 bestSlots = 0;
    uint32 bestPrice = 0;

    for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
    {
        AuctionEntry* entry = it->second;
        if (!entry)
            continue;

        if (entry->owner == bot->GetGUID())
            continue;

        if (entry->buyout == 0)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry->item_template);
        if (!IsBagLikeContainer(proto))
            continue;

        uint32 const size = proto->ContainerSlots;
        if (size == 0)
            continue;

        if (smallest != 0 && size <= smallest)
            continue;

        if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        uint32 const price = entry->buyout;

        if (!botAI->HasCheat(BotCheatMask::gold) && price > bot->GetMoney())
            continue;

        if (size > bestSlots || (size == bestSlots && (bestPrice == 0 || price < bestPrice)))
        {
            bestAuction = entry;
            bestSlots = size;
            bestPrice = price;
        }
    }

    if (!bestAuction)
        return false;

    PlayerbotAuction::SendPlaceBid(bot, auctioneer, bestAuction->Id, bestAuction->buyout);

    LOG_DEBUG("playerbots",
              "[BAG] {} buying out [{}] ({} slots) from AH for {} copper (auctionId={})",
              bot->GetName(), bestAuction->item_template, bestSlots,
              bestAuction->buyout, bestAuction->Id);

    return true;
}
