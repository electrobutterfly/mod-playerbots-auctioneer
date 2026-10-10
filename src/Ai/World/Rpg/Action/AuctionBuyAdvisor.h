#ifndef PLAYERBOTS_AUCTIONBUYADVISOR_H
#define PLAYERBOTS_AUCTIONBUYADVISOR_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;
class PlayerbotAI;
struct AuctionEntry;

namespace PlayerbotAuction
{
    enum class SellerType : uint8
    {
        Unknown    = 0,
        RealPlayer = 1,
        Playerbot  = 2,
        AhBot      = 3
    };

        struct BuyCandidate
    {
        AuctionEntry* auction = nullptr;
        uint32 maxWillingPrice = 0;
        uint32 bidAmount       = 0;
        uint32 buyoutAmount    = 0;
        bool   doBuyout        = false;
        SellerType seller      = SellerType::Unknown;
        uint8  usageTier       = 255;  // lower = higher priority
    };

    // Classify the seller of a given auction. Uses the configured
    // AhBotGUIDs list and the playerbots random-account list.
    SellerType ClassifySeller(Player* bot, AuctionEntry* entry);

    // Returns the single best listing this bot should try to bid on or buy out,
    // or a candidate with a null auction pointer if nothing qualifies.
    BuyCandidate FindAuctionToBuy(PlayerbotAI* botAI);
}

#endif
