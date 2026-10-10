/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AuctionSellAdvisor.h"
#include "AuctionHouseMgr.h"
#include "ItemUsageValue.h"
#include "Bag.h"
#include "Item.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include <algorithm>
#include <vector>
#include <unordered_map>

namespace PlayerbotAuction
{

bool BotHasLargeBags(Player* bot)
{
    if (!bot)
        return false;

    uint32 largest = 0;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            if (bag->GetBagSize() > largest)
                largest = bag->GetBagSize();
        }
    }
    return largest >= sPlayerbotAIConfig.auctionLargeBagThreshold;
}

namespace
{
    // Usage values that mean "the bot wants this for a purpose". Anything in
    // this set should not be listed on the AH.
    bool IsUsageProtected(ItemUsage usage)
    {
        switch (usage)
        {
            case ITEM_USAGE_EQUIP:
            case ITEM_USAGE_REPLACE:
            case ITEM_USAGE_BROKEN_EQUIP:
            case ITEM_USAGE_QUEST:
            case ITEM_USAGE_SKILL:
            case ITEM_USAGE_USE:
            case ITEM_USAGE_AMMO:
            case ITEM_USAGE_KEEP:
            case ITEM_USAGE_GUILD_TASK:
                return true;
            default:
                return false;
        }
    }
}

bool IsProtectedClassReagent(Player* bot, Item* item)
{
    if (!bot || !item)
        return false;

    uint32 const entry = item->GetEntry();
    switch (bot->getClass())
    {
        case CLASS_MAGE:
            return entry == 17031 || entry == 17032;
        case CLASS_WARLOCK:
            return entry == 6265   // Soul Shard
                || entry == 5512   // Healthstone
                || entry == 5522   // Spellstone
                || entry == 13699; // Firestone
        case CLASS_SHAMAN:
            return entry == 17030; // Ankh
        case CLASS_PALADIN:
            return entry == 21177; // Symbol of Kings
        case CLASS_ROGUE:
            return entry == 5140     // Flash Powder
                || entry == 5530     // Blinding Powder
                || (item->GetTemplate()->Class == ITEM_CLASS_CONSUMABLE &&
                    item->GetTemplate()->SubClass == ITEM_SUBCLASS_POTION &&
                    item->GetTemplate()->Name1.find("Poison") != std::string::npos);
        case CLASS_HUNTER:
            return item->GetTemplate()->Class == ITEM_CLASS_PROJECTILE;
        case CLASS_DRUID:
            return entry == 17034; // Wild Thornroot
        default:
            return false;
    }
}

void ComputeSellPrice(PlayerbotAI* botAI, Item* item, uint32& outBid, uint32& outBuyout, bool isRelist)
{
    outBid = 0;
    outBuyout = 0;
    if (!botAI || !item)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;

    uint32 basePrice = 0;

    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
    {
        uint32 lowest = 0;
        for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
        {
            AuctionEntry* entry = it->second;
            if (!entry)
                continue;
            if (entry->item_template != proto->ItemId)
                continue;
            uint32 price = entry->buyout ? entry->buyout : entry->startbid;
            if (price == 0)
                continue;
            if (lowest == 0 || price < lowest)
                lowest = price;
        }
        basePrice = lowest;
    }

    if (basePrice == 0)
    {
        uint32 vendor = proto->SellPrice;
        if (vendor < 1)
            vendor = 1;
        basePrice = vendor * 10;
    }

    // Add Auctioneer
    // Relist undercuts the current market more aggressively than a fresh listing.
    uint32 minPct = isRelist ? sPlayerbotAIConfig.auctionRelistUndercutPercentMin
                             : sPlayerbotAIConfig.auctionSellUndercutPercentMin;
    uint32 maxPct = isRelist ? sPlayerbotAIConfig.auctionRelistUndercutPercentMax
                             : sPlayerbotAIConfig.auctionSellUndercutPercentMax;
    if (maxPct < minPct)
        maxPct = minPct;
    uint32 undercutPct = urand(minPct, maxPct);

    uint32 sellPrice = basePrice - (basePrice * undercutPct / 100);
    if (sellPrice < 1)
        sellPrice = 1;

    uint32 bid = sellPrice / 2;
    if (bid < 1)
        bid = 1;

    outBid = bid;
    outBuyout = sellPrice;
}

SellCandidate FindItemToSell(PlayerbotAI* botAI)
{
    SellCandidate result;
    if (!botAI)
        return result;

    Player* bot = botAI->GetBot();
    if (!bot)
        return result;

    if (!sPlayerbotAIConfig.auctionEnableSelling)
        return result;

    uint32 const minQuality = sPlayerbotAIConfig.auctionMinSellQuality;
    uint32 const minValue = sPlayerbotAIConfig.auctionMinSellValueCopper;
    bool const largeBags = BotHasLargeBags(bot);
    uint32 const keepHealth =
        largeBags ? sPlayerbotAIConfig.auctionKeepHealthPotionsLargeBags
                  : sPlayerbotAIConfig.auctionKeepHealthPotions;
    uint32 const keepMana =
        largeBags ? sPlayerbotAIConfig.auctionKeepManaPotionsLargeBags
                  : sPlayerbotAIConfig.auctionKeepManaPotions;

    std::unordered_map<uint32, uint32> potionCounts;
    for (Item* it : botAI->GetInventoryItems())
    {
        if (!it)
            continue;
        ItemTemplate const* p = it->GetTemplate();
        if (!p || p->Class != ITEM_CLASS_CONSUMABLE || p->SubClass != ITEM_SUBCLASS_POTION)
            continue;
        std::string const& name = p->Name1;
        uint32 bucket = 0;
        if (name.find("Healing") != std::string::npos || name.find("Health") != std::string::npos)
            bucket = 1;
        else if (name.find("Mana") != std::string::npos)
            bucket = 2;
        else
            continue;
        potionCounts[bucket] += it->GetCount();
    }

        std::unordered_map<uint32, uint32> potionAlreadyPicked;

        struct Candidate
    {
        Item* item;
        uint32 value;
    };
    std::vector<Candidate> candidates;

    // Add Auctioneer
    // Per-reason rejection counters. Only accumulate and emit when both
    // debugAuction and auctionRunAnalysis are on.
    bool const runAnalysis =
        sPlayerbotAIConfig.debugAuction && sPlayerbotAIConfig.auctionRunAnalysis;
    uint32 scanned = 0, rejTrade = 0, rejQuality = 0, rejEquipped = 0, rejValue = 0;
    uint32 rejQuest = 0, rejBound = 0, rejFullStack = 0, rejClassReagent = 0;
    uint32 rejUseful = 0, rejPotion = 0;

    for (Item* it : botAI->GetInventoryItems())
    {
        if (runAnalysis)
            ++scanned;
        if (!it)
            continue;
        ItemTemplate const* p = it->GetTemplate();
        if (!p)
            continue;

        if (!it->CanBeTraded()) { if (runAnalysis) ++rejTrade; continue; }
        if (p->Quality < minQuality) { if (runAnalysis) ++rejQuality; continue; }
        if (it->IsEquipped()) { if (runAnalysis) ++rejEquipped; continue; }
        if (p->SellPrice < minValue) { if (runAnalysis) ++rejValue; continue; }

        if (sPlayerbotAIConfig.auctionProtectQuestItems && p->Class == ITEM_CLASS_QUEST)
        {
            if (runAnalysis) ++rejQuest;
            continue;
        }

        if (sPlayerbotAIConfig.auctionProtectBoundItems && it->IsSoulBound())
        {
            if (runAnalysis) ++rejBound;
            continue;
        }

        if (sPlayerbotAIConfig.auctionSellFullStacksOnly)
        {
            uint32 const maxStack = p->GetMaxStackSize();
            if (maxStack > 1 && it->GetCount() < maxStack)
            {
                if (runAnalysis) ++rejFullStack;
                continue;
            }
        }

        if (sPlayerbotAIConfig.auctionProtectClassReagents && IsProtectedClassReagent(bot, it))
        {
            if (runAnalysis) ++rejClassReagent;
            continue;
        }

        if (sPlayerbotAIConfig.auctionProtectUsefulItems)
        {
            AiObjectContext* ctx = botAI->GetAiObjectContext();
            ItemUsage usage = ctx->GetValue<ItemUsage>("item usage", p->ItemId)->Get();
            if (IsUsageProtected(usage))
            {
                if (runAnalysis) ++rejUseful;
                continue;
            }
        }

        if (p->Class == ITEM_CLASS_CONSUMABLE && p->SubClass == ITEM_SUBCLASS_POTION)
        {
            std::string const& name = p->Name1;
            uint32 bucket = 0;
            uint32 cap = 0;
            if (name.find("Healing") != std::string::npos || name.find("Health") != std::string::npos)
            {
                bucket = 1;
                cap = keepHealth;
            }
            else if (name.find("Mana") != std::string::npos)
            {
                bucket = 2;
                cap = keepMana;
            }
            else
            {
                bucket = 100 + p->ItemId;
                cap = 20;
            }

            uint32 const held = potionCounts[bucket];
            uint32 const picked = potionAlreadyPicked[bucket];
            if (held <= cap || picked >= (held - cap))
            {
                if (runAnalysis) ++rejPotion;
                continue;
            }

            potionAlreadyPicked[bucket] += it->GetCount();
        }

                uint32 value = p->SellPrice * it->GetCount();
        candidates.push_back({it, value});
    }

    // Add Auctioneer
    if (runAnalysis)
    {
        LOG_ERROR("playerbots",
                  "[AH-TRACE] {} sellpick: scanned={} candidates={} "
                  "rejTrade={} rejQuality={} rejEquipped={} rejValue={} "
                  "rejQuest={} rejBound={} rejFullStack={} rejClassReagent={} "
                  "rejUseful={} rejPotion={}",
                  bot->GetName(), scanned, candidates.size(),
                  rejTrade, rejQuality, rejEquipped, rejValue,
                  rejQuest, rejBound, rejFullStack, rejClassReagent,
                  rejUseful, rejPotion);
    }

    if (candidates.empty())
        return result;

    // Sort by value descending, take the top N, pick one at random.
    // This diversifies what bots list so the AH doesn't fill with the
    // same item from every bot.
    std::sort(candidates.begin(), candidates.end(),
              [](Candidate const& a, Candidate const& b)
              { return a.value > b.value; });

    constexpr size_t kTopN = 5;
    size_t const poolSize = std::min(candidates.size(), kTopN);
    size_t const pick = urand(0, poolSize - 1);
    Item* best = candidates[pick].item;

    result.item = best;
    ComputeSellPrice(botAI, best, result.suggestedBid, result.suggestedBuyout);
    return result;
}

} // namespace PlayerbotAuction
