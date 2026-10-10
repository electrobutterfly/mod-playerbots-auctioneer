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
}

#endif
