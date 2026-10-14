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

#ifndef PLAYERBOTS_BUYCONTAINERACTION_H
#define PLAYERBOTS_BUYCONTAINERACTION_H

#include "InventoryAction.h"

class Creature;

// Buy the biggest affordable container upgrade the bot can find at the
// current vendor or auctioneer. Called from the RPG vendor and auctioneer
// flows once the sell/collect/bid passes have finished.
//
// A container is an upgrade when it is strictly larger than the smallest
// currently equipped bag, or when any bag slot is empty (in which case
// any standard bag of any size is an upgrade). Only the standard bag
// subclass (ITEM_SUBCLASS_CONTAINER) is considered; quivers and ammo
// pouches are deliberately excluded because equipping them requires
// changes in EquipAction and ItemUsageValue that are not yet in place.
class BuyContainerAction : public InventoryAction
{
public:
    BuyContainerAction(PlayerbotAI* botAI) : InventoryAction(botAI, "buy container") {}

    bool Execute(Event event) override;

private:
    // Returns true if a container was purchased from the given vendor.
    bool BuyContainerFromVendor(Creature* vendor);

    // Returns true if a bid or buyout was placed on the AH for a container.
    bool BuyContainerFromAuctioneer(Creature* auctioneer);
};

#endif
