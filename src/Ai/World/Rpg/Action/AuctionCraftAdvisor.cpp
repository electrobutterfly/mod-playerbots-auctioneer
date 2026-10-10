#include "AuctionCraftAdvisor.h"
#include "AuctionBaseline.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>

namespace PlayerbotAuction
{

std::vector<uint32> ComputeProtectedReagents(Player* bot)
{
    std::vector<uint32> result;

    if (!bot)
        return result;
    if (sPlayerbotAIConfig.auctionBaselineMode != 2)
        return result;
    if (!sPlayerbotAIConfig.auctionCraftArbitrageEnabled)
        return result;

    uint8 const faction = (bot->GetTeamId() == TEAM_ALLIANCE) ? 2 : 6;
    auto const& mgr = AuctionBaselineMgr::instance();

    uint32 const minMarginPct = sPlayerbotAIConfig.auctionCraftMarginPercent;

    for (auto const& [spellId, spellState] : bot->GetSpellMap())
    {
        if (!spellState)
            continue;
        if (spellState->State == PLAYERSPELL_REMOVED)
            continue;
        if (!spellState->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        // Only spells that create an item.
        if (spellInfo->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM)
            continue;

        uint32 const outputItem = spellInfo->Effects[EFFECT_0].ItemType;
        if (!outputItem)
            continue;

        // Output must have a baseline we can price against.
        Baseline const* outB = mgr.Lookup(faction, outputItem, 0);
        if (!outB || outB->priceSamples < 3 || outB->priceLow == 0)
            continue;

        // Conservative: assume the output sells at the low end of the band,
        // then deduct the standard auction cut.
        uint64 outputValue = uint64(outB->priceLow);
        uint64 const cut   = outputValue * 5 / 100;
        if (outputValue <= cut)
            continue;
        outputValue -= cut;

        // Sum reagent costs from the baseline lows. Skip the whole recipe
        // if any reagent has no baseline: we cannot price it, and
        // pretending it costs zero would produce false positives.
        uint64 reagentCost = 0;
        bool reagentsPriceable = true;
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            uint32 const rId    = spellInfo->Reagent[i];
            uint32 const rCount = spellInfo->ReagentCount[i];
            if (!rId || !rCount)
                continue;

            Baseline const* rB = mgr.Lookup(faction, rId, 0);
            if (!rB || rB->priceSamples < 3 || rB->priceLow == 0)
            {
                reagentsPriceable = false;
                break;
            }
            reagentCost += uint64(rB->priceLow) * uint64(rCount);
        }
        if (!reagentsPriceable || reagentCost == 0)
            continue;

        // Margin check.
        if (outputValue <= reagentCost)
            continue;
        uint64 const margin = outputValue - reagentCost;
        if (margin * 100 < reagentCost * uint64(minMarginPct))
            continue;

        // Profitable. Reserve every reagent of this recipe.
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            uint32 const rId = spellInfo->Reagent[i];
            if (!rId)
                continue;
            if (std::find(result.begin(), result.end(), rId) == result.end())
                result.push_back(rId);
        }
    }

    return result;
}

}  // namespace PlayerbotAuction
