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
 
#ifndef PLAYERBOTS_AUCTIONCRAFTADVISOR_H
#define PLAYERBOTS_AUCTIONCRAFTADVISOR_H

#include "Common.h"
#include <vector>

class Player;

namespace PlayerbotAuction
{
    // Forward-declared in AuctionBaseline.h; used by the arbitrage path.
    // (The actual implementation lives in AuctionBaseline.cpp; this header
    // exists only to keep the arbitrage concern visually separate from
    // the loaders and the general policy function.)
}

#endif
