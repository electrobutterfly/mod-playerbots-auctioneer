#ifndef PLAYERBOTS_AUCTIONBIDTRACKER_H
#define PLAYERBOTS_AUCTIONBIDTRACKER_H

#include "Common.h"
#include "ItemUsageValue.h"
#include "PlayerbotAI.h"

class Player;

namespace PlayerbotAuction
{
    // Load every persisted bid for this bot into botAI->trackedAuctions.
    // No-op if already loaded this session. Safe to call from any AH entry
    // point; the first call does the work, later calls return immediately.
    void LoadTrackedBids(PlayerbotAI* botAI);

    // Insert or overwrite a single bid row.
    void UpsertTrackedBid(PlayerbotAI* botAI, PlayerbotAI::TrackedAuction const& bid);

    // Remove a single bid row. Also removes the in-memory copy if the caller
    // is iterating a vector that is still in use.
    void RemoveTrackedBid(PlayerbotAI* botAI, uint32 auctionId);

    // Remove every bid row for this bot. Used by the reset and delete paths.
    void RemoveAllTrackedBids(PlayerbotAI* botAI);

    // True if this ItemUsage means the bot still wants the item, matching the
    // buy-side filter in AuctionBuyAdvisor::FindAuctionToBuy.
    bool IsUsageWanted(ItemUsage usage);
}

#endif
