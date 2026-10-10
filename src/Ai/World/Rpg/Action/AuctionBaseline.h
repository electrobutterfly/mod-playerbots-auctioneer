#ifndef PLAYERBOTS_AUCTIONBASELINE_H
#define PLAYERBOTS_AUCTIONBASELINE_H

#include "Common.h"
#include <string>
#include <unordered_map>

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
    };

    // Process-wide cache of the baseline table. Loaded once at module init.
    // Consumers query the in-memory map, never the DB.
    class AuctionBaselineMgr
    {
    public:
        static AuctionBaselineMgr& instance()
        {
            static AuctionBaselineMgr instance;
            return instance;
        }

        // Populate the cache from auctioneer_baseline_item. Safe to call
        // when the table is empty or does not exist: logs a warning and
        // leaves the cache empty. Never throws to the caller.
        void Load();

        // Exact lookup by (faction, itemId, suffix). nullptr on miss.
        Baseline const* Lookup(uint8 faction, uint32 itemId, int32 suffix) const;

        // Same as Lookup, but when a specific suffix has no row, retries
        // with suffix 0 for the same item. The plain-suffix row is a
        // reasonable proxy when the exact variant is not in the reference
        // market yet.
        Baseline const* LookupWithFallback(uint8 faction, uint32 itemId,
                                           int32 suffix) const;

        size_t Size() const { return _items.size(); }
        std::string const& Snapshot() const { return _snapshot; }

    private:
        AuctionBaselineMgr() = default;
        ~AuctionBaselineMgr() = default;
        AuctionBaselineMgr(AuctionBaselineMgr const&) = delete;
        AuctionBaselineMgr& operator=(AuctionBaselineMgr const&) = delete;

        static uint64 MakeKey(uint8 faction, uint32 itemId, int32 suffix);

        std::unordered_map<uint64, Baseline> _items;
        std::string _snapshot;
    };

    // Per-item listing cap. Returns false when the market for this item
    // on this faction is already at or above the baseline's typical
    // concurrent listing count. Always returns true when the baseline is
    // disabled (Mode 1) or no row exists.
    bool ShouldListGivenBaselineCap(Player* bot, Item* item);

}  // namespace PlayerbotAuction

#endif
