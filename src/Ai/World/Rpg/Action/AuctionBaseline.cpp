#include "AuctionBaseline.h"
#include "AuctionHouseMgr.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include <algorithm>

namespace PlayerbotAuction
{

uint64 AuctionBaselineMgr::MakeItemKey(uint8 faction, uint32 itemId, int32 suffix)
{
    return (uint64(faction) << 48)
         | (uint64(itemId)  << 16)
         | uint64(uint32(suffix) & 0xFFFF);
}

uint32 AuctionBaselineMgr::MakeCategoryKey(uint8 faction, uint8 itemClass,
                                           uint8 quality)
{
    return (uint32(faction) << 16)
         | (uint32(itemClass) << 8)
         | uint32(quality);
}

void AuctionBaselineMgr::Load()
{
    _items.clear();
    _categories.clear();
    _snapshot.clear();

    // ---- item table --------------------------------------------------
    QueryResult result = CharacterDatabase.Query(
        "SELECT faction, item_id, suffix, "
        "       price_low, price_median, price_high, price_q1, price_q3, price_samples, "
        "       stack_typical, stack_low, stack_high, "
        "       listing_typical, listing_snapshots, "
        "       bid_ratio_median, bid_ratio_reliable, bid_ratio_class_prior_bp, "
        "       bid_ratio_samples, trend_bp, snapshot "
        "FROM auctioneer_baseline_item");

    if (!result)
    {
        LOG_WARN("playerbots",
                 ">> auctioneer_baseline_item is empty or missing; "
                 "baseline pricing will fall back to legacy behaviour.");
    }
    else
    {
        do
        {
            Field* fields = result->Fetch();

            uint8  faction = fields[0].Get<uint8>();
            uint32 itemId  = fields[1].Get<uint32>();
            int32  suffix  = fields[2].Get<int32>();

            Baseline b;
            b.priceLow           = fields[3].Get<uint32>();
            b.priceMedian        = fields[4].Get<uint32>();
            b.priceHigh          = fields[5].Get<uint32>();
            b.priceQ1            = fields[6].Get<uint32>();
            b.priceQ3            = fields[7].Get<uint32>();
            b.priceSamples       = fields[8].Get<uint32>();
            b.stackTypical       = std::max<uint32>(1, fields[9].Get<uint32>());
            b.stackLow           = std::max<uint32>(1, fields[10].Get<uint32>());
            b.stackHigh          = std::max<uint32>(1, fields[11].Get<uint32>());
            b.listingTypical     = std::max<uint32>(1, fields[12].Get<uint32>());
            b.listingSnapshots   = fields[13].Get<uint32>();
            b.bidRatioMedian     = fields[14].Get<uint32>();
            b.bidRatioReliable   = fields[15].Get<uint8>() != 0;
            b.bidRatioClassPrior = fields[16].Get<uint32>();
            b.bidRatioSamples    = fields[17].Get<uint32>();
            b.trendBp            = fields[18].Get<int32>();

            if (b.bidRatioMedian == 0)
                b.bidRatioMedian = 5000;
            if (b.bidRatioClassPrior == 0)
                b.bidRatioClassPrior = 9400;

            _items[MakeItemKey(faction, itemId, suffix)] = b;

            if (_snapshot.empty())
                _snapshot = fields[19].Get<std::string>();
        } while (result->NextRow());

        LOG_INFO("playerbots",
                 ">> Loaded {} auctioneer baseline items, snapshot '{}'",
                 _items.size(), _snapshot);
    }

    // ---- category table ----------------------------------------------
    QueryResult catResult = CharacterDatabase.Query(
        "SELECT faction, item_class, quality, "
        "       sample_count, q1, median, adj_low, adj_high "
        "FROM auctioneer_baseline_category");

    if (!catResult)
    {
        LOG_WARN("playerbots",
                 ">> auctioneer_baseline_category is empty or missing; "
                 "category-depth gating will be inactive.");
        return;
    }

    do
    {
        Field* fields = catResult->Fetch();

        uint8 faction   = fields[0].Get<uint8>();
        uint8 itemClass = fields[1].Get<uint8>();
        uint8 quality   = fields[2].Get<uint8>();

        CategoryBaseline c;
        c.sampleCount = fields[3].Get<uint32>();
        c.q1          = fields[4].Get<uint32>();
        c.median      = fields[5].Get<uint32>();
        c.adjLow      = fields[6].Get<uint32>();
        c.adjHigh     = fields[7].Get<uint32>();

        _categories[MakeCategoryKey(faction, itemClass, quality)] = c;
    } while (catResult->NextRow());

    LOG_INFO("playerbots",
             ">> Loaded {} auctioneer baseline categories",
             _categories.size());
}

Baseline const* AuctionBaselineMgr::Lookup(uint8 faction, uint32 itemId,
                                           int32 suffix) const
{
    auto it = _items.find(MakeItemKey(faction, itemId, suffix));
    if (it == _items.end())
        return nullptr;
    return &it->second;
}

Baseline const* AuctionBaselineMgr::LookupWithFallback(uint8 faction,
                                                       uint32 itemId,
                                                       int32 suffix) const
{
    if (Baseline const* exact = Lookup(faction, itemId, suffix))
        return exact;

    if (suffix != 0)
        if (Baseline const* plain = Lookup(faction, itemId, 0))
            return plain;

    return nullptr;
}

CategoryBaseline const* AuctionBaselineMgr::LookupCategory(
    uint8 faction, uint8 itemClass, uint8 quality) const
{
    auto it = _categories.find(MakeCategoryKey(faction, itemClass, quality));
    if (it == _categories.end())
        return nullptr;
    return &it->second;
}

bool ShouldListGivenBaselinePolicy(Player* bot, Item* item,
                                   std::vector<uint32> const& protectedReagents)
{
    if (!bot || !item)
        return true;

    if (sPlayerbotAIConfig.auctionBaselineMode != 2)
        return true;

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return true;

    // 1. Crafting-arbitrage reserve. An item that is a reagent for a
    // currently profitable craft should be held, not dumped at mat price.
    for (uint32 const rid : protectedReagents)
    {
        if (rid == proto->ItemId)
            return false;
    }

    uint8 const faction = (bot->GetTeamId() == TEAM_ALLIANCE) ? 2 : 6;

    // Single pass over the AH to gather both counts.
    uint32 liveForItem     = 0;
    uint32 liveInCategory  = 0;
    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
    {
        for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
        {
            AuctionEntry* entry = it->second;
            if (!entry)
                continue;

            if (entry->item_template == proto->ItemId)
                ++liveForItem;

            ItemTemplate const* ep = sObjectMgr->GetItemTemplate(entry->item_template);
            if (ep && ep->Class == proto->Class && ep->Quality == proto->Quality)
                ++liveInCategory;
        }
    }

    // 2. Category depth.
    bool categoryStarving = false;
    CategoryBaseline const* cat =
        AuctionBaselineMgr::instance().LookupCategory(faction, proto->Class,
                                                      proto->Quality);
    if (cat && cat->median > 0)
    {
        if (liveInCategory > cat->median)
            return false;  // bloated: skip entirely
        if (liveInCategory < cat->q1)
            categoryStarving = true;  // starving: override per-item cap
    }

    // 3. Per-item cap (only when the category is not starving).
    if (!categoryStarving)
    {
        Baseline const* baseline =
            AuctionBaselineMgr::instance().LookupWithFallback(
                faction, proto->ItemId, item->GetItemRandomPropertyId());

        if (baseline && baseline->listingTypical > 0 &&
            liveForItem >= baseline->listingTypical)
        {
            return false;
        }
    }

    return true;
}

}  // namespace PlayerbotAuction
