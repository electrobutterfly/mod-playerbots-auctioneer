/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgAction.h"
#include "AreaDefines.h"
#include "AuctionHouseMgr.h"
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
            return RandomChangeStatus({RPG_GO_CAMP, RPG_GO_GRIND, RPG_GO_AUCTIONEER, RPG_WANDER_RANDOM, RPG_WANDER_NPC,
                                       RPG_DO_QUEST, RPG_TRAVEL_FLIGHT, RPG_REST, RPG_OUTDOOR_PVP, RPG_DO_GATHER});

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
        // Add Auctioneer
                case RPG_GO_AUCTIONEER:
        {
            auto& data = std::get<NewRpgInfo::GoAuctioneer>(info.data);
            assert(data.pos != WorldPosition());

            // Wait for the action to signal it has finished (it sets
            // lastReach after sending MSG_AUCTION_HELLO). If we
            // transition on distance alone, we kill the action before
            // it can do its job.
            if (data.lastReach)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            // Give up if the walk takes too long (unreachable destination).
            if (info.HasStatusPersisted(statusGoAuctioneerDuration))
            {
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
        Creature* auctioneer = nullptr;
        GuidVector const npcs = AI_VALUE(GuidVector, "nearest npcs");
        for (ObjectGuid const& guid : npcs)
        {
            Creature* creature = botAI->GetCreature(guid);
            if (!creature)
                continue;
            if (!creature->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER))
                continue;
            if (bot->GetDistance(creature) > INTERACTION_DISTANCE)
                continue;
            auctioneer = creature;
            break;
        }

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

        WorldPacket packet(CMSG_AUCTION_PLACE_BID);
        packet << auctioneer->GetGUID();
        packet << uint32(auctionId);
        packet << uint32(price);
        packet.rpos(0);
        bot->GetSession()->HandleAuctionPlaceBid(packet);

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

        Creature* auctioneer = nullptr;
        GuidVector const npcs = AI_VALUE(GuidVector, "nearest npcs");
        for (ObjectGuid const& guid : npcs)
        {
            Creature* creature = botAI->GetCreature(guid);
            if (!creature)
                continue;
            if (!creature->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER))
                continue;
            if (bot->GetDistance(creature) > INTERACTION_DISTANCE)
                continue;
            auctioneer = creature;
            break;
        }

        if (!auctioneer)
        {
            reply("No auctioneer within interaction range. Walk to one first.");
            return false;
        }

        Item* target = nullptr;
        for (Item* item : botAI->GetInventoryItems())
        {
            if (!item)
                continue;
            if (item->GetEntry() != itemId)
                continue;
            if (!item->CanBeTraded())
                continue;
            target = item;
            break;
        }

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

        WorldPacket packet(CMSG_AUCTION_SELL_ITEM);
        packet << auctioneer->GetGUID();
        packet << uint32(1);
        packet << target->GetGUID();
        packet << uint32(target->GetCount());
        packet << uint32(bid);
        packet << uint32(buyout);
        packet << uint32(duration);
        packet.rpos(0);
        bot->GetSession()->HandleAuctionSellItem(packet);

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
