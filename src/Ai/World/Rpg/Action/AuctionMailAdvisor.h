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
 
#ifndef PLAYERBOTS_AUCTIONMAILADVISOR_H
#define PLAYERBOTS_AUCTIONMAILADVISOR_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Player;
class PlayerbotAI;

namespace PlayerbotAuction
{
    // Get MAIL AUCTION returned mail
    // True if the bot has at least one unclaimed MAIL_AUCTION returned-item
    // mail. Fills outEntries with the item_template of every returned item;
    // duplicates are preserved, so outEntries.size() == total item count.
    // Never reads mail from players. Does not mutate mail state.
    bool BotHasExpiredAuctionMail(Player* bot, std::vector<uint32>& outEntries);

    // Add Auctioneer
    // Diagnostic-only. Logs a one-line breakdown of everything in the bot's
    // mailbox when debugAuction && auctionRunAnalysis are both on. Safe to
    // call on every AH visit; does not mutate anything.
    void LogMailboxContents(Player* bot);

    // Nearest mailbox within INTERACTION_DISTANCE of the bot, or empty.
    ObjectGuid FindMailboxInRange(PlayerbotAI* botAI);

    // Nearest mailbox with no distance check (walk target). Empty if none.
    ObjectGuid FindNearestMailbox(PlayerbotAI* botAI);

    // Take items from each MAIL_AUCTION returned-item mail into the bot's bags,
    // delete the emptied mail rows, and return the collected item_template IDs.
    // Only ever calls HandleMailTakeItem / HandleMailDelete.
    std::vector<uint32> TakeExpiredAuctionMail(PlayerbotAI* botAI, ObjectGuid mailbox);

    // Number of empty slots across the bot's backpack and equipped bags.
    uint32 CountFreeBagSlots(Player* bot);

    // True if the bot has at least one uncollected bag-like item won at
    // the auction house and still in the mailbox. Extends the mailbox
    // walk trigger so a bot with a won bag still in transit walks to the
    // mailbox on its next AH visit rather than leaving it stuck forever.
    bool BotHasUncollectedWonBags(Player* bot);

    // Number of bag-like items won at the auction house and still in the
    // mailbox. Bags bought on the AH arrive by mail, not by inventory; a
    // bot that has already won a bag but not yet collected it must not
    // buy another bag from a vendor or another auction, or the copper is
    // wasted on a duplicate that may not even be an upgrade once the
    // mail bag is equipped.
    uint32 CountUncollectedWonBags(Player* bot);

    // Collect every won-bag mail from the mailbox, storing each bag into
    // the bot's bags. Returns the item_template IDs of every collected
    // bag. The bags will then be picked up by the item-push-result
    // handler and equipped by EquipUpgradesPacketAction.
    std::vector<uint32> TakeWonBagMail(PlayerbotAI* botAI, ObjectGuid mailbox);
}

#endif
