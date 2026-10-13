/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgInfo.h"
#include "Timer.h"
#include <cmath>

void NewRpgInfo::ChangeToGoGrind(WorldPosition pos)
{
    startT = getMSTime();
    data = GoGrind{pos};
}

void NewRpgInfo::ChangeToGoCamp(WorldPosition pos)
{
    startT = getMSTime();
    data = GoCamp{pos};
}

// Add Auctioneer
void NewRpgInfo::ChangeToGoAuctioneer(WorldPosition pos, uint32 auctioneerEntry,
                                      std::vector<ObjectGuid> const& pendingSells,
                                      uint32 buyAuctionId,
                                      uint32 buyPrice,
                                      uint32 buyMaxWilling)
{
    startT = getMSTime();
    GoAuctioneer data_go;
    data_go.pos = pos;
    data_go.auctioneerEntry = auctioneerEntry;
    data_go.lastReach = 0;
    data_go.mailChecked = false;
    data_go.sellQueueBuilt = false;
    data_go.cityTeleported = false;
    data_go.pendingSells = pendingSells;
    data_go.returnedEntries.clear();
    data_go.buyAuctionId = buyAuctionId;
    data_go.buyPrice = buyPrice;
    data_go.buyMaxWilling = buyMaxWilling;
    data_go.buyDone = false;
    data_go.trackedBidsDone = false;
    data = data_go;
}

void NewRpgInfo::ChangeToGoVendor(WorldPosition pos, uint32 vendorEntry)
{
    startT = getMSTime();
    GoVendor data_go;
    data_go.pos = pos;
    data_go.vendorEntry = vendorEntry;
    data_go.lastReach = 0;
    data_go.vendorSellDone = false;
    data_go.disenchantAttempts = 0;
    data_go.disenchantSuccesses = 0;
    data_go.disenchantDone = false;
    data_go.cityTeleported = false;
    data = data_go;
}

void NewRpgInfo::ChangeToWanderNpc()
{
    startT = getMSTime();
    data = WanderNpc{};
}

void NewRpgInfo::ChangeToWanderRandom()
{
    startT = getMSTime();
    data = WanderRandom{};
}

void NewRpgInfo::ChangeToDoQuest(uint32 questId, Quest const* quest)
{
    startT = getMSTime();
    DoQuest do_quest;
    do_quest.questId = questId;
    do_quest.quest = quest;
    data = do_quest;
}

void NewRpgInfo::ChangeToTravelFlight(uint32 flightMasterEntry, WorldPosition flightMasterPos, std::vector<uint32> path)
{
    startT = getMSTime();
    TravelFlight flight;
    flight.flightMasterEntry = flightMasterEntry;
    flight.flightMasterPos = flightMasterPos;
    flight.path = std::move(path);
    flight.inFlight = false;
    data = flight;
}

void NewRpgInfo::ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId)
{
    startT = getMSTime();
    OutdoorPvP pvp;
    pvp.capturePointSpawnId = capturePointSpawnId;
    data = pvp;
}

void NewRpgInfo::ChangeToDoGather()
{
    startT = getMSTime();
    data = DoGather{};
}

void NewRpgInfo::ChangeToRest()
{
    startT = getMSTime();
    data = Rest{};
}

void NewRpgInfo::ChangeToIdle()
{
    startT = getMSTime();
    data = Idle{};
}

bool NewRpgInfo::CanChangeTo(NewRpgStatus)
{
    return true;
}

void NewRpgInfo::Reset()
{
    data = Idle{};
    startT = getMSTime();
}

void NewRpgInfo::SetMoveFarTo(WorldPosition pos)
{
    nearestMoveFarDis = FLT_MAX;
    stuckTs = 0;
    stuckAttempts = 0;
    moveFarPos = pos;
}

NewRpgStatus NewRpgInfo::StatusFromString(std::string const& name)
{
    if (name == "idle")           return RPG_IDLE;
    if (name == "rest")           return RPG_REST;
    if (name == "wander random")  return RPG_WANDER_RANDOM;
    if (name == "wander npc")     return RPG_WANDER_NPC;
    if (name == "go grind")       return RPG_GO_GRIND;
    if (name == "go camp")        return RPG_GO_CAMP;
    // Add Auctioneer
    if (name == "go auctioneer")  return RPG_GO_AUCTIONEER;
    // Add Vendor
    if (name == "go vendor")      return RPG_GO_VENDOR;
    if (name == "do quest")       return RPG_DO_QUEST;
    if (name == "travel flight")  return RPG_TRAVEL_FLIGHT;
    if (name == "outdoor pvp")    return RPG_OUTDOOR_PVP;
    if (name == "do gather")      return RPG_DO_GATHER;
    return RPG_STATUS_END;
}

NewRpgStatus NewRpgInfo::GetStatus()
{
    return std::visit([](auto&& arg) -> NewRpgStatus {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Idle>) return RPG_IDLE;
        if constexpr (std::is_same_v<T, GoGrind>) return RPG_GO_GRIND;
        if constexpr (std::is_same_v<T, GoCamp>) return RPG_GO_CAMP;
        // Add Auctioneer
        if constexpr (std::is_same_v<T, GoAuctioneer>) return RPG_GO_AUCTIONEER;
        // Add Vendor
        if constexpr (std::is_same_v<T, GoVendor>) return RPG_GO_VENDOR;
        if constexpr (std::is_same_v<T, WanderNpc>) return RPG_WANDER_NPC;
        if constexpr (std::is_same_v<T, WanderRandom>) return RPG_WANDER_RANDOM;
        if constexpr (std::is_same_v<T, Rest>) return RPG_REST;
        if constexpr (std::is_same_v<T, DoQuest>) return RPG_DO_QUEST;
        if constexpr (std::is_same_v<T, TravelFlight>) return RPG_TRAVEL_FLIGHT;
        if constexpr (std::is_same_v<T, OutdoorPvP>) return RPG_OUTDOOR_PVP;
        if constexpr (std::is_same_v<T, DoGather>) return RPG_DO_GATHER;
        return RPG_IDLE;
    }, data);
}

std::string NewRpgInfo::ToString()
{
    std::stringstream out;
    out << "Status: ";
    std::visit([&out, this](auto&& arg)
    {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, GoGrind>)
        {
            out << "GO_GRIND";
            out << "\nGrindPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastGoGrind: " << startT;
        }
        else if constexpr (std::is_same_v<T, GoCamp>)
        {
            out << "GO_CAMP";
            out << "\nCampPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastGoCamp: " << startT;
        }
        // Add Auctioneer
        else if constexpr (std::is_same_v<T, GoAuctioneer>)
        {
            out << "GO_AUCTIONEER";
            out << "\nAuctioneerPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nAuctioneerEntry: " << arg.auctioneerEntry;
            out << "\nlastReach: " << arg.lastReach;
        }
        // Add Vendor
        else if constexpr (std::is_same_v<T, GoVendor>)
        {
            out << "GO_VENDOR";
            out << "\nVendorPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nVendorEntry: " << arg.vendorEntry;
            out << "\nlastReach: " << arg.lastReach;
            out << "\nvendorSellDone: " << arg.vendorSellDone;
            out << "\ndisenchantDone: " << arg.disenchantDone;
        }
        else if constexpr (std::is_same_v<T, WanderNpc>)
        {
            out << "WANDER_NPC";
            out << "\nnpcOrGo: entry " << arg.npcOrGo.GetEntry() << " (guid " << arg.npcOrGo.GetCounter()
                << ")";
            out << "\nlastWanderNpc: " << startT;
            out << "\nlastReachNpcOrGo: " << arg.lastReach;
        }
        else if constexpr (std::is_same_v<T, WanderRandom>)
        {
            out << "WANDER_RANDOM";
            out << "\nlastWanderRandom: " << startT;
        }
        else if constexpr (std::is_same_v<T, Idle>)
        {
            out << "IDLE";
        }
        else if constexpr (std::is_same_v<T, Rest>)
        {
            out << "REST";
            out << "\nlastRest: " << startT;
        }
        else if constexpr (std::is_same_v<T, DoQuest>)
        {
            out << "DO_QUEST";
            out << "\nquestId: " << arg.questId;
            out << "\nobjectiveIdx: " << arg.objectiveIdx;
            out << "\npoiPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastReachPOI: " << (arg.lastReachPOI ? GetMSTimeDiffToNow(arg.lastReachPOI) : 0);
        }
        else if constexpr (std::is_same_v<T, TravelFlight>)
        {
            out << "TRAVEL_FLIGHT";
            out << "\nflightMasterEntry: " << arg.flightMasterEntry;
            out << "\nfromNode: " << arg.path[0];
            out << "\ntoNode: " << arg.path[arg.path.size() - 1];
            out << "\ninFlight: " << arg.inFlight;
        }
        else if constexpr (std::is_same_v<T, OutdoorPvP>)
        {
            out << "OUTDOOR_PVP";
            if (!arg.capturePointSpawnId)
                out << "\nNo capture point assigned.";
            else
                out << "\ncapturePointSpawnId: " << arg.capturePointSpawnId;
        }
        else if constexpr (std::is_same_v<T, DoGather>)
        {
            out << "DO_GATHER";
            out << "\nnodeSpawnId: " << arg.nodeSpawnId;
            out << "\nnodePos: " << arg.nodePos.GetMapId() << " " << arg.nodePos.GetPositionX() << " "
                << arg.nodePos.GetPositionY() << " " << arg.nodePos.GetPositionZ();
            out << "\nvisited: " << arg.visited.size();
            out << "\nlastReach: " << (arg.lastReach ? GetMSTimeDiffToNow(arg.lastReach) : 0);
        }
        else
            out << "UNKNOWN";
    }, data);
    return out.str();
}
