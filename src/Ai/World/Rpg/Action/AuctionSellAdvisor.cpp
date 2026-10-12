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

#include "AuctionSellAdvisor.h"
#include "AuctionBaseline.h"
#include "AuctionHouseMgr.h"
#include "ItemUsageValue.h"
#include "Bag.h"
#include "Item.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include <algorithm>
#include <cmath>
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

    // Draw a per-unit price from a triangular distribution over [lo, hi]
    // with the peak at mid. The baseline carries only the trimmed low /
    // median / high, so a triangular draw is the best approximation of
    // the split-normal the reference data describes. Spreading each bot's
    // listing slightly around the reference price is what stops every bot
    // from copying the same number and producing a flat AH.
    uint32 DrawBaselinePrice(uint32 lo, uint32 mid, uint32 hi)
    {
        if (lo > mid)
            lo = mid;
        if (hi < mid)
            hi = mid;
        if (lo == hi)
            return mid ? mid : 1;
        if (mid == 0)
            return lo ? lo : 1;

        double const span  = double(hi - lo);
        double const c     = double(mid - lo) / span;
        double const u     = frand(0.0, 1.0);
        double       value;
        if (u < c)
            value = double(lo) + std::sqrt(u * span * double(mid - lo));
        else
            value = double(hi) - std::sqrt((1.0 - u) * span * double(hi - mid));

        if (value < 1.0)
            value = 1.0;
        return uint32(value + 0.5);
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

    uint32 const count = item->GetCount();
    if (count == 0)
        return;

    // Vendor floor for the whole stack. Listing a stack below what a vendor
    // would pay is a strictly losing trade. Always applied last.
    uint32 vendorPerUnit = proto->SellPrice;
    if (vendorPerUnit < 1)
        vendorPerUnit = 1;
    uint32 const floorPrice = vendorPerUnit * count;

    // Market reference: the cheapest per-unit price any current listing asks
    // for this item. 0 if nothing matching is up.
    uint32 lowestPerUnit = 0;
    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
    {
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
            uint32 const ec = entry->itemCount > 0 ? entry->itemCount : 1;
            uint32 perUnit = price / ec;
            if (perUnit < 1)
                perUnit = 1;
            if (lowestPerUnit == 0 || perUnit < lowestPerUnit)
                lowestPerUnit = perUnit;
        }
    }

    // Baseline lookup. Only consulted when the admin selected Mode 2.
    // A row is trusted only when there is actual listing data behind it;
    // category-placeholder rows and one-sample rows are still usable but
    // a row with fewer than three samples or zero bounds is discarded so
    // the legacy path handles that item instead.
    PlayerbotAuction::Baseline const* baseline = nullptr;
    if (sPlayerbotAIConfig.auctionBaselineMode == 2)
    {
        uint8 const faction = (bot->GetTeamId() == TEAM_ALLIANCE) ? 2 : 6;
        int32 const suffix  = item->GetItemRandomPropertyId();
        baseline = PlayerbotAuction::AuctionBaselineMgr::instance()
                       .LookupWithFallback(faction, proto->ItemId, suffix);

        if (baseline &&
            (baseline->priceSamples < 3 ||
             baseline->priceLow   == 0 ||
             baseline->priceHigh  == 0))
        {
            baseline = nullptr;
        }
    }

    uint32 basePrice = 0;
    if (lowestPerUnit > 0)
    {
        // Live market reference: undercut the cheapest per-unit listing.
        // Correct comparison across stack sizes, since the cheapest listing
        // may be a stack of 1 while ours is a stack of 20.
        basePrice = lowestPerUnit * count;
    }
        else if (baseline)
    {
        // No live listing. Draw a per-unit price from the reference band.
        // This is the branch that stops fresh bots from defaulting to
        // vendor * 5 and flooding the market.
        uint32 perUnit = DrawBaselinePrice(baseline->priceLow,
                                           baseline->priceMedian,
                                           baseline->priceHigh);

        // Multi-snapshot trend. A rising market means today's listings are
        // cheap relative to what buyers will pay tomorrow, so nudge the
        // draw up. A falling market means dump inventory now, so nudge
        // down. Clamped to the same [low, high] band either way, so a
        // strong trend cannot push a listing outside the reference range.
        if (baseline->trendBp != 0)
        {
            int64 const shift = int64(perUnit) * baseline->trendBp / 10000;
            int64 const shifted = int64(perUnit) + shift;
            int64 const lo = int64(baseline->priceLow);
            int64 const hi = int64(baseline->priceHigh);
            if (shifted < lo)
                perUnit = uint32(lo);
            else if (shifted > hi)
                perUnit = uint32(hi);
            else
                perUnit = uint32(shifted);
        }

        basePrice = perUnit * count;
    }
    else
    {
        // No live listing and no baseline row. Legacy fallback.
        basePrice = vendorPerUnit * count * 5;
    }

    // Undercut. Relist uses a slightly more aggressive band than a fresh
    // listing because the item already failed to sell once.
    uint32 minPct = isRelist ? sPlayerbotAIConfig.auctionRelistUndercutPercentMin
                             : sPlayerbotAIConfig.auctionSellUndercutPercentMin;
    uint32 maxPct = isRelist ? sPlayerbotAIConfig.auctionRelistUndercutPercentMax
                             : sPlayerbotAIConfig.auctionSellUndercutPercentMax;
    if (maxPct < minPct)
        maxPct = minPct;
    uint32 const undercutPct = urand(minPct, maxPct);

    uint32 sellPrice = basePrice - (basePrice * undercutPct / 100);

    // Baseline floor and ceiling. Applied after the undercut so the floor
    // actually holds even when the undercut would have gone below it.
    // This is the rule that terminates the undercut race: once the cheapest
    // live listing reaches priceLow * count, further undercuts from other
    // bots cannot pull the price below that.
    if (baseline)
    {
        uint64 const floorFromBaseline = uint64(baseline->priceLow) * count;
        if (uint64(sellPrice) < floorFromBaseline)
            sellPrice = (floorFromBaseline > 0x7FFFFFFF)
                            ? 0x7FFFFFFF
                            : uint32(floorFromBaseline);

        uint64 const ceilingFromBaseline = uint64(baseline->priceHigh) * count;
        if (uint64(sellPrice) > ceilingFromBaseline)
            sellPrice = (ceilingFromBaseline > 0x7FFFFFFF)
                            ? 0x7FFFFFFF
                            : uint32(ceilingFromBaseline);
    }

    // Vendor floor, always applied last.
    if (sellPrice < floorPrice)
        sellPrice = floorPrice;

    if (sellPrice < 1)
        sellPrice = 1;

    // Starting bid.
    //   Mode 2 with a row: use the reference ratio (per-item when reliable,
    //   class prior otherwise). This is what makes startbids look human
    //   instead of always being exactly half the buyout.
    //   Mode 1, or no baseline row: fall back to buyout / 2.
    uint32 bid = sellPrice / 2;
    if (baseline)
    {
        uint32 ratioBp = baseline->bidRatioReliable
                            ? baseline->bidRatioMedian
                            : baseline->bidRatioClassPrior;
        if (ratioBp == 0)
            ratioBp = 5000;
        uint64 const fromRatio = uint64(sellPrice) * ratioBp / 10000;
        bid = (fromRatio > 0x7FFFFFFF) ? 0x7FFFFFFF : uint32(fromRatio);
    }

    if (bid < 1)
        bid = 1;
    if (bid > sellPrice)
        bid = sellPrice;

    outBid    = bid;
    outBuyout = sellPrice;
}

SellCandidate FindItemToSell(PlayerbotAI* botAI)
{
    if (sPlayerbotAIConfig.debugAuction)
    {
        LOG_DEBUG("playerbots", "[AH-TRACE] FindItemToSell CALLED bot={}",
                  (botAI && botAI->GetBot()) ? botAI->GetBot()->GetName() : "<null>");
    }

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

    if (sPlayerbotAIConfig.debugAuction)
    {
        LOG_ERROR("playerbots",
                  "[AH-TRACE] {} FindItemToSell cfg: minQuality={} minValue={} sellEnabled={} protectUseful={} protectBound={} protectQuest={} protectReagents={}",
                  bot->GetName(), minQuality, minValue,
                  sPlayerbotAIConfig.auctionEnableSelling ? 1 : 0,
                  sPlayerbotAIConfig.auctionProtectUsefulItems ? 1 : 0,
                  sPlayerbotAIConfig.auctionProtectBoundItems ? 1 : 0,
                  sPlayerbotAIConfig.auctionProtectQuestItems ? 1 : 0,
                  sPlayerbotAIConfig.auctionProtectClassReagents ? 1 : 0);
    }
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

    // Per-reason rejection counters. Only accumulate and emit when both
    // debugAuction and auctionRunAnalysis are on.
    bool const runAnalysis =
        sPlayerbotAIConfig.debugAuction && sPlayerbotAIConfig.auctionRunAnalysis;
    uint32 scanned = 0, rejTrade = 0, rejQuality = 0, rejEquipped = 0, rejValue = 0;
    uint32 rejQuest = 0, rejBound = 0, rejFullStack = 0, rejClassReagent = 0;
    uint32 rejUseful = 0, rejPotion = 0, rejVendorSold = 0, rejExcluded = 0;
    uint32 rejExhausted = 0;

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

        // Config-driven exclusion list. Server owners can list specific item
        // IDs to keep off the AH entirely; defaults are the tool items that
        // playerbots hands out freely and that a vendor stocks, which no
        // player would ever buy from the AH.
        if (sPlayerbotAIConfig.IsAuctionExcludedItem(p->ItemId))
        {
            if (runAnalysis) ++rejExcluded;
            continue;
        }

        // Vendor junk filter: skip items that an NPC vendor actually stocks,
        // unless it is a trade good, recipe, or consumable. Those three
        // classes sometimes have a BuyPrice on a limited-supply vendor while
        // still being legitimate AH items (e.g. rare recipes from a quest
        // vendor).
        //
        // Only npc_vendor is consulted here. item_template.BuyPrice is set on
        // almost every item in the game, so it cannot distinguish vendor-stock
        // items from ordinary loot. Specific tool items that a bot hands
        // itself but a vendor also stocks (Arclight Spanner, Mining Pick, etc)
        // are covered by AiPlayerbot.Auction.ExcludedItemIds instead.
        if (sPlayerbotAIConfig.auctionExcludeVendorSoldItems &&
            p->Class != ITEM_CLASS_TRADE_GOODS &&
            p->Class != ITEM_CLASS_RECIPE &&
            p->Class != ITEM_CLASS_CONSUMABLE &&
            sPlayerbotAIConfig.IsVendorSoldItem(p->ItemId))
        {
            if (runAnalysis) ++rejVendorSold;
            continue;
        }

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

        // Add Auctioneer: an item whose relist budget is exhausted must not
        // be re-queued. The mail step already declined to relist it; if we
        // re-add it here it gets listed again, expires again, and comes back
        // again -- the loop the retry cap exists to break. ItemUsageValue
        // already classifies it as VENDOR, so the vendor path will pick it up.
        if (sPlayerbotAIConfig.auctionMaxRelistRetries > 0 &&
            sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            uint32 const retries = sRandomPlayerbotMgr.GetValue(
                bot, "auction.relist." + std::to_string(p->ItemId));
            if (retries >= sPlayerbotAIConfig.auctionMaxRelistRetries)
            {
                if (runAnalysis) ++rejExhausted;
                continue;
            }
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

    if (runAnalysis)
    {
        // Show what each equipped bag slot currently holds, so we can tell
        // whether bots actually have their starter bags and how big they are.
        // Empty slots read as 0.
        std::string bagSizes;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            bagSizes += bag ? std::to_string(bag->GetBagSize()) : "0";
            bagSizes += ",";
        }

        LOG_ERROR("playerbots",
                  "[AH-TRACE] {} sellpick: scanned={} candidates={} "
                  "rejTrade={} rejQuality={} rejEquipped={} rejValue={} "
                  "rejQuest={} rejBound={} rejFullStack={} rejClassReagent={} "
                  "rejUseful={} rejPotion={} rejVendorSold={} rejExcluded={} "
                  "rejExhausted={} bags=[{}]",
                  bot->GetName(), scanned, candidates.size(),
                  rejTrade, rejQuality, rejEquipped, rejValue,
                  rejQuest, rejBound, rejFullStack, rejClassReagent,
                  rejUseful, rejPotion, rejVendorSold, rejExcluded, rejExhausted,
                  bagSizes);
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

    if (runAnalysis && poolSize > 0)
    {
        std::ostringstream pool;
        for (size_t i = 0; i < poolSize; ++i)
        {
            ItemTemplate const* tp = candidates[i].item->GetTemplate();
            pool << "[" << tp->ItemId << " q" << uint32(tp->Quality)
                 << " c" << uint32(tp->Class) << "] ";
        }
        LOG_ERROR("playerbots", "[AH-TRACE] {} candidate pool: {}", bot->GetName(), pool.str());
    }

    size_t const pick = urand(0, poolSize - 1);
    Item* best = candidates[pick].item;

    result.item = best;
    ComputeSellPrice(botAI, best, result.suggestedBid, result.suggestedBuyout);
    return result;
}

} // namespace PlayerbotAuction
