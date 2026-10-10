/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AuctionBuyAdvisor.h"
#include "AiObjectContext.h"
#include "AuctionHouseMgr.h"
#include "CharacterCache.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include <algorithm>

namespace PlayerbotAuction
{

namespace
{
    // Usages that mean "this bot wants this item".
    bool IsUsageWanted(ItemUsage usage)
    {
        switch (usage)
        {
            case ITEM_USAGE_EQUIP:
            case ITEM_USAGE_REPLACE:
            case ITEM_USAGE_QUEST:
            case ITEM_USAGE_SKILL:
            case ITEM_USAGE_USE:
            case ITEM_USAGE_AMMO:
            case ITEM_USAGE_DISENCHANT:
                return true;
            default:
                return false;
        }
    }

    // Seller weight for effective-price sorting. Higher = preferred.
    float SellerWeightFor(SellerType seller)
    {
        switch (sPlayerbotAIConfig.auctionSellerPreferenceAggression)
        {
            case 1:  // mild: RealPlayer 3, Playerbot 2, AhBot 1
                switch (seller)
                {
                    case SellerType::RealPlayer: return 3.0f;
                    case SellerType::Playerbot:  return 2.0f;
                    case SellerType::AhBot:      return 1.0f;
                    default:                     return 1.0f;
                }
            case 2:  // strong: 10 / 5 / 1
                switch (seller)
                {
                    case SellerType::RealPlayer: return 10.0f;
                    case SellerType::Playerbot:  return 5.0f;
                    case SellerType::AhBot:      return 1.0f;
                    default:                     return 1.0f;
                }
            case 3:  // absolute: handled separately, weight is 1 here
                return 1.0f;
            case 0:  // custom
            default:
                switch (seller)
                {
                    case SellerType::RealPlayer: return float(sPlayerbotAIConfig.auctionSellerPreferenceRealPlayer);
                    case SellerType::Playerbot:  return float(sPlayerbotAIConfig.auctionSellerPreferencePlayerbot);
                    case SellerType::AhBot:      return float(sPlayerbotAIConfig.auctionSellerPreferenceAhBot);
                    default:                     return 1.0f;
                }
        }
    }

    float EffectivePrice(uint32 price, SellerType seller)
    {
        float w = SellerWeightFor(seller);
        if (w <= 0.0f)
            return 1e30f;
        return float(price) / w;
    }
}  // namespace

SellerType ClassifySeller(Player* /*bot*/, AuctionEntry* entry)
{
    if (!entry || entry->owner.IsEmpty())
        return SellerType::Unknown;

    uint32 const guidLow = entry->owner.GetCounter();

    // Configured AH bot GUIDs
    for (uint32 ahGuid : sPlayerbotAIConfig.auctionAhBotGUIDs)
        if (ahGuid == guidLow)
            return SellerType::AhBot;

    // Is it a random playerbot account?
    uint32 const accountId = sCharacterCache->GetCharacterAccountIdByGuid(entry->owner);
    if (accountId && sPlayerbotAIConfig.IsInRandomAccountList(accountId))
        return SellerType::Playerbot;

    // Online player with a bot AI
    if (Player* seller = ObjectAccessor::FindConnectedPlayer(entry->owner))
    {
        if (GET_PLAYERBOT_AI(seller))
            return SellerType::Playerbot;
    }

    return SellerType::RealPlayer;
}

BuyCandidate FindAuctionToBuy(PlayerbotAI* botAI)
{
    BuyCandidate result;
    if (!botAI)
        return result;

    Player* bot = botAI->GetBot();
    if (!bot)
        return result;

    if (!sPlayerbotAIConfig.auctionEnableBuying)
        return result;

    AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction());
    if (!ahObj)
        return result;

    // Budget for the whole visit
    uint32 const budget = uint32(uint64(bot->GetMoney()) *
                                 sPlayerbotAIConfig.auctionMaxSpendPercentPerVisit / 100);
    if (budget < 1)
        return result;

    // Roll a price multiplier for this visit
    uint32 multMin = sPlayerbotAIConfig.auctionBuyPriceMultiplierMin;
    uint32 multMax = sPlayerbotAIConfig.auctionBuyPriceMultiplierMax;
    if (multMax < multMin)
        multMax = multMin;
    uint32 const priceMultiplier = urand(multMin, multMax);

    std::vector<BuyCandidate> candidates;

    for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
    {
        AuctionEntry* entry = it->second;
        if (!entry)
            continue;

        if (entry->owner == bot->GetGUID())
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry->item_template);
        if (!proto)
            continue;

        // Skip listing prices we can't reason about
        uint32 const listingPrice = entry->buyout ? entry->buyout : entry->startbid;
        if (listingPrice == 0)
            continue;

        // Ask the game: does this bot want this item?
        AiObjectContext* ctx = botAI->GetAiObjectContext();
        ItemUsage usage = ctx->GetValue<ItemUsage>("item usage", proto->ItemId)->Get();
        if (!IsUsageWanted(usage))
            continue;

        // Vendor price gives us a stable anchor for "what is this worth".
        uint32 const vendorValue = proto->SellPrice;
        if (vendorValue == 0)
            continue;

        uint32 const maxWilling = vendorValue * priceMultiplier * entry->itemCount;

        SellerType const seller = ClassifySeller(bot, entry);
        if (SellerWeightFor(seller) <= 0.0f)
            continue;

        // Absolute aggression: skip bot listings if any real-player listing
        // exists for this same item. We do a second pass later for that; for
        // now, keep candidates from all sellers.

        bool doBuyout = false;
        uint32 bidAmount = 0;
        uint32 buyoutAmount = 0;

        if (entry->buyout != 0 && entry->buyout <= maxWilling &&
            sPlayerbotAIConfig.auctionAllowBuyout)
        {
            doBuyout = true;
            buyoutAmount = entry->buyout;
        }
        else if (entry->bid == 0)
        {
            if (entry->startbid > maxWilling)
                continue;
            bidAmount = entry->startbid;
        }
        else
        {
            uint32 const nextBid = entry->bid + entry->GetAuctionOutBid();
            if (nextBid > maxWilling)
                continue;
            bidAmount = nextBid;
        }

        // Don't outbid real players if configured
        if (sPlayerbotAIConfig.auctionDontOutbidRealPlayers &&
            !doBuyout && entry->bidder && !entry->bidder.IsEmpty())
        {
            if (ClassifySeller(bot, entry) != SellerType::RealPlayer)
            {
                // Check the current bidder
                uint32 const bidderAccount = sCharacterCache->GetCharacterAccountIdByGuid(entry->bidder);
                bool const bidderIsBot = bidderAccount &&
                    sPlayerbotAIConfig.IsInRandomAccountList(bidderAccount);
                if (!bidderIsBot)
                    continue;  // real player is the current high bidder
            }
        }

        uint32 const spend = doBuyout ? buyoutAmount : bidAmount;
        if (spend > budget || spend > bot->GetMoney())
            continue;

        BuyCandidate cand;
        cand.auction        = entry;
        cand.maxWillingPrice = maxWilling;
        cand.bidAmount      = bidAmount;
        cand.buyoutAmount   = buyoutAmount;
        cand.doBuyout       = doBuyout;
        cand.seller         = seller;
        candidates.push_back(cand);
    }

    if (candidates.empty())
        return result;

    // Absolute aggression: if any RealPlayer listing exists, filter out others
    if (sPlayerbotAIConfig.auctionSellerPreferenceAggression == 3)
    {
        bool const anyReal = std::any_of(candidates.begin(), candidates.end(),
            [](BuyCandidate const& c) { return c.seller == SellerType::RealPlayer; });
        if (anyReal)
        {
            candidates.erase(
                std::remove_if(candidates.begin(), candidates.end(),
                    [](BuyCandidate const& c) { return c.seller != SellerType::RealPlayer; }),
                candidates.end());
        }
    }

    // Sort by weighted effective price, cheapest first
    std::sort(candidates.begin(), candidates.end(),
        [](BuyCandidate const& a, BuyCandidate const& b)
        {
            uint32 const pa = a.doBuyout ? a.buyoutAmount : a.bidAmount;
            uint32 const pb = b.doBuyout ? b.buyoutAmount : b.bidAmount;
            return EffectivePrice(pa, a.seller) < EffectivePrice(pb, b.seller);
        });

    return candidates.front();
}

}  // namespace PlayerbotAuction
