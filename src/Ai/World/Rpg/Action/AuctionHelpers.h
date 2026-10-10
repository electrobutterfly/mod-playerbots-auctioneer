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
}

#endif
