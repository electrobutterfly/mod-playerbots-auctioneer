/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgAction.h"
#include "AreaDefines.h"
#include "AuctionHouseMgr.h"
#include "AuctionHelpers.h"
#include "AuctionSellAdvisor.h"
#include "AuctionBuyAdvisor.h"
#include "AuctionMailAdvisor.h"
#include "AuctionBidTracker.h"
#include "AuctionBaseline.h"
#include "AuctionCraftAdvisor.h"
#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "DBCStores.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "TravelMgr.h"
#include "WaypointMovementGenerator.h"
#include "G3D/Vector2.h"
#include <cmath>
#include <cstdlib>

void TellRpgStatusAction::WhisperStatusChange(Player* owner, std::string const& statusName)
{
    std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
        RPG_STATUS_CHANGED_KEY, RPG_STATUS_CHANGED_DEFAULT,
        {{"%status", statusName}});
    bot->Whisper(msg, LANG_UNIVERSAL, owner);
}

bool TellRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    if (text.empty())
    {
        std::string out = botAI->rpgInfo.ToString();
        bot->Whisper(out.c_str(), LANG_UNIVERSAL, owner);
        return true;
    }

    Player* master = botAI->GetMaster();
    bool isMaster = master && master->GetGUID() == owner->GetGUID();
    bool isGM = owner->GetSession() && owner->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    if (!isMaster && !isGM)
    {
        std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "rpg_debug_permission_error",
            "Only your master or a GM can change my rpg status.", {});
        bot->Whisper(msg, LANG_UNIVERSAL, owner);
        return false;
    }

    std::string name = text;
    uint32 questId = 0;
    static std::string const doQuestPrefix = "do quest ";
    size_t doQuestPos = text.find(doQuestPrefix);
    if (doQuestPos != std::string::npos)
    {
        name = "do quest";
        std::string idStr = text.substr(doQuestPos + doQuestPrefix.length());
        try
        {
            questId = static_cast<uint32>(std::stoul(idStr));
        }
        catch (std::exception const&)
        {
            questId = 0;
        }
    }

    NewRpgStatus status = NewRpgInfo::StatusFromString(name);
    NewRpgInfo& info = botAI->rpgInfo;

    if (status == RPG_IDLE)
    {
        info.ChangeToIdle();
        WhisperStatusChange(owner, "IDLE");
        return true;
    }
    else if (status == RPG_REST)
    {
        info.ChangeToRest();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        WhisperStatusChange(owner, "REST");
        return true;
    }
    else if (status == RPG_WANDER_RANDOM)
    {
        info.ChangeToWanderRandom();
        WhisperStatusChange(owner, "WANDER_RANDOM");
        return true;
    }
    else if (status == RPG_WANDER_NPC)
    {
        info.ChangeToWanderNpc();
        WhisperStatusChange(owner, "WANDER_NPC");
        return true;
    }
    else if (status == RPG_GO_GRIND)
    {
        WorldPosition pos = SelectRandomGrindPos(bot);
        if (pos == WorldPosition())
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_grind_pos_error", "No grind position available.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToGoGrind(pos);
        WhisperStatusChange(owner, "GO_GRIND");
        return true;
    }
    else if (status == RPG_GO_CAMP)
    {
        WorldPosition pos = SelectRandomCampPos(bot);
        if (pos == WorldPosition())
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_camp_pos_error", "No camp position available.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToGoCamp(pos);
        WhisperStatusChange(owner, "GO_CAMP");
        return true;
        }
    else if (status == RPG_GO_AUCTIONEER)
    {
        TravelMgr::NpcLocation auctioneer;
        if (!sTravelMgr.SelectAuctioneerByMap(bot, auctioneer))
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_auctioneer_error", "No auctioneer available on this map.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        WorldPosition pos(auctioneer.loc.GetMapId(),
                          auctioneer.loc.GetPositionX(),
                          auctioneer.loc.GetPositionY(),
                          auctioneer.loc.GetPositionZ());
        info.ChangeToGoAuctioneer(pos, auctioneer.entry);
        WhisperStatusChange(owner, "GO_AUCTIONEER");
        return true;
    }
    else if (status == RPG_TRAVEL_FLIGHT)
    {
        uint32 flightMasterEntry = 0;
        WorldPosition flightMasterPos;
        std::vector<uint32> path;
        if (!SelectRandomFlightTaxiNode(flightMasterEntry, flightMasterPos, path))
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_flight_path_error", "No flight path available.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToTravelFlight(flightMasterEntry, flightMasterPos, std::move(path));
        WhisperStatusChange(owner, "TRAVEL_FLIGHT");
        return true;
    }
    else if (status == RPG_OUTDOOR_PVP)
    {
        info.ChangeToOutdoorPvp();
        WhisperStatusChange(owner, "OUTDOOR_PVP");
        return true;
    }
    else if (status == RPG_DO_GATHER)
    {
        if (!botAI->HasSkill(SKILL_HERBALISM) && !botAI->HasSkill(SKILL_MINING))
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_gather_no_profession_error", "I have neither Herbalism nor Mining, so I can't gather.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToDoGather();
        WhisperStatusChange(owner, "DO_GATHER");
        return true;
    }
    else if (status == RPG_DO_QUEST)
    {
        if (!questId)
        {
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 qid = bot->GetQuestSlotQuestId(slot);
                if (!qid)
                    continue;
                std::vector<POIInfo> poi;
                if (GetQuestPOIPosAndObjectiveIdx(qid, poi, true))
                {
                    questId = qid;
                    break;
                }
            }
        }
        if (!questId)
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_quest_error", "No quest available; use 'do quest <id>'.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        QuestStatus questStatus = bot->GetQuestStatus(questId);
        if (!quest || (questStatus != QUEST_STATUS_INCOMPLETE && questStatus != QUEST_STATUS_COMPLETE))
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_invalid_quest_error", "Invalid quest %quest_id",
                {{"%quest_id", std::to_string(questId)}});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToDoQuest(questId, quest);
        WhisperStatusChange(owner, "DO_QUEST " + std::to_string(questId));
        return true;
    }

std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "rpg_unknown_status_error",
        "Unknown rpg status. Options: idle, rest, wander random, wander npc, "
        "go grind, go camp, go auctioneer, do quest [<id>], travel flight, outdoor pvp, do gather.", {});
    bot->Whisper(msg, LANG_UNIVERSAL, owner);
    return false;
}

bool StartRpgDoQuestAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    PlayerbotChatHandler ch(owner);
    uint32 questId = ch.extractQuestId(text);
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (quest)
    {
        botAI->rpgInfo.ChangeToDoQuest(questId, quest);
        bot->Whisper("Start to do quest " + std::to_string(questId), LANG_UNIVERSAL, owner);
        return true;
    }
    bot->Whisper("Invalid quest " + text, LANG_UNIVERSAL, owner);
    return false;
}

bool NewRpgStatusUpdateAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    NewRpgStatus status = info.GetStatus();
    switch (status)
    {
        case RPG_IDLE:
            return RandomChangeStatus({RPG_GO_CAMP, RPG_GO_GRIND, RPG_GO_AUCTIONEER, RPG_GO_VENDOR, RPG_WANDER_RANDOM,
                                       RPG_WANDER_NPC, RPG_DO_QUEST, RPG_TRAVEL_FLIGHT, RPG_REST, RPG_OUTDOOR_PVP,
                                       RPG_DO_GATHER});

        case RPG_GO_GRIND:
        {
            auto& data = std::get<NewRpgInfo::GoGrind>(info.data);
            WorldPosition& originalPos = data.pos;
            assert(data.pos != WorldPosition());
            // GO_GRIND -> WANDER_RANDOM
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderRandom();
                return true;
            }
            break;
        }
        case RPG_GO_CAMP:
        {
            auto& data = std::get<NewRpgInfo::GoCamp>(info.data);
            WorldPosition& originalPos = data.pos;
            assert(data.pos != WorldPosition());
            // GO_CAMP -> WANDER_NPC
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            break;
        }
        // Add Vendor
        case RPG_GO_VENDOR:
        {
            auto& data = std::get<NewRpgInfo::GoVendor>(info.data);
            assert(data.pos != WorldPosition());

                        if (data.lastReach && data.vendorSellDone && data.disenchantDone && data.buyContainerDone &&
                data.craftPassDone)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            if (info.HasStatusPersisted(statusGoVendorDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        // Add Auctioneer
        case RPG_GO_AUCTIONEER:
        {
            auto& data = std::get<NewRpgInfo::GoAuctioneer>(info.data);
            assert(data.pos != WorldPosition());

            // Wait for the action to finish: HELLO sent (lastReach set),
            // mail pass done, sell queue built and drained, buy task
            // attempted, and the tracked-bid pass done.
            bool const sellComplete = data.pendingSells.empty();
            bool const buyComplete  = data.buyAuctionId == 0 || data.buyDone;
            bool const bidsComplete = data.trackedBidsDone;
            bool const mailComplete = data.mailChecked;
            bool const queueComplete = data.sellQueueBuilt;
            bool const containerComplete = data.buyContainerDone;
            bool const craftComplete = data.craftPassDone;
            if (data.lastReach && queueComplete && sellComplete && buyComplete && bidsComplete && mailComplete &&
                containerComplete && craftComplete)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            // Give up if the walk takes too long (unreachable destination).
            if (info.HasStatusPersisted(statusGoAuctioneerDuration))
            {
                if (sPlayerbotAIConfig.debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} AH visit TIMEOUT: elapsed={}ms lastReach={} "
                              "queueBuilt={} sellsLeft={} mailDone={} bidsDone={} buyDone={}",
                              bot->GetName(), GetMSTimeDiffToNow(info.startT),
                              data.lastReach,
                              data.sellQueueBuilt ? 1 : 0,
                              data.pendingSells.size(),
                              data.mailChecked ? 1 : 0,
                              data.trackedBidsDone ? 1 : 0,
                              data.buyDone ? 1 : 0);
                }
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        
        case RPG_WANDER_RANDOM:
        {
            // WANDER_RANDOM -> IDLE
            if (info.HasStatusPersisted(statusWanderRandomDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_WANDER_NPC:
        {
            if (info.HasStatusPersisted(statusWanderNpcDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_QUEST:
        {
            // DO_QUEST -> IDLE
            if (info.HasStatusPersisted(statusDoQuestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            auto& data = std::get<NewRpgInfo::TravelFlight>(info.data);
            if (data.inFlight && !bot->IsInFlight())
            {
                // flight arrival
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_REST:
        {
            // REST -> IDLE
            if (info.HasStatusPersisted(statusRestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_OUTDOOR_PVP:
        {
            if (info.HasStatusPersisted(statusOutDoorPvPDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_GATHER:
        {
            // DO_GATHER -> IDLE
            if (info.HasStatusPersisted(statusDoGatherDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool NewRpgGoGrindAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;
    if (auto* data = std::get_if<NewRpgInfo::GoGrind>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos))
            return true;
        // Small nudge so the next tick's MoveFarTo starts from a
        // slightly different position. Kept small so it doesn't look
        // like the bot is abandoning its destination.
        return MoveRandomNear(10.0f);
    }

    return false;
}

bool NewRpgGoCampAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    if (auto* data = std::get_if<NewRpgInfo::GoCamp>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    return false;
}

// Modify for Auctioneer
bool NewRpgGoAuctioneerAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    if (auto* data = std::get_if<NewRpgInfo::GoAuctioneer>(&botAI->rpgInfo.data))
    {
        bool const debugAuction = sPlayerbotAIConfig.debugAuction;
        float const distToPos = bot->GetDistance(data->pos);

        // Far-away bots cannot walk to a city auctioneer inside the visit
        // budget: a bot deep in Tanaris is 4000+ yards from Orgrimmar. Do
        // what a real player does -- take a flight path to the city -- and
        // teleport the bot to the auctioneer's location once. The remaining
        // approach is walked normally so the arrival looks natural.
        //
        // The distance threshold is 500 yards: on foot that is about 70
        // seconds at run speed, comfortably inside the visit budget with
        // margin for the sell queue and mail pass. Bots closer than that
        // walk the whole way.
        constexpr float kWalkDistance = 500.0f;
        if (!data->cityTeleported && distToPos > kWalkDistance)
        {
            data->cityTeleported = true;
            if (debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} teleporting to auctioneer city (dist={:.0f}yd)",
                          bot->GetName(), distToPos);
            }
            bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
            bot->TeleportTo(data->pos.GetMapId(),
                            data->pos.GetPositionX(),
                            data->pos.GetPositionY(),
                            data->pos.GetPositionZ(),
                            data->pos.GetOrientation());
            return false;
        }

        // Mailbox walk sub-state. While set, the auctioneer-approach and
        // HELLO logic is skipped entirely so the bot can actually reach
        // the mailbox. The bot either completes the round trip or gives
        // up; it does not return to normal flow mid-walk.
        if (data->mailboxWalkState == 1)
        {
            ObjectGuid mailbox = PlayerbotAuction::FindMailboxInRange(botAI);
            if (!mailbox.IsEmpty())
            {
                if (debugAuction)
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} relist: mailbox reached, collecting",
                              bot->GetName());

                // Arrived. Collect and queue the returned items for
                // relisting, then commit to the walk back.
                data->mailboxWalkState = 2;

                std::vector<uint32> mailEntries;
                if (PlayerbotAuction::BotHasExpiredAuctionMail(bot, mailEntries))
                {
                    std::vector<uint32> collected =
                        PlayerbotAuction::TakeExpiredAuctionMail(botAI, mailbox);

                    if (!collected.empty())
                    {
                        uint32 const maxRetries = sPlayerbotAIConfig.auctionMaxRelistRetries;
                        bool const isRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
                        uint32 const botLow = bot->GetGUID().GetCounter();

                        std::vector<uint32> relistable;
                        for (uint32 entry : collected)
                        {
                            if (isRandomBot && maxRetries > 0)
                            {
                                std::string const key =
                                    "auction.relist." + std::to_string(entry);
                                uint32 const retries =
                                    sRandomPlayerbotMgr.GetValue(botLow, key);
                                if (retries >= maxRetries)
                                    continue;
                                sRandomPlayerbotMgr.SetValue(botLow, key, retries + 1);
                            }
                            relistable.push_back(entry);
                        }

                        for (uint32 entry : relistable)
                        {
                            if (std::find(data->returnedEntries.begin(),
                                          data->returnedEntries.end(),
                                          entry) == data->returnedEntries.end())
                                data->returnedEntries.push_back(entry);
                        }

                        if (debugAuction)
                            LOG_ERROR("playerbots",
                                      "[AH-TRACE] {} relist: collected {} items, queue deferred to AH arrival",
                                      bot->GetName(), collected.size());
                    }
                }

                return true;
            }

            // Still en route.
            ObjectGuid nearest = PlayerbotAuction::FindNearestMailbox(botAI);
            if (nearest.IsEmpty())
            {
                if (debugAuction)
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} relist: lost sight of mailbox, giving up",
                              bot->GetName());
                data->mailboxWalkState = 0;
                data->mailChecked = true;
                return true;
            }

            if (!bot->isMoving())
            {
                if (!MoveWorldObjectTo(nearest))
                {
                    if (debugAuction)
                        LOG_ERROR("playerbots",
                                  "[AH-TRACE] {} relist: cannot path to mailbox, giving up",
                                  bot->GetName());
                    data->mailboxWalkState = 0;
                    data->mailChecked = true;
                }
            }
            return true;
        }

        if (data->mailboxWalkState == 2)
        {
            if (distToPos < 5.0f)
            {
                data->mailboxWalkState = 0;
                return true;
            }

            if (!bot->isMoving())
            {
                if (!MoveFarTo(data->pos))
                {
                    // Cannot path back. Clear state so the rest of the
                    // visit still gets a chance to run.
                    data->mailboxWalkState = 0;
                }
            }
            return true;
        }

        if (debugAuction && distToPos < 30.0f)
        {
            LOG_DEBUG("playerbots",
                      "[AH-TRACE] {} approaching: distToPos={:.2f} entry={} lastReach={}",
                      bot->GetName(), distToPos, data->auctioneerEntry, data->lastReach);
        }

        if (distToPos > 10.0f)
        {
            if (MoveFarTo(data->pos))
                return true;
            return MoveRandomNear(10.0f);
        }

        // Find the actual creature in the world. The cached NpcLocation
        // only tells us where to walk; we need a live Creature* to send
        // its GUID to the auction house handler.
        Creature* auctioneer = bot->FindNearestCreature(data->auctioneerEntry, INTERACTION_DISTANCE * 3);
        if (!auctioneer)
        {
            LOG_DEBUG("playerbots",
                      "[New RPG] {} reached auctioneer position but cannot find entry {} nearby, aborting",
                      bot->GetName(), data->auctioneerEntry);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }

        if (debugAuction)
        {
            LOG_DEBUG("playerbots",
                      "[AH-TRACE] {} found auctioneer {} (entry {}), distToNPC={:.2f}",
                      bot->GetName(), auctioneer->GetName(), data->auctioneerEntry,
                      bot->GetDistance(auctioneer));
        }

        // INTERACTION_DISTANCE is 5.55 yards; HandleAuctionHelloOpcode validates
        // this. If the bot stopped at the outer 10-yard arrival radius, walk the
        // last few yards before sending.
        if (bot->GetDistance(auctioneer) > INTERACTION_DISTANCE)
        {
            if (MoveFarTo(auctioneer))
                return true;
            return false;
        }

        // Send the handshake once, on first arrival.
        if (!data->lastReach)
        {
            data->lastReach = getMSTime();
            // Reset the status clock to the moment of arrival. The AH visit
            // budget (statusGoAuctioneerDuration) is measured by
            // NewRpgStatusUpdateAction from rpgInfo.startT, which is set when
            // the status is selected -- i.e. before the walk to the auctioneer.
            // Without this reset, a long walk consumes the entire visit budget
            // and the sell pass never runs: the bot reaches the auctioneer,
            // builds its sell queue, and the 5-minute timeout fires before the
            // first sell step. Resetting startT here gives the bot its full
            // visit window from the moment it is actually standing at the
            // auctioneer, which is what the timeout was meant to bound.
            botAI->rpgInfo.startT = getMSTime();

            if (debugAuction)
            {
                LOG_DEBUG("playerbots",
                          "[AH-TRACE] {} sending MSG_AUCTION_HELLO to {} (entry {}), dist={:.2f}",
                          bot->GetName(), auctioneer->GetName(), data->auctioneerEntry,
                          bot->GetDistance(auctioneer));
            }

            WorldPacket packet(MSG_AUCTION_HELLO);
            packet << auctioneer->GetGUID();
            packet.rpos(0);
            bot->GetSession()->HandleAuctionHelloOpcode(packet);

            // Start the per-bot AH cooldown so this bot doesn't return
            // too often. Randomized between VisitMinMinutes and VisitMaxMinutes.
            {
                uint32 const minMin = sPlayerbotAIConfig.auctionVisitMinMinutes;
                uint32 const maxMin = sPlayerbotAIConfig.auctionVisitMaxMinutes;
                uint32 const cooldownMin = urand(minMin, maxMin > minMin ? maxMin : minMin);
                botAI->nextAuctionVisitTime = time(nullptr) + cooldownMin * 60;
            }

            LOG_DEBUG("playerbots",
                      "[New RPG] {} sent MSG_AUCTION_HELLO to {} (entry {}), now standing at AH",
                      bot->GetName(), auctioneer->GetName(), data->auctioneerEntry);

            // Debug-only: read listings directly from the AuctionHouseMgr.
            // Read-only, no session, no packet. Thread-safety note: this runs
            // on a map thread while AuctionHouseMgr::Update() runs on the
            // world thread; the read is expected to be benign but is not
            // strictly safe. Will be replaced by the packet path when we
            // need the client-side view (for bidding).
            if (debugAuction)
            {
                AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
                if (ahObj)
                {
                    uint32 const total = ahObj->Getcount();
                    LOG_DEBUG("playerbots",
                              "[AH-TRACE] {} listing read: {} auctions on this AH",
                              bot->GetName(), total);

                    uint32 logged = 0;
                    for (auto it = ahObj->GetAuctionsBegin();
                         it != ahObj->GetAuctionsEnd() && logged < 5;
                         ++it, ++logged)
                    {
                        AuctionEntry* entry = it->second;
                        if (!entry)
                            continue;
                        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry->item_template);
                        LOG_DEBUG("playerbots",
                                  "[AH-TRACE]   #{} item='{}' (entry {}) x{} startbid={} bid={} buyout={}",
                                  entry->Id,
                                  proto ? proto->Name1 : "<unknown>",
                                  entry->item_template,
                                  entry->itemCount,
                                  entry->startbid,
                                  entry->bid,
                                  entry->buyout);
                    }
                }
                else
                {
                    LOG_DEBUG("playerbots",
                              "[AH-TRACE] {} listing read: no AuctionHouseObject for faction {}",
                              bot->GetName(), auctioneer->GetFaction());
                }
            }
        }
                else if (!data->mailChecked)
        {
            // HELLO sent. Collect any returned-auction mail before doing
            // anything else with the AH, and queue those items to sell.
            data->mailChecked = true;

            // Get MAIL AUCTION returned mail
            std::vector<uint32> mailEntries;
            if (PlayerbotAuction::BotHasExpiredAuctionMail(bot, mailEntries))
            {
                if (debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} relist: {} returned items in mail",
                              bot->GetName(), mailEntries.size());
                }

                ObjectGuid mailbox = PlayerbotAuction::FindMailboxInRange(botAI);
                if (mailbox.IsEmpty())
                {
                    // Not in range. Commit to the mailbox walk if there is
                    // enough bag space to hold the returned items. The
                    // mailboxWalkState sub-state takes over on the next tick
                    // and bypasses the auctioneer-approach and HELLO logic
                    // until the round trip completes or fails.
                    uint32 freeSlots = PlayerbotAuction::CountFreeBagSlots(bot);
                    if (freeSlots < mailEntries.size())
                    {
                        if (debugAuction)
                            LOG_ERROR("playerbots",
                                      "[AH-TRACE] {} relist: bag space insufficient "
                                      "(need {} slots, have {}), skipping this visit",
                                      bot->GetName(), mailEntries.size(), freeSlots);
                        return false;
                    }

                    if (debugAuction)
                        LOG_ERROR("playerbots",
                                  "[AH-TRACE] {} relist: {} returned items, walking to mailbox",
                                  bot->GetName(), mailEntries.size());

                    data->mailboxWalkState = 1;
                    return true;
                }
                else
                {
                    std::vector<uint32> collected = PlayerbotAuction::TakeExpiredAuctionMail(botAI, mailbox);

                    if (!collected.empty())
                    {
                        if (debugAuction)
                        {
                            LOG_ERROR("playerbots",
                                      "[AH-TRACE] {} relist: collected {} items from mailbox",
                                      bot->GetName(), collected.size());
                        }

                                                // Per-bot retry budget. Every entry has a persistent
                        // counter; when it reaches the configured cap, the
                        // returned item is collected but not relisted, so it
                        // falls through to the normal vendor/destroy path.
                        uint32 const maxRetries = sPlayerbotAIConfig.auctionMaxRelistRetries;
                        bool const isRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
                        uint32 const botLow = bot->GetGUID().GetCounter();

                        std::vector<uint32> relistable;
                        for (uint32 entry : collected)
                        {
                            if (isRandomBot && maxRetries > 0)
                            {
                                std::string const key =
                                    "auction.relist." + std::to_string(entry);
                                uint32 const retries = sRandomPlayerbotMgr.GetValue(botLow, key);
                                if (retries >= maxRetries)
                                {
                                    if (retries >= maxRetries)
                                {
                                    if (debugAuction)
                                    {
                                        LOG_ERROR("playerbots",
                                                  "[AH-TRACE] {} relist: entry {} hit max retries ({}/{}), not relisting",
                                                  bot->GetName(), entry, retries, maxRetries);
                                    }

                                    // Add Auctioneer: if the item has no vendor
                                    // value at all, destroy it now rather than
                                    // letting it sit in bags until the next
                                    // bag-pressure destroy pass. SellPrice > 0
                                    // items are left for the vendor path.
                                    ItemTemplate const* exhaustedProto =
                                        sObjectMgr->GetItemTemplate(entry);
                                    if (exhaustedProto && exhaustedProto->SellPrice == 0)
                                    {
                                        Item* exhaustedItem =
                                            PlayerbotAuction::FindTradeableItemInBags(botAI, entry);
                                        if (exhaustedItem)
                                        {
                                            bot->DestroyItem(exhaustedItem->GetBagSlot(),
                                                             exhaustedItem->GetSlot(), true);
                                            if (debugAuction)
                                            {
                                                LOG_ERROR("playerbots",
                                                          "[AH-TRACE] {} relist: entry {} has no vendor value, destroyed",
                                                          bot->GetName(), entry);
                                            }
                                        }
                                    }
                                    continue;
                                }
                                }
                                sRandomPlayerbotMgr.SetValue(botLow, key, retries + 1);
                            }
                            relistable.push_back(entry);
                        }

                        // Record returned entries for relist pricing.
                        for (uint32 entry : relistable)
                        {
                            if (std::find(data->returnedEntries.begin(),
                                          data->returnedEntries.end(),
                                          entry) == data->returnedEntries.end())
                            {
                                data->returnedEntries.push_back(entry);
                            }
                        }

                        // Queue building is deferred to the sellQueueBuilt
                        // step below, which runs after this mail pass and
                        // scans the bot's current bag contents. Returned
                        // items and fresh items are queued by the same code
                        // path and the queue is not stale by the time the
                        // sell step runs.
                    }
                }
            }
        }
                else if (!data->sellQueueBuilt)
        {
            // Build the sell queue now, at AH arrival time, from the bot's
            // current bag contents plus any items just collected from
            // returned mail. This replaces the old pre-visit scan that used
            // to run in RandomChangeStatus; the queue was built minutes
            // before arrival and went stale if the bot looted, fought or
            // traded in between. Returned items are queued first so they use
            // the relist undercut band; fresh items fill the rest of the
            // queue up to MaxSellsPerVisit.
            data->sellQueueBuilt = true;

            uint32 const maxSells = sPlayerbotAIConfig.auctionMaxSellsPerVisit;
            uint32 const listingsCap = sPlayerbotAIConfig.auctionMaxActiveListingsPerBot;
            bool const canSell =
                sPlayerbotAIConfig.auctionEnableSelling &&
                maxSells > 0 &&
                PlayerbotAuction::CountActiveListings(bot) < listingsCap;

            if (canSell)
            {
                // Returned-auction items first.
                for (uint32 entry : data->returnedEntries)
                {
                    if (data->pendingSells.size() >= maxSells)
                        break;

                    Item* it = PlayerbotAuction::FindTradeableItemInBags(botAI, entry);
                    if (!it)
                        continue;

                    ItemTemplate const* p = it->GetTemplate();
                    if (!p)
                        continue;

                    if (sPlayerbotAIConfig.IsAuctionExcludedItem(p->ItemId))
                        continue;
                    if (sPlayerbotAIConfig.auctionExcludeVendorSoldItems &&
                        p->Class != ITEM_CLASS_TRADE_GOODS &&
                        p->Class != ITEM_CLASS_RECIPE &&
                        p->Class != ITEM_CLASS_CONSUMABLE &&
                        sPlayerbotAIConfig.IsVendorSoldItem(p->ItemId))
                        continue;

                    ObjectGuid const guid = it->GetGUID();
                    if (std::find(data->pendingSells.begin(),
                                  data->pendingSells.end(), guid) != data->pendingSells.end())
                        continue;

                    data->pendingSells.push_back(guid);
                }

                // Fresh items. FindItemToSell picks at random from the
                // top-N most valuable candidates, so a loop with GUID
                // de-duplication gets us several distinct stacks without
                // repeating one. A small duplicate-streak cap avoids
                // spinning forever when the candidate pool is smaller
                // than the requested batch.
                uint32 duplicateStreak = 0;
                while (data->pendingSells.size() < maxSells && duplicateStreak < 3)
                {
                    PlayerbotAuction::SellCandidate sellCand =
                        PlayerbotAuction::FindItemToSell(botAI);
                    if (!sellCand.item)
                        break;

                    ObjectGuid const guid = sellCand.item->GetGUID();
                    if (std::find(data->pendingSells.begin(),
                                  data->pendingSells.end(), guid) != data->pendingSells.end())
                    {
                        ++duplicateStreak;
                        continue;
                    }
                    duplicateStreak = 0;

                    // A fresh listing drawn from bags means the bot is not
                    // churning the same expired item. Reset the relist retry
                    // counter for this entry so it starts a new cycle.
                    if (sRandomPlayerbotMgr.IsRandomBot(bot))
                    {
                        std::string const key = "auction.relist." +
                            std::to_string(sellCand.item->GetEntry());
                        sRandomPlayerbotMgr.SetValue(bot->GetGUID().GetCounter(),
                                                      key, 0);
                    }

                    data->pendingSells.push_back(guid);
                }
            }

            if (sPlayerbotAIConfig.debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} sell queue built: {} items ({} returned, {} fresh)",
                          bot->GetName(),
                          data->pendingSells.size(),
                          data->returnedEntries.size(),
                          data->pendingSells.size() > data->returnedEntries.size()
                              ? data->pendingSells.size() - data->returnedEntries.size()
                              : 0);
            }

            return false;
        }
        else if (!data->trackedBidsDone)
        {
            // HELLO sent. Re-evaluate every in-progress bid before rebidding:
            //   * auction still exists in the AH
            //   * we are not the current high bidder
            //   * we still want the item (ItemUsageValue, same source of truth
            //     as AuctionBuyAdvisor::FindAuctionToBuy)
            //   * the next minimum bid still fits under our original cap
            //     and our current gold
            // Drop the bid on any failing condition. Persistence is kept in
            // sync on every drop, so a rebid is never chased with stale data.
            data->trackedBidsDone = true;

            PlayerbotAuction::LoadTrackedBids(botAI);

            if (!botAI->trackedAuctions.empty())
            {
                if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction()))
                {
                    AiObjectContext* ctx = botAI->GetAiObjectContext();

                    for (auto it = botAI->trackedAuctions.begin();
                         it != botAI->trackedAuctions.end(); )
                    {
                        AuctionEntry* tracked = ahObj->GetAuction(it->auctionId);

                        // Auction has expired, sold, or been cancelled.
                        if (!tracked)
                        {
                            PlayerbotAuction::RemoveTrackedBid(botAI, it->auctionId);
                            it = botAI->trackedAuctions.erase(it);
                            continue;
                        }

                        // Still the highest bidder — nothing to do.
                        if (tracked->bidder == bot->GetGUID())
                        {
                            ++it;
                            continue;
                        }

                        // Re-evaluation: does the bot still want this item?
                        // ItemUsageValue runs against the bot's current gear,
                        // inventory, and quest log. A bot that has looted a
                        // better item, vendored the reagents it was hoarding,
                        // or finished the quest this was for will drop the bid
                        // here instead of blindly rebidding a stale maximum.
                        ItemUsage usage = ctx->GetValue<ItemUsage>(
                            "item usage", tracked->item_template)->Get();

                        if (!PlayerbotAuction::IsUsageWanted(usage))
                        {
                            if (sPlayerbotAIConfig.debugAuction)
                            {
                                LOG_ERROR("playerbots",
                                          "[AH-TRACE] {} dropping tracked bid auctionId={} "
                                          "item=[{}] usage={} (no longer wanted)",
                                          bot->GetName(), tracked->Id,
                                          tracked->item_template,
                                          static_cast<uint32>(usage));
                            }
                            PlayerbotAuction::RemoveTrackedBid(botAI, it->auctionId);
                            it = botAI->trackedAuctions.erase(it);
                            continue;
                        }

                        // Still wanted. Compute the next minimum bid.
                        uint32 nextBid = tracked->bid > 0
                            ? tracked->bid + tracked->GetAuctionOutBid()
                            : tracked->startbid;

                        if (nextBid > it->maxWilling || nextBid > bot->GetMoney())
                        {
                            if (sPlayerbotAIConfig.debugAuction)
                            {
                                LOG_ERROR("playerbots",
                                          "[AH-TRACE] {} dropping tracked bid auctionId={} "
                                          "(nextBid={} > max={} or unaffordable)",
                                          bot->GetName(), tracked->Id, nextBid, it->maxWilling);
                            }
                            PlayerbotAuction::RemoveTrackedBid(botAI, it->auctionId);
                            it = botAI->trackedAuctions.erase(it);
                            continue;
                        }

                        PlayerbotAuction::SendPlaceBid(bot, auctioneer, tracked->Id, nextBid);

                        if (sPlayerbotAIConfig.debugAuction)
                        {
                            LOG_ERROR("playerbots",
                                      "[AH-TRACE] {} re-bid auctionId={} price={} (max={})",
                                      bot->GetName(), tracked->Id, nextBid, it->maxWilling);
                        }
                        ++it;
                    }
                }
            }

            return false;
        }
        else if (!data->pendingSells.empty())
        {
            // Pop the next item off the queue and list it. Returned-auction
            // items are at the front of the queue (mail step prepends them),
            // and any item whose entry matches a returned entry uses the
            // relist undercut range.
            ObjectGuid nextGuid = data->pendingSells.front();
            data->pendingSells.erase(data->pendingSells.begin());

            Item* toSell = bot->GetItemByGuid(nextGuid);
            if (!toSell || !toSell->CanBeTraded())
            {
                if (sPlayerbotAIConfig.debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} sell task item gone, skipping",
                              bot->GetName());
                }
                return false;
            }

            // One-off per visit: reserve reagents for profitable crafts.
            // Computed lazily on the first sell of the visit, then reused
            // for every subsequent item in the queue.
            if (!data->protectedReagentsComputed)
            {
                data->protectedReagentIds =
                    PlayerbotAuction::ComputeProtectedReagents(bot);
                data->protectedReagentsComputed = true;
                if (sPlayerbotAIConfig.debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} reserved {} reagents for craft arbitrage",
                              bot->GetName(), data->protectedReagentIds.size());
                }
            }

            // Combined baseline policy: craft reserve, category depth,
            // per-item cap. Mode 1 and missing baseline rows pass through.
            if (!PlayerbotAuction::ShouldListGivenBaselinePolicy(
                    bot, toSell, data->protectedReagentIds))
            {
                if (sPlayerbotAIConfig.debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} skipping [{}] x{}: baseline policy rejected",
                              bot->GetName(), toSell->GetEntry(),
                              toSell->GetCount());
                }
                return false;
            }

            // Add Auctioneer
            // Price is computed at sell time so it reflects the market as it
            // stands right now, not as it stood when the visit began.
            bool const isRelist = std::find(data->returnedEntries.begin(),
                                            data->returnedEntries.end(),
                                            toSell->GetEntry()) != data->returnedEntries.end();

            uint32 bid = 0;
            uint32 buyout = 0;
            PlayerbotAuction::ComputeSellPrice(botAI, toSell, bid, buyout, isRelist);

            // Duration is picked per-listing so a bot with several items
            // queued in the same visit does not stamp all of them with the
            // same expiry. In fixed mode this always returns the configured
            // SellDurationMinutes.
            uint32 const sellDuration = sPlayerbotAIConfig.PickAuctionSellDuration();

            PlayerbotAuction::SendSellItem(bot, auctioneer, toSell,
                                            bid, buyout, sellDuration);

            if (sPlayerbotAIConfig.debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} listed [{}] x{} bid={} buyout={} dur={} relist={}",
                          bot->GetName(), toSell->GetEntry(), toSell->GetCount(),
                          bid, buyout, sellDuration,
                          isRelist ? "yes" : "no");
            }
        }
                else if (data->buyAuctionId != 0 && !data->buyDone)
        {
            PlayerbotAuction::SendPlaceBid(bot, auctioneer, data->buyAuctionId, data->buyPrice);
            data->buyDone = true;

            // Remember this auction so we re-bid if someone outbids us later.
            // item_template and item_count are captured now, while the entry
            // is still in the AH object, so the next visit's re-evaluation
            // has the data it needs to run ItemUsageValue against the bot's
            // current gear/quest state.
            if (sPlayerbotAIConfig.auctionMaxTrackedBids > 0)
            {
                bool alreadyTracked = false;
                for (auto const& ta : botAI->trackedAuctions)
                    if (ta.auctionId == data->buyAuctionId) { alreadyTracked = true; break; }

                if (!alreadyTracked)
                {
                    AuctionEntry* entry = nullptr;
                    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction()))
                        entry = ahObj->GetAuction(data->buyAuctionId);

                    if (entry)
                    {
                        while (botAI->trackedAuctions.size() >= sPlayerbotAIConfig.auctionMaxTrackedBids)
                        {
                            PlayerbotAuction::RemoveTrackedBid(botAI, botAI->trackedAuctions.front().auctionId);
                            botAI->trackedAuctions.erase(botAI->trackedAuctions.begin());
                        }

                        PlayerbotAI::TrackedAuction ta;
                        ta.auctionId      = data->buyAuctionId;
                        ta.itemTemplateId = entry->item_template;
                        ta.itemCount      = entry->itemCount;
                        ta.maxWilling     = data->buyMaxWilling;
                        ta.trackedSince   = time(nullptr);

                        botAI->trackedAuctions.push_back(ta);
                        PlayerbotAuction::UpsertTrackedBid(botAI, ta);
                    }
                }
            }

            if (sPlayerbotAIConfig.debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} bid/bought auctionId={} price={} max={}",
                          bot->GetName(), data->buyAuctionId, data->buyPrice,
                          data->buyMaxWilling);
            }
        }
        else if (!data->craftPassDone)
        {
            // Craft pass. Same reasoning as the vendor flow: use reagents
            // before buying a container the bot could have produced. The
            // bot is standing at an auctioneer with its bags open, which is
            // a natural moment for a real player to top up on crafted goods
            // before listing the rest.
            data->craftPassDone = true;
            botAI->DoSpecificAction("craft random item", Event("rpg action", "ah"), true);
            return false;
        }
        else if (!data->buyContainerDone)
        {
            // Buy-container pass. Runs once per visit, after every other
            // pass has completed. Buyout auctions only; see
            // BuyContainerAction for the reasoning.
            data->buyContainerDone = true;
            botAI->DoSpecificAction("buy container", Event("rpg action", "ah"), true);
            return false;
        }

        return false;
    }

    return false;
}

// Add Vendor
// Walks the bot to the nearest vendor, then on arrival runs two passes:
// a one-shot vendor sell (which handles every item classified
// ITEM_USAGE_VENDOR, i.e. old soulbound gear and cheap loot), followed by
// a disenchant pass for bots with Enchanting. Both passes are idempotent;
// the status update transitions out once lastReach && vendorSellDone &&
// disenchantDone are all true.
bool NewRpgGoVendorAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    if (auto* data = std::get_if<NewRpgInfo::GoVendor>(&botAI->rpgInfo.data))
    {
        bool const debugAuction = sPlayerbotAIConfig.debugAuction;
        float const distToPos = bot->GetDistance(data->pos);

        // Same teleport-if-far as the AH visit. See the comment there.
        constexpr float kWalkDistance = 500.0f;
        if (!data->cityTeleported && distToPos > kWalkDistance)
        {
            data->cityTeleported = true;
            if (debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} teleporting to vendor city (dist={:.0f}yd)",
                          bot->GetName(), distToPos);
            }
            bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
            bot->TeleportTo(data->pos.GetMapId(),
                            data->pos.GetPositionX(),
                            data->pos.GetPositionY(),
                            data->pos.GetPositionZ(),
                            data->pos.GetOrientation());
            return false;
        }

        if (debugAuction && distToPos < 30.0f)
        {
            LOG_DEBUG("playerbots",
                      "[AH-TRACE] {} approaching vendor: distToPos={:.2f} entry={} lastReach={}",
                      bot->GetName(), distToPos, data->vendorEntry, data->lastReach);
        }

        if (distToPos > 10.0f)
        {
            if (MoveFarTo(data->pos))
                return true;
            return MoveRandomNear(10.0f);
        }

        Creature* vendor = bot->FindNearestCreature(data->vendorEntry, INTERACTION_DISTANCE * 6);
        if (!vendor)
        {
            // The cached vendor entry may not be visible from where the bot
            // stopped -- interiors, staircases, and different floors of the
            // same building are common cases. Fall back to any vendor within
            // interaction range: the exact entry is a preference, not a
            // requirement, and the sell pass works with any vendor.
            GuidVector const& npcs =
                botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
            for (ObjectGuid const& guid : npcs)
            {
                Creature* c = botAI->GetCreature(guid);
                if (!c || !c->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
                    continue;
                if (bot->GetDistance(c) > INTERACTION_DISTANCE)
                    continue;
                vendor = c;
                break;
            }
        }
        if (!vendor)
        {
            LOG_DEBUG("playerbots",
                      "[AH-TRACE] {} reached vendor position but cannot find entry {} nearby, aborting",
                      bot->GetName(), data->vendorEntry);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }

        if (debugAuction)
        {
            LOG_DEBUG("playerbots",
                      "[AH-TRACE] {} found vendor {} (entry {}), distToNPC={:.2f}",
                      bot->GetName(), vendor->GetName(), data->vendorEntry,
                      bot->GetDistance(vendor));
        }

        // INTERACTION_DISTANCE is 5.55 yards. If the outer 10-yard arrival
        // radius caught us short, walk the last few yards.
        if (bot->GetDistance(vendor) > INTERACTION_DISTANCE)
        {
            if (MoveFarTo(vendor))
                return true;
            return false;
        }

        // First-tick-on-arrival initialisation.
        if (!data->lastReach)
        {
            data->lastReach = getMSTime();
            // Same status-clock reset as the AH visit. See the comment there.
            botAI->rpgInfo.startT = getMSTime();

            // Per-bot cooldown, so the bot doesn't immediately re-qualify
            // for another vendor visit once this one ends.
            {
                uint32 const minMin = sPlayerbotAIConfig.vendorVisitMinMinutes;
                uint32 const maxMin = sPlayerbotAIConfig.vendorVisitMaxMinutes;
                uint32 const cooldownMin = urand(minMin, maxMin > minMin ? maxMin : minMin);
                botAI->nextVendorVisitTime = time(nullptr) + cooldownMin * 60;
            }

            if (debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} arrived at vendor {} (entry {}), starting sell pass",
                          bot->GetName(), vendor->GetName(), data->vendorEntry);
            }
        }

        // Vendor sell pass. Runs once per visit. SellVendorItemsVisitor
        // filters to ITEM_USAGE_VENDOR and skips ITEM_USAGE_AH items, so
        // anything that should stay on the auction house is left alone.
        if (!data->vendorSellDone)
        {
            data->vendorSellDone = true;
            bool const sold = botAI->DoSpecificAction("sell", Event("rpg action", "vendor"), true);
            if (debugAuction)
            {
                LOG_ERROR("playerbots",
                          "[AH-TRACE] {} vendor sell pass complete (sold anything={})",
                          bot->GetName(), sold ? "yes" : "no");
            }
            return false;
        }

        // Disenchant pass. Only for bots with Enchanting. One cast per tick,
        // until DisEnchantRandomItemAction returns false (nothing left to
        // disenchant) or the safety cap is hit.
        if (!data->disenchantDone)
        {
            if (!botAI->HasSkill(SKILL_ENCHANTING))
            {
                data->disenchantDone = true;
                return false;
            }

            constexpr uint32 kMaxDisenchantAttempts = 30;
            if (data->disenchantAttempts >= kMaxDisenchantAttempts)
            {
                if (debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} disenchant pass hit cap ({} attempts, {} successes), stopping",
                              bot->GetName(), data->disenchantAttempts, data->disenchantSuccesses);
                }
                data->disenchantDone = true;
                return false;
            }

            ++data->disenchantAttempts;
            bool const ok = botAI->DoSpecificAction("disenchant random item", Event(), true);
            if (!ok)
            {
                data->disenchantDone = true;
                if (debugAuction)
                {
                    LOG_ERROR("playerbots",
                              "[AH-TRACE] {} disenchant pass complete ({} successes)",
                              bot->GetName(), data->disenchantSuccesses);
                }
                return false;
            }

            ++data->disenchantSuccesses;
            // Let the cast finish before trying the next one.
            botAI->SetNextCheckDelay(sPlayerbotAIConfig.ReactDelay * 10);
            return false;
        }

        // Craft pass. Runs before buy-container so the bot spends reagents it
        // is already carrying before spending gold on a bag it could have
        // produced itself. The action classifies bag-like and useful-item
        // crafts at the same tier, so a bot that can make bags and also
        // produces potions or enchants picks among them uniformly. Bots with
        // no craftable spells or no reagents skip the pass silently.
        if (!data->craftPassDone)
        {
            data->craftPassDone = true;
            botAI->DoSpecificAction("craft random item", Event("rpg action", "vendor"), true);
            return false;
        }

        // Buy-container pass. Runs once per visit, after sell and disenchant
        // so the bot has the most money available for the purchase. Standard
        // bags, quivers and ammo pouches are all eligible; see
        // BuyContainerAction for the details.
        if (!data->buyContainerDone)
        {
            data->buyContainerDone = true;
            botAI->DoSpecificAction("buy container", Event("rpg action", "vendor"), true);
            return false;
        }

        // All passes are done. The status update will transition us out on
        // the next tick.
        return false;
    }

    return false;
}

bool NewRpgWanderRandomAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveRandomNear();
}

bool NewRpgWanderNpcAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::WanderNpc>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    if (!data.npcOrGo)
    {
        // No npc can be found, switch to IDLE
        ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract();
        if (npcOrGo.IsEmpty())
        {
            info.ChangeToIdle();
            return true;
        }
        data.npcOrGo = npcOrGo;
        data.lastReach = 0;
        return true;
    }

    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, data.npcOrGo);
    if (object && IsWithinInteractionDist(object))
    {
        if (!data.lastReach)
        {
            data.lastReach = getMSTime();
            if (bot->CanInteractWithQuestGiver(object))
                InteractWithNpcOrGameObjectForQuest(data.npcOrGo);
            return true;
        }

        if (data.lastReach && GetMSTimeDiffToNow(data.lastReach) < npcStayTime)
            return false;

        // has reached the npc for more than `npcStayTime`, select the next target
        data.npcOrGo = ObjectGuid();
        data.lastReach = 0;
    }
    else
    {
        if (MoveWorldObjectTo(data.npcOrGo))
            return true;
        // NPC pathing failed (random offset in a wall, mmap hiccup, etc).
        // Take a small random step so the next tick retries from a
        // different spot instead of staring at the NPC from afar.
        return MoveRandomNear(15.0f);
    }

    return true;
}

bool NewRpgDoQuestAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    uint32 questId = data.questId;
    uint8 questStatus = bot->GetQuestStatus(questId);
    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest(data);
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest(data);
        default:
            break;
    }
    info.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    if (data.pos != WorldPosition())
    {
        /// @TODO: extract to a new function
        int32 currentObjective = data.objectiveIdx;
        // check if the objective has completed
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        QuestStatusData const& q_status = bot->getQuestStatusMap().at(questId);
        bool completed = true;
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] < quest->RequiredNpcOrGoCount[currentObjective])
                completed = false;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] <
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                completed = false;
        }
        // the current objective is completed, clear and find a new objective later
        if (completed)
        {
            data.lastReachPOI = 0;
            data.pos = WorldPosition();
            data.objectiveIdx = 0;
        }
    }
    if (data.pos == WorldPosition())
    {
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            // can't find a poi pos to go, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        uint32 rndIdx = urand(0, poiInfo.size() - 1);
        G3D::Vector2 nearestPoi = poiInfo[rndIdx].pos;
        int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;

        float dx = nearestPoi.x, dy = nearestPoi.y;

        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        data.lastReachPOI = 0;
        data.pos = pos;
        data.objectiveIdx = objectiveIdx;
    }

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos))
            return true;
        // Long-range sampler couldn't land a candidate — nudge the
        // bot a short distance so the next tick retries from a
        // different position instead of sitting idle.
        return MoveRandomNear(10.0f);
    }
    // Now we are near the quest objective
    // kill mobs and looting quest should be done automatically by grind strategy

    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        bool hasProgression = false;
        int32 currentObjective = data.objectiveIdx;
        // check if the objective has progression
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        QuestStatusData const& q_status = bot->getQuestStatusMap().at(questId);
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] != 0 && quest->RequiredNpcOrGoCount[currentObjective])
                hasProgression = true;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] != 0 &&
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                hasProgression = true;
        }
        if (!hasProgression)
        {
            // we has reach the poi for more than 5 mins but no progession
            // may not be able to complete this quest, marked as abandoned
            /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        // clear and select another poi later
        data.lastReachPOI = 0;
        data.pos = WorldPosition();
        data.objectiveIdx = 0;
        return true;
    }

    // At the POI: keep the bot actively placed but avoid large
    // random 20yd hops that look like pacing back and forth. A small
    // ~8yd wander reads as the bot looking around while grind/loot
    // strategies do their work.
    return MoveRandomNear(8.0f);
}

bool NewRpgDoQuestAction::DoCompletedQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    Quest const* quest = data.quest;

    if (data.objectiveIdx != -1)
    {
        // if quest is completed, back to poi with -1 idx to reward
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
        botAI->rpgStatistic.questCompleted++;
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
        {
            // can't find a poi pos to reward, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }
        assert(poiInfo.size() > 0);
        // now we get the place to get rewarded
        float dx = poiInfo[0].pos.x, dy = poiInfo[0].pos.y;
        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        data.lastReachPOI = 0;
        data.pos = pos;
        data.objectiveIdx = -1;
    }

    if (data.pos == WorldPosition())
        return false;

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    // Now we are near the qoi of reward
    // the quest should be rewarded by SearchQuestGiverAndAcceptOrReward
    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}

bool NewRpgTravelFlightAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::TravelFlight>(&info.data);
    if (!dataPtr)
        return false;

    auto& data = *dataPtr;
    if (bot->IsInFlight())
    {
        data.inFlight = true;
        ContinueCrossMapTaxi();
        return false;
    }

    if (bot->GetDistance(data.flightMasterPos) > INTERACTION_DISTANCE)
        return MoveFarTo(data.flightMasterPos);

    Creature* flightMaster = bot->FindNearestCreature(data.flightMasterEntry, INTERACTION_DISTANCE * 3);
    if (!flightMaster || !flightMaster->IsAlive())
    {
        info.ChangeToIdle();
        return true;
    }
    if (bot->GetDistance(flightMaster) > INTERACTION_DISTANCE)
        return MoveFarTo(flightMaster);

    std::vector<uint32> nodes = data.path;

    botAI->RemoveShapeshift();
    if (bot->IsMounted())
        bot->Dismount();

    bot->GetSession()->SendLearnNewTaxiNode(flightMaster);

    if (!bot->ActivateTaxiPathTo(nodes, flightMaster, 0))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} active taxi path {} (from {} to {}) failed", bot->GetName(),
                  flightMaster->GetEntry(), nodes.empty() ? 0 : nodes.front(), nodes.empty() ? 0 : nodes.back());
        info.ChangeToIdle();
        return true;
    }
    return true;
}

void NewRpgTravelFlightAction::ContinueCrossMapTaxi()
{
    if (bot->IsBeingTeleported())
        return;

    if (!bot->movespline->Finalized())
        return;

    MotionMaster* mm = bot->GetMotionMaster();
    if (!mm || mm->GetCurrentMovementGeneratorType() != FLIGHT_MOTION_TYPE)
        return;

    // Check if we are at our destination.
    uint32 nextDest = bot->m_taxi.GetTaxiDestination();
    if (!nextDest)
        return;

    // Confirm next node needs different map.
    TaxiNodesEntry const* nextNode = sTaxiNodesStore.LookupEntry(nextDest);
    if (!nextNode || nextNode->map_id == bot->GetMapId())
        return;

    FlightPathMovementGenerator* flight = dynamic_cast<FlightPathMovementGenerator*>(mm->top());
    if (!flight)
        return;

    LOG_DEBUG("playerbots", "[New RPG] {} continuing taxi across map boundary (next node {} on map {})",
              bot->GetName(), nextDest, nextNode->map_id);

    flight->SetCurrentNodeAfterTeleport();

    if (flight->HasArrived())
        return;

    TaxiPathNodeEntry const* node = flight->GetPath()[flight->GetCurrentNode()];
    flight->SkipCurrentNode();

    bot->TeleportTo(nextNode->map_id, node->x, node->y, node->z, bot->GetOrientation(), TELE_TO_NOT_LEAVE_TAXI);
}

bool AhAction::Execute(Event event)
{
    LOG_ERROR("playerbots", "[AH-DIAG] AhAction::Execute fired, param='{}'", event.getParam());

    Player* owner = event.getOwner();
    if (!owner)
    {
        LOG_ERROR("playerbots", "[AH-DIAG] no owner on event, aborting");
        return false;
    }

        auto reply = [&](std::string const& msg) {
        bot->Whisper(msg, LANG_UNIVERSAL, owner);
        LOG_ERROR("playerbots", "[AH-DIAG] reply to {}: {}", owner->GetName(), msg);
    };

    if (!sPlayerbotAIConfig.allowPlayerAuctionCommands)
    {
        reply("Auction commands are disabled on this server.");
        return false;
    }

    // Auction commands are GM-only. This is a second, independent gate on
    // top of allowPlayerAuctionCommands: the config key controls whether
    // GMs may use them at all; this check enforces that only a GM-level
    // session can actually reach them. A regular player who somehow gets
    // through (whisper, party message, chat shortcut) is rejected and the
    // attempt is logged so abuse is visible in the log.
    if (!owner->GetSession() ||
        owner->GetSession()->GetSecurity() < SEC_GAMEMASTER)
    {
        LOG_WARN("playerbots",
                 "[AH-TRACE] Non-GM {} (security {}) attempted AH command",
                 owner->GetName(),
                 owner->GetSession() ? owner->GetSession()->GetSecurity() : 0);
        reply("Auction commands are restricted to Game Masters.");
        return false;
    }

    std::string const text = event.getParam();
    if (text.empty())
    {
        reply("Usage: ah bid <auctionId> <priceCopper>");
        return false;
    }

    std::istringstream iss(text);
    std::string subcommand;
    iss >> subcommand;

    if (subcommand == "bid")
    {
        uint32 auctionId = 0;
        uint32 price = 0;
        if (!(iss >> auctionId >> price) || !auctionId || !price)
        {
            reply("Usage: ah bid <auctionId> <priceCopper>");
            return false;
        }

        // Find any auctioneer within interaction range.
        Creature* auctioneer = PlayerbotAuction::FindAuctioneerInRange(botAI);
        if (!auctioneer)
        {
            reply("No auctioneer within interaction range. Walk to one first.");
            return false;
        }

        // -- diagnostic: check every precondition the handler will check --
        Creature* verify = bot->GetNPCIfCanInteractWith(auctioneer->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER);
        LOG_ERROR("playerbots", "[AH-DIAG] GetNPCIfCanInteractWith: {}",
                  verify ? "OK" : "NULL");
        LOG_ERROR("playerbots", "[AH-DIAG] bot money={} needed={} alive={} inWorld={}",
                  bot->GetMoney(), price,
                  bot->IsAlive() ? "yes" : "no",
                  bot->IsInWorld() ? "yes" : "no");

        AuctionHouseEntry const* ahEntry =
            AuctionHouseMgr::GetAuctionHouseEntryFromFactionTemplate(auctioneer->GetFaction());
        if (!ahEntry)
        {
            LOG_ERROR("playerbots", "[AH-DIAG] no AuctionHouseEntry for faction {}",
                      auctioneer->GetFaction());
        }
        else
        {
            AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
            if (!ahObj)
            {
                LOG_ERROR("playerbots", "[AH-DIAG] no AuctionHouseObject");
            }
            else
            {
                AuctionEntry* auction = ahObj->GetAuction(auctionId);
                if (!auction)
                {
                    LOG_ERROR("playerbots", "[AH-DIAG] auction {} NOT FOUND", auctionId);
                }
                else
                {
                    LOG_ERROR("playerbots",
                              "[AH-DIAG] auction {} found: owner={} startbid={} bid={} buyout={}",
                              auctionId, auction->owner.ToString(),
                              auction->startbid, auction->bid, auction->buyout);
                    LOG_ERROR("playerbots", "[AH-DIAG] owner is bot? {}",
                              auction->owner == bot->GetGUID() ? "YES (this blocks the bid)" : "no");
                }
            }
        }
        // -- end diagnostic --
        
        // Reject bids that aren't strictly greater than the current bid.
        // The server-side handler silently drops these; catching it here
        // gives a useful reply instead of silence.
        if (ahEntry)
        {
            if (AuctionHouseObject* guardObj = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction()))
            {
                if (AuctionEntry* guardAuction = guardObj->GetAuction(auctionId))
                {
                    if (price <= guardAuction->bid || price < guardAuction->startbid)
                    {
                        std::ostringstream why;
                        why << "Bid rejected: must exceed current bid (" << guardAuction->bid
                            << ") and meet startbid (" << guardAuction->startbid << ").";
                        reply(why.str());
                        return false;
                    }
                }
            }
        }

        PlayerbotAuction::SendPlaceBid(bot, auctioneer, auctionId, price);

        std::ostringstream out;
        out << "Sent bid: auctionId=" << auctionId
            << " price=" << price << " copper to " << auctioneer->GetName();
        reply(out.str());

        if (sPlayerbotAIConfig.debugAuction)
        {
            LOG_ERROR("playerbots",
                      "[AH-TRACE] {} sent bid auctionId={} price={} to {} (entry {})",
                      bot->GetName(), auctionId, price,
                      auctioneer->GetName(), auctioneer->GetEntry());
        }

        return true;
    }

            if (subcommand == "buypick")
    {
        auto candidate = PlayerbotAuction::FindAuctionToBuy(botAI);
        if (!candidate.auction)
        {
            reply("No auction worth bidding on.");
            return false;
        }
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(candidate.auction->item_template);
        std::ostringstream out;
        ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", candidate.auction->item_template);

        out << "Would buy: auctionId=" << candidate.auction->Id
            << " item=[" << candidate.auction->item_template << "] "
            << " usage=" << static_cast<uint32>(usage)
            << " " << (proto ? proto->Name1 : "<unknown>")
            << " x" << candidate.auction->itemCount
            << " seller=" << static_cast<int>(candidate.seller)
            << (candidate.doBuyout ? " action=buyout price=" : " action=bid price=")
            << (candidate.doBuyout ? candidate.buyoutAmount : candidate.bidAmount)
            << " maxWilling=" << candidate.maxWillingPrice
            << " copper";
        reply(out.str());
        return true;
    }

    if (subcommand == "sellpick")
    {
        auto candidate = PlayerbotAuction::FindItemToSell(botAI);
        if (!candidate.item)
        {
            reply("No item worth selling.");
            return false;
        }
        std::ostringstream out;
        out << "Would sell: [" << candidate.item->GetEntry() << "] "
            << candidate.item->GetTemplate()->Name1
            << " x" << candidate.item->GetCount()
            << " for bid=" << candidate.suggestedBid
            << " buyout=" << candidate.suggestedBuyout
            << " copper";
        reply(out.str());
        return true;
    }

    if (subcommand == "sell")
    {
        uint32 itemId = 0;
        uint32 bid = 0;
        uint32 buyout = 0;
        uint32 duration = 720;

        if (!(iss >> itemId >> bid >> buyout) || !itemId || !bid)
        {
            reply("Usage: ah sell <itemId> <bidCopper> <buyoutCopper> [durationMinutes]");
            return false;
        }

        if (!(iss >> duration))
            duration = 720;
        if (duration != 720 && duration != 1440 && duration != 2880)
        {
            reply("Duration must be 720 (12h), 1440 (24h), or 2880 (48h).");
            return false;
        }

        if (buyout != 0 && buyout < bid)
        {
            reply("Buyout must be 0 (no buyout) or at least the bid.");
            return false;
        }

                Creature* auctioneer = PlayerbotAuction::FindAuctioneerInRange(botAI);
        if (!auctioneer)
        {
            reply("No auctioneer within interaction range. Walk to one first.");
            return false;
        }

        Item* target = PlayerbotAuction::FindTradeableItemInBags(botAI, itemId);
        if (!target)
        {
            std::ostringstream why;
            why << "No tradeable item with entry " << itemId << " in my bags.";
            reply(why.str());
            return false;
        }

        {
            Creature* verify = bot->GetNPCIfCanInteractWith(auctioneer->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER);
            LOG_ERROR("playerbots", "[AH-DIAG] sell GetNPCIfCanInteractWith: {}",
                      verify ? "OK" : "NULL");
            LOG_ERROR("playerbots",
                      "[AH-DIAG] sell item entry={} count={} tradable={} bag={} slot={}",
                      target->GetEntry(), target->GetCount(),
                      target->CanBeTraded() ? "yes" : "no",
                      target->GetBagSlot(), target->GetSlot());
            AuctionHouseEntry const* ahE =
                AuctionHouseMgr::GetAuctionHouseEntryFromFactionTemplate(auctioneer->GetFaction());
            if (!ahE)
                LOG_ERROR("playerbots", "[AH-DIAG] sell no AuctionHouseEntry");
            else
                LOG_ERROR("playerbots", "[AH-DIAG] sell auctionHouseEntry OK houseId={}",
                          uint32(ahE->houseId));
        }

        PlayerbotAuction::SendSellItem(bot, auctioneer, target, bid, buyout, duration);

        std::ostringstream out;
        out << "Sent sell: item=" << itemId
            << " x" << target->GetCount()
            << " bid=" << bid
            << " buyout=" << buyout
            << " duration=" << duration
            << " via " << auctioneer->GetName();
        reply(out.str());

        if (sPlayerbotAIConfig.debugAuction)
        {
            LOG_ERROR("playerbots",
                      "[AH-TRACE] {} sent sell item={} x{} bid={} buyout={} dur={} to {} (entry {})",
                      bot->GetName(), itemId, target->GetCount(), bid, buyout, duration,
                      auctioneer->GetName(), auctioneer->GetEntry());
        }

        return true;
    }

    reply("Unknown ah subcommand. Usage: ah bid <auctionId> <priceCopper> | ah sell <itemId> <bidCopper> <buyoutCopper> [durationMinutes]");
    return false;
}
