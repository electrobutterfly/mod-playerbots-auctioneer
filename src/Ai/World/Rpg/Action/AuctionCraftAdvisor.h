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
