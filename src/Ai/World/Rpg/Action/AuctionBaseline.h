#ifndef PLAYERBOTS_AUCTIONBASELINE_H
#define PLAYERBOTS_AUCTIONBASELINE_H

#include "Common.h"
#include <string>
#include <unordered_map>
#include <vector>

class Item;
class Player;

namespace PlayerbotAuction
{
    // One reference row from auctioneer_baseline_item.
    struct Baseline
    {
        uint32 priceLow           = 0;
        uint32 priceMedian        = 0;
        uint32 priceHigh          = 0;
        uint32 priceQ1            = 0;
        uint32 priceQ3            = 0;
        uint32 priceSamples       = 0;

        uint32 stackTypical       = 1;
        uint32 stackLow           = 1;
        uint32 stackHigh          = 1;

        uint32 listingTypical     = 1;
        uint32 listingSnapshots   = 0;

        uint32 bidRatioMedian     = 5000;
        uint32 bidRatioClassPrior = 9400;
        uint32 bidRatioSamples    = 0;
        bool   bidRatioReliable   = false;

        // Percent change of the price median over the history window, in
        // basis points. +500 = +5%, -1200 = -12%. Range is clamped to
        // [-5000, +5000] on the parser side.
        int32  trendBp            = 0;
    };

    // One reference row from auctioneer_baseline_category. Every field
    // describes how many auctions of a given (class, quality) were live
    // at once on the source market, not a price.
    struct CategoryBaseline
    {
        uint32 sampleCount = 0;
        uint32 q1          = 0;
        uint32 median      = 0;
        uint32 adjLow      = 0;
        uint32 adjHigh     = 0;
    };

    // Process-wide cache of the two baseline tables. Loaded once at module
    // init. Consumers query the in-memory maps, never the DB.
    class AuctionBaselineMgr
    {
    public:
        static AuctionBaselineMgr& instance()
        {
            static AuctionBaselineMgr instance;
            return instance;
        }

        // Populate the caches from auctioneer_baseline_item and
        // auctioneer_baseline_category. Safe to call when either table is
        // empty or missing: logs a warning and leaves the corresponding
        // cache empty. Never throws to the caller.
        void Load();

        // Exact lookup by (faction, itemId, suffix). nullptr on miss.
        Baseline const* Lookup(uint8 faction, uint32 itemId, int32 suffix) const;

        // Same as Lookup, but when a specific suffix has no row, retries
        // with suffix 0 for the same item.
        Baseline const* LookupWithFallback(uint8 faction, uint32 itemId,
                                           int32 suffix) const;

        // Category-depth lookup by (faction, class, quality).
        CategoryBaseline const* LookupCategory(uint8 faction, uint8 itemClass,
                                               uint8 quality) const;

        size_t Size() const { return _items.size(); }
        size_t CategorySize() const { return _categories.size(); }
        std::string const& Snapshot() const { return _snapshot; }

    private:
        AuctionBaselineMgr() = default;
        ~AuctionBaselineMgr() = default;
        AuctionBaselineMgr(AuctionBaselineMgr const&) = delete;
        AuctionBaselineMgr& operator=(AuctionBaselineMgr const&) = delete;

        static uint64 MakeItemKey(uint8 faction, uint32 itemId, int32 suffix);
        static uint32 MakeCategoryKey(uint8 faction, uint8 itemClass,
                                      uint8 quality);

        std::unordered_map<uint64, Baseline>         _items;
        std::unordered_map<uint32, CategoryBaseline> _categories;
        std::string _snapshot;
    };

    // Compute the item IDs the bot must not sell on the AH because they are
    // reagents for a craft that is currently profitable against the
    // baseline. Empty when the feature is off, the bot has no profession
    // crafts, or none of them clear the configured margin.
    std::vector<uint32> ComputeProtectedReagents(Player* bot);

    // Central policy decision for the sell path. Called per item, in the
    // order the items appear in the pendingSells queue.
    //
    //   Mode 1 (legacy):        always returns true.
    //   Mode 2 (baseline):
    //     false when the item is a protected reagent (craft arbitrage)
    //     false when the (class, quality) category is bloated
    //     false when the per-item cap is reached, UNLESS the category is
    //       starved, in which case the cap is overridden
    //     true otherwise
    bool ShouldListGivenBaselinePolicy(Player* bot, Item* item,
                                       std::vector<uint32> const& protectedReagents);
}

#endif
