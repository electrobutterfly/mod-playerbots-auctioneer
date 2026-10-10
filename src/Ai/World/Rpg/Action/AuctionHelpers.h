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
 
#ifndef PLAYERBOTS_AUCTIONHELPERS_H
#define PLAYERBOTS_AUCTIONHELPERS_H

#include "Common.h"

class Creature;
class Item;
class Player;
class PlayerbotAI;

namespace PlayerbotAuction
{
    // Find an auctioneer within interaction range of the bot.
    // Returns nullptr if there isn't one. Uses the bot's "nearest npcs"
    // AI value, so the auctioneer must be visible to the bot.
    Creature* FindAuctioneerInRange(PlayerbotAI* botAI);

    // Find a tradeable item with the given entry in the bot's bags.
    // Returns nullptr if not found, or if the item cannot be traded.
    Item* FindTradeableItemInBags(PlayerbotAI* botAI, uint32 itemId);

    // Send CMSG_AUCTION_PLACE_BID. Caller is responsible for having verified
    // the price against the current bid. Returns true if the packet was sent.
    bool SendPlaceBid(Player* bot, Creature* auctioneer, uint32 auctionId, uint32 priceCopper);

    // Send CMSG_AUCTION_SELL_ITEM. `durationMinutes` must be 720, 1440, or 2880.
    // Returns true if the packet was sent. Verifies the item is in the bot's bags.
    bool SendSellItem(Player* bot, Creature* auctioneer, Item* item,
                      uint32 bidCopper, uint32 buyoutCopper, uint32 durationMinutes);

    // Number of live auctions in the AH currently owned by this bot.
    uint32 CountActiveListings(Player* bot);

    // Cancel and delete every live auction owned by `bot`, refunding any
    // bidder through the core's normal cancel mail. Returns count removed.
    uint32 RemoveAllBotAuctions(Player* bot);

    // Delete every mail row for this bot, both in-memory and in the DB.
    // Used by the full-randomize reset path so a wiped bot does not carry
    // stale expired-auction mail into its new life.
    uint32 CleanupBotMail(Player* bot);
}

#endif
