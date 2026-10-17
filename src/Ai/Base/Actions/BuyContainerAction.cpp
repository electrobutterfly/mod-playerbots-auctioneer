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
#include "AuctionMailAdvisor.h"
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
    bool IsBagLikeContainer(ItemTemplate const* proto)
    {
        if (!proto || proto->Class != ITEM_CLASS_CONTAINER)
            return false;

        return proto->SubClass == ITEM_SUBCLASS_CONTAINER ||
               proto->SubClass == ITEM_SUBCLASS_QUIVER ||
               proto->SubClass == ITEM_SUBCLASS_AMMO_POUCH;
    }

    // Can this bot actually use the bag? A profession-restricted bag
    // (herb pouch, mining bag, enchanting bag, etc.) only stores that
    // profession's materials; a bot without the matching profession
    // gets zero benefit and wastes a bag slot that could hold any item
    // instead. Restricted bags are also strictly worse than a general
    // bag of the same size, so a bot should never buy one.
    bool IsBagUsableByBot(Player* bot, PlayerbotAI* botAI, ItemTemplate const* proto)
    {
        if (!proto || proto->Class != ITEM_CLASS_CONTAINER)
            return false;

        uint32 const family = proto->BagFamily;
        if (family == 0)
            return true;

        if (family & BAG_FAMILY_MASK_HERBS)
            return botAI->HasSkill(SKILL_HERBALISM);
        if (family & BAG_FAMILY_MASK_MINING_SUPP)
            return botAI->HasSkill(SKILL_MINING);
        if (family & BAG_FAMILY_MASK_LEATHERWORKING_SUPP)
            return botAI->HasSkill(SKILL_LEATHERWORKING);
        if (family & BAG_FAMILY_MASK_ENCHANTING_SUPP)
            return botAI->HasSkill(SKILL_ENCHANTING);
        if (family & BAG_FAMILY_MASK_ENGINEERING_SUPP)
            return botAI->HasSkill(SKILL_ENGINEERING);
        if (family & BAG_FAMILY_MASK_GEMS)
            return botAI->HasSkill(SKILL_JEWELCRAFTING);
        if (family & BAG_FAMILY_MASK_INSCRIPTION_SUPP)
            return botAI->HasSkill(SKILL_INSCRIPTION);
        if (family & BAG_FAMILY_MASK_SOUL_SHARDS)
            return bot->getClass() == CLASS_WARLOCK;
        if (family & (BAG_FAMILY_MASK_ARROWS | BAG_FAMILY_MASK_BULLETS))
            return bot->getClass() == CLASS_HUNTER;

        return false;
    }

    // Does the bot already have a bag of this family equipped or in
    // inventory? A single herb bag is sufficient even for a herbalist;
    // multiple copies of the same restricted family are pointless.
    bool HasBagOfFamily(Player* bot, PlayerbotAI* botAI, uint32 family)
    {
        if (family == 0)
            return false;

        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Item* bag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (bag && bag->GetTemplate() && bag->GetTemplate()->BagFamily == family)
                return true;
        }

        for (Item* item : botAI->GetInventoryItems())
        {
            if (item && item->GetTemplate() && item->GetTemplate()->BagFamily == family)
                return true;
        }

        return false;
    }
}

bool BuyContainerAction::Execute(Event event)
{
    // A bag won at the AH arrives by mail, not by inventory. If one is
    // pending collection, buying another bag from a vendor or the AH
    // while the previous one is still in transit wastes copper on a
    // duplicate that may not even be an upgrade once the mail bag is
    // equipped. Wait for the mail bag to be collected and equipped,
    // then re-evaluate on the next visit.
    if (PlayerbotAuction::CountUncollectedWonBags(bot) > 0)
        return false;

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

    // Multi-buy loop. Buys the biggest affordable bag, updates the local
    // slot-size tracking, and repeats until either no bigger bag is
    // stocked, the gold budget is exhausted, or the number of bags needed
    // to fill the bot's empty slots has been reached.
    //
    // Crucial correction over the previous version: the loop must not buy
    // more bags than the bot will actually be able to equip. Equips are
    // triggered asynchronously by the SMSG_ITEM_PUSH_RESULT packet and
    // may not finish within the same visit. Bags bought but not yet
    // equipped sit in inventory, and the next visit would see the same
    // empty slots and buy them again, accumulating wasted gold. The
    // budget is therefore capped at (emptySlots - bagsAlreadyInInventory)
    // so that no purchase happens while the previous buys are still
    // awaiting equip.
    std::vector<uint32> slotSizes;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Item* equippedBag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (equippedBag && equippedBag->GetTemplate())
            slotSizes.push_back(equippedBag->GetTemplate()->ContainerSlots);
        else
            slotSizes.push_back(0);
    }
    std::sort(slotSizes.begin(), slotSizes.end());

    uint32 bagsInInventory = 0;
    for (Item* item : botAI->GetInventoryItems())
    {
        if (!item)
            continue;
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && IsBagLikeContainer(proto))
            ++bagsInInventory;
    }

    // Bigger is always better. Attempt up to four buys regardless of what
    // the current slot state is; the `size > smallest` check inside the
    // loop filters out anything that would be a downgrade or an equal
    // replacement. The buyBudget cap still applies, so a bot whose previous
    // purchases are still pending equip in inventory will not overbuy.
    uint32 const buyBudget = 4 > bagsInInventory ? 4 - bagsInInventory : 0;
    if (buyBudget == 0)
        return false;

    uint32 bought = 0;

    for (uint32 iter = 0; iter < buyBudget; ++iter)
    {
        uint32 const smallest = slotSizes[0];

        // Affordability ceiling, computed once per iteration. Without
        // this, size-only selection picks the biggest bag on offer and
        // the post-selection check breaks the whole buy loop when that
        // bag is unaffordable - a bot at Haris Pilton with 150 000c would
        // pick Portable Hole (24 slots, 300 000c), fail the check, and
        // buy NOTHING instead of the Gigantique Bag (22 slots,
        // 120 000c) that is also on offer. Filtering here lets the
        // selection fall back to the next-biggest affordable bag.
        uint32 const freeMoney = botAI->HasCheat(BotCheatMask::gold)
            ? ~0u
            : AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::gear);

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
            if (!IsBagUsableByBot(bot, botAI, proto))
                continue;
            if (HasBagOfFamily(bot, botAI, proto->BagFamily))
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

            if (price > freeMoney)
                continue;

            if (size > bestSlots || (size == bestSlots && (bestPrice == 0 || price < bestPrice)))
            {
                bestItemId = proto->ItemId;
                bestSlots = size;
                bestSlotIndex = slot;
                bestPrice = price;
            }
        }

        if (bestItemId == 0)
            break;

        uint32 const oldCount = bot->GetItemCount(bestItemId, false);

        bot->BuyItemFromVendorSlot(vendor->GetGUID(), bestSlotIndex, bestItemId, 1, NULL_BAG, NULL_SLOT);

        uint32 const newCount = bot->GetItemCount(bestItemId, false);
        if (newCount > oldCount)
        {
            LOG_DEBUG("playerbots",
                      "[BAG] {} bought [{}] ({} slots) from vendor {} for {} copper",
                      bot->GetName(), bestItemId, bestSlots, vendor->GetName(), bestPrice);

            // Equip the purchased bag now, synchronously, before returning to
            // the caller. Relying on the SMSG_ITEM_PUSH_RESULT handler to do
            // it on a later tick is unreliable: any Refresh()/Randomize()
            // that fires between the buy and the bot's next UpdateAIInternal
            // runs PlayerbotFactory::ClearInventory(), which iterates
            // ITERATE_ITEMS_IN_BAGS and destroys every item not yet in an
            // equipment slot. A bag still in the backpack becomes a wipe
            // casualty; a bag in an equipment slot survives. This mirrors
            // BuyAction::Execute, which already does the same explicit call
            // for gear purchases for the same reason.
            botAI->DoSpecificAction("equip upgrades packet action");

            // Verify the equip actually happened. GetItemCount counts
            // equipped bags too (its first loop walks EQUIPMENT_SLOT_START ..
            // INVENTORY_SLOT_ITEM_END, which includes slots 19-22), so look
            // at the equipment slots directly: the bag is only present in
            // one of them after a successful SwapItem in
            // EquipAction::EquipItem.
            bool equipped = false;
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            {
                Item* equippedBag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
                if (equippedBag && equippedBag->GetEntry() == bestItemId)
                {
                    equipped = true;
                    break;
                }
            }

            if (!equipped)
            {
                // Equip did not happen. Stop buying: any further bag we buy
                // would also sit in the backpack and the next Refresh()
                // would wipe every one of them. The bot keeps the gold and
                // retries on a future visit.
                break;
            }

            // Simulate the equip in the local slot tracking so the next
            // iteration of this loop looks for a strictly bigger bag.
            slotSizes[0] = bestSlots;
            std::sort(slotSizes.begin(), slotSizes.end());
            ++bought;
        }
        else
        {
            // Purchase failed for some reason; do not loop forever.
            break;
        }
    }

    return bought > 0;
}

bool BuyContainerAction::BuyContainerFromAuctioneer(Creature* auctioneer)
{
    if (!auctioneer)
        return false;

    AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
    if (!ahObj)
        return false;

    // Multi-buy loop. AH bag purchases arrive by mail, so the bot's
    // equipped bags do not update during this visit. Track virtual slot
    // sizes locally so each iteration looks for a strictly bigger bag
    // than the last. One buyout removes the auction entry synchronously
    // (SendPlaceBid triggers the core buyout handler), so a repeat cannot
    // double-buy the same listing.
    //
    // Same over-buy guard as the vendor path: AH bags go to mail, not to
    // the bot's equipped slots, so any bag listed for this bot is one that
    // has not been equipped yet. Do not place a second buyout until the
    // previous one has been collected and equipped. The budget is therefore
    // capped at (emptySlots - bagsAlreadyInInventory) exactly as in the
    // vendor path, so consecutive visits cannot over-buy.
    std::vector<uint32> slotSizes;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Item* equippedBag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (equippedBag && equippedBag->GetTemplate())
            slotSizes.push_back(equippedBag->GetTemplate()->ContainerSlots);
        else
            slotSizes.push_back(0);
    }
    std::sort(slotSizes.begin(), slotSizes.end());

    uint32 bagsInInventory = 0;
    for (Item* item : botAI->GetInventoryItems())
    {
        if (!item)
            continue;
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && IsBagLikeContainer(proto))
            ++bagsInInventory;
    }

    // Bigger is always better. Attempt up to four buys regardless of what
    // the current slot state is; the `size > smallest` check inside the
    // loop filters out anything that would be a downgrade or an equal
    // replacement. The buyBudget cap still applies, so a bot whose previous
    // purchases are still pending equip in inventory will not overbuy.
    uint32 const buyBudget = 4 > bagsInInventory ? 4 - bagsInInventory : 0;
    if (buyBudget == 0)
        return false;

    uint32 bought = 0;

    for (uint32 iter = 0; iter < buyBudget; ++iter)
    {
        uint32 const smallest = slotSizes[0];

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
            if (!IsBagUsableByBot(bot, botAI, proto))
                continue;
            if (HasBagOfFamily(bot, botAI, proto->BagFamily))
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
            break;

        PlayerbotAuction::SendPlaceBid(bot, auctioneer, bestAuction->Id, bestAuction->buyout);

        LOG_DEBUG("playerbots",
                  "[BAG] {} buying out [{}] ({} slots) from AH for {} copper (auctionId={})",
                  bot->GetName(), bestAuction->item_template, bestSlots,
                  bestAuction->buyout, bestAuction->Id);

        slotSizes[0] = bestSlots;
        std::sort(slotSizes.begin(), slotSizes.end());
        ++bought;
    }

    return bought > 0;
}
