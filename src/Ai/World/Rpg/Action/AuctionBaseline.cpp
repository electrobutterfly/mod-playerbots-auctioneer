#include "AuctionBaseline.h"
#include "AuctionHouseMgr.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include <algorithm>

namespace PlayerbotAuction
{

uint64 AuctionBaselineMgr::MakeKey(uint8 faction, uint32 itemId, int32 suffix)
{
    // (faction << 48) | (itemId << 16) | (suffix & 0xFFFF)
    // Suffix is int32 in theory but fits inside 16 bits in practice for
    // every item that exists in AzerothCore item_template.
    return (uint64(faction) << 48)
         | (uint64(itemId)  << 16)
         | uint64(uint32(suffix) & 0xFFFF);
}

void AuctionBaselineMgr::Load()
{
    _items.clear();
    _snapshot.clear();

    // On a fresh install the table may not exist yet. If the query fails
    // the whole feature falls back to legacy behaviour via the consumer
    // side's nullptr checks.
    QueryResult result = CharacterDatabase.Query(
        "SELECT faction, item_id, suffix, "
        "       price_low, price_median, price_high, price_q1, price_q3, price_samples, "
        "       stack_typical, stack_low, stack_high, "
        "       listing_typical, listing_snapshots, "
        "       bid_ratio_median, bid_ratio_reliable, bid_ratio_class_prior_bp, "
        "       bid_ratio_samples, snapshot "
        "FROM auctioneer_baseline_item");

    if (!result)
    {
        LOG_WARN("playerbots",
                 ">> auctioneer_baseline_item is empty or missing; "
                 "baseline pricing will fall back to legacy behaviour. "
                 "Run the auctioneer_baseline import tool to populate it.");
        return;
    }

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

        // Defend against rows the tool left with zero in a fallback column.
        if (b.bidRatioMedian == 0)
            b.bidRatioMedian = 5000;
        if (b.bidRatioClassPrior == 0)
            b.bidRatioClassPrior = 9400;

        _items[MakeKey(faction, itemId, suffix)] = b;

        if (_snapshot.empty())
            _snapshot = fields[18].Get<std::string>();
    } while (result->NextRow());

    LOG_INFO("playerbots",
             ">> Loaded {} auctioneer baseline items, snapshot '{}'",
             _items.size(), _snapshot);
}

Baseline const* AuctionBaselineMgr::Lookup(uint8 faction, uint32 itemId,
                                           int32 suffix) const
{
    auto it = _items.find(MakeKey(faction, itemId, suffix));
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

bool ShouldListGivenBaselineCap(Player* bot, Item* item)
{
    if (!bot || !item)
        return true;

    if (sPlayerbotAIConfig.auctionBaselineMode != 2)
        return true;

    uint8 const faction = (bot->GetTeamId() == TEAM_ALLIANCE) ? 2 : 6;
    int32 const suffix  = item->GetItemRandomPropertyId();

    Baseline const* baseline =
        AuctionBaselineMgr::instance().LookupWithFallback(faction,
                                                          item->GetEntry(),
                                                          suffix);
    if (!baseline || baseline->listingTypical < 1)
        return true;

    uint32 liveCount = 0;
    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
    {
        for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
        {
            AuctionEntry* entry = it->second;
            if (entry && entry->item_template == item->GetEntry())
                ++liveCount;
        }
    }

    return liveCount < baseline->listingTypical;
}

}  // namespace PlayerbotAuction
