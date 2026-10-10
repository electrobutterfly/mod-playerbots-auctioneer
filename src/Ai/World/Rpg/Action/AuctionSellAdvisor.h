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
 
#ifndef PLAYERBOTS_AUCTIONSELLADVISOR_H
#define PLAYERBOTS_AUCTIONSELLADVISOR_H

#include "Common.h"

class Item;
class Player;
class PlayerbotAI;

namespace PlayerbotAuction
{
    struct SellCandidate
    {
        Item* item = nullptr;
        uint32 suggestedBid = 0;
        uint32 suggestedBuyout = 0;
    };

    // Returns the item the bot should sell next, or a SellCandidate with a null
    // item if there is nothing worth selling. Applies all the sell rules:
    //   - item is tradeable, quality at or above MinSellQuality
    //   - vendor sell price at or above MinSellValueCopper
    //   - not a protected class reagent (if ProtectClassReagents)
    //   - not a quest item (if ProtectQuestItems)
    //   - not bound (if ProtectBoundItems)
    //   - potion caps respected (KeepHealthPotions / KeepManaPotions, with large-bag bonus)
    //   - not currently equipped
    SellCandidate FindItemToSell(PlayerbotAI* botAI);

    // Compute a listing price for a given item. isRelist switches the undercut
    // range to the more aggressive relist percent band and leaves everything
    // else (current-market base, vendor fallback, bid = buyout/2) unchanged.
    void ComputeSellPrice(PlayerbotAI* botAI, Item* item, uint32& outBid, uint32& outBuyout,
         bool isRelist = false);

    // True if the item should be protected from selling because the bot's class
    // needs it as a reagent (portal runes, ankhs, soul shards, etc.).
    bool IsProtectedClassReagent(Player* bot, Item* item);

    // True if the item's largest bag size in the bot's equipped bags is at or
    // above the "large bag" threshold configured in playerbots_auction.conf.
    bool BotHasLargeBags(Player* bot);
}

#endif
