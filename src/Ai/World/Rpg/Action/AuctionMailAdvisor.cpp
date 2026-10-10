/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AuctionMailAdvisor.h"
#include "AiObjectContext.h"
#include "Bag.h"
#include "GameObject.h"
#include "Item.h"
#include "Mail.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>

namespace PlayerbotAuction
{

namespace
{
    // Add Auctioneer
    // A mail item came back because its auction expired unsold.
    //
    // The core does NOT set MAIL_CHECK_MASK_RETURNED (0x02) on expired-auction
    // mail. Empirically it sets MAIL_CHECK_MASK_COPIED (0x04) instead, so
    // filtering on the returned bit rejects everything. The authoritative
    // signal is the mail subject, which the core formats as:
    //
    //     "{itemTemplateId}:{unknown}:{response}:{auctionId}:{itemCount}"
    //
    // The `response` field matches the AuctionMailAnswers enum:
    //   1 = AUCTION_WON        -> the *buyer's* mail; the item is what they won
    //   2 = AUCTION_SUCCESSFUL -> the *seller's* money mail, no item attached
    //   3 = AUCTION_EXPIRED    -> the seller's item returned unsold; what we want
    //
    // Everything else (6 = bid error, etc.) is not a return we can relist.
    bool IsReturnedAuctionMail(Mail const* mail)
    {
        if (!mail)
            return false;
        if (mail->state == MAIL_STATE_DELETED)
            return false;
        if (mail->messageType != MAIL_AUCTION)
            return false;
        if (mail->items.empty())
            return false;
        if (mail->deliver_time > time(nullptr))
            return false;

        // Subject: "{item}:{unknown}:{response}:{auctionId}:{count}". Reject
        // anything whose shape doesn't match before we look at the response.
        std::string const& s = mail->subject;
        size_t const c1 = s.find(':');
        if (c1 == std::string::npos)
            return false;
        size_t const c2 = s.find(':', c1 + 1);
        if (c2 == std::string::npos)
            return false;
        size_t const c3 = s.find(':', c2 + 1);
        if (c3 == std::string::npos)
            return false;

        std::string const response = s.substr(c2 + 1, c3 - c2 - 1);
        return response == "3";  // AUCTION_EXPIRED
    }
}  // namespace

bool BotHasExpiredAuctionMail(Player* bot, std::vector<uint32>& outEntries)
{
    outEntries.clear();
    if (!bot)
        return false;

    // Add Auctioneer
    LogMailboxContents(bot);

    for (PlayerMails::const_iterator it = bot->GetMails().begin(); it != bot->GetMails().end(); ++it)
    {
        Mail* mail = *it;
        if (!IsReturnedAuctionMail(mail))
            continue;

        for (MailItemInfo const& mii : mail->items)
            outEntries.push_back(mii.item_template);
    }

    return !outEntries.empty();
}

ObjectGuid FindMailboxInRange(PlayerbotAI* botAI)
{
    if (!botAI)
        return ObjectGuid::Empty;

    Player* bot = botAI->GetBot();
    if (!bot)
        return ObjectGuid::Empty;

    GuidVector const& gos =
        botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();

    for (ObjectGuid const& guid : gos)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;
        if (go->GetGoType() != GAMEOBJECT_TYPE_MAILBOX)
            continue;
        if (bot->GetDistance(go) > INTERACTION_DISTANCE)
            continue;
        return guid;
    }

    return ObjectGuid::Empty;
}

ObjectGuid FindNearestMailbox(PlayerbotAI* botAI)
{
    if (!botAI)
        return ObjectGuid::Empty;

    GuidVector const& gos =
        botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();

    for (ObjectGuid const& guid : gos)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;
        if (go->GetGoType() != GAMEOBJECT_TYPE_MAILBOX)
            continue;
        return guid;
    }

    return ObjectGuid::Empty;
}

std::vector<uint32> TakeExpiredAuctionMail(PlayerbotAI* botAI, ObjectGuid mailbox)
{
    std::vector<uint32> collected;
    if (!botAI || mailbox.IsEmpty())
        return collected;

    Player* bot = botAI->GetBot();
    if (!bot)
        return collected;

    // Snapshot matching mail pointers up front: taking items mutates the
    // mail object contents we would otherwise iterate live.
    std::vector<Mail*> mails;
    for (PlayerMails::const_iterator it = bot->GetMails().begin(); it != bot->GetMails().end(); ++it)
    {
        Mail* mail = *it;
        if (IsReturnedAuctionMail(mail))
            mails.push_back(mail);
    }

    for (Mail* mail : mails)
    {
        // Copy the item list: HandleMailTakeItem shrinks mail->items.
        std::vector<MailItemInfo> items = mail->items;
        std::vector<uint32> taken;

        for (MailItemInfo const& mii : items)
        {
            WorldPacket packet;
            packet << mailbox;
            packet << mail->messageID;
            packet << mii.item_guid;
            packet.rpos(0);
            bot->GetSession()->HandleMailTakeItem(packet);
            taken.push_back(mii.item_template);
        }

        WorldPacket del;
        del << mailbox;
        del << mail->messageID;
        del << uint32(0);  // mailTemplateId
        del.rpos(0);
        bot->GetSession()->HandleMailDelete(del);

        for (uint32 entry : taken)
            collected.push_back(entry);
    }

    return collected;
}

uint32 CountFreeBagSlots(Player* bot)
{
    if (!bot)
        return 0;

    uint32 used = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++used;
    }

    uint32 free = (INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START) - used;

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        if (Bag const* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot))
        {
            ItemTemplate const* proto = bag->GetTemplate();
            if (proto && proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER)
                free += bag->GetFreeSlots();
        }
    }

    return free;
}

// Add Auctioneer
// Diagnostic-only. Emits one [AH-TRACE] line per bot per AH visit showing what
// is actually in the mailbox: total mail count, how many are MAIL_AUCTION, how
// many have items, whether the returned-bit is set, and how many match our
// collector's filter. Gated on both debugAuction and auctionRunAnalysis.
void LogMailboxContents(Player* bot)
{
    if (!bot)
        return;
    if (!sPlayerbotAIConfig.debugAuction || !sPlayerbotAIConfig.auctionRunAnalysis)
        return;

    size_t total = 0, auctionType = 0, withItems = 0, returnedBit = 0, deliverable = 0, matched = 0;
    time_t const now = time(nullptr);

    for (PlayerMails::const_iterator it = bot->GetMails().begin(); it != bot->GetMails().end(); ++it)
    {
        Mail* mail = *it;
        if (!mail)
            continue;
        if (mail->state == MAIL_STATE_DELETED)
            continue;

        ++total;
        if (mail->messageType == MAIL_AUCTION)
            ++auctionType;
        if (!mail->items.empty())
            ++withItems;
        if (mail->checked & MAIL_CHECK_MASK_RETURNED)
            ++returnedBit;
        if (mail->deliver_time <= now)
            ++deliverable;

        if (IsReturnedAuctionMail(mail))
            ++matched;
    }

    LOG_ERROR("playerbots",
              "[AH-TRACE] {} mailbox: total={} auctionType={} withItems={} returnedBit={} deliverable={} matched={}",
              bot->GetName(), total, auctionType, withItems, returnedBit, deliverable, matched);
}

}  // namespace PlayerbotAuction
