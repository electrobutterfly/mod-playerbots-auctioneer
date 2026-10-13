/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGINFO_H
#define PLAYERBOTS_NEWRPGINFO_H

#include <unordered_set>

#include "Define.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Strategy.h"
#include "Timer.h"
#include "TravelMgr.h"

using NewRpgStatusTransitionProb = std::vector<std::vector<int>>;

struct NewRpgInfo
{
    NewRpgInfo() : data(Idle{}) {}
    ~NewRpgInfo() = default;

    // RPG_GO_GRIND
    struct GoGrind
    {
        WorldPosition pos{};
    };
    // RPG_GO_CAMP
    struct GoCamp
    {
        WorldPosition pos{};
    };
    // RPG_GO_AUCTIONEER
    struct GoAuctioneer
    {
        WorldPosition pos{};
        uint32 auctioneerEntry{0};
        uint32 lastReach{0};
        // Get MAIL AUCTION returned mail
        bool mailChecked{false};
        // Sell-queue build flag. Set to true once the queue has been built
        // from the bot's current bag contents at AH arrival time. This is
        // what keeps the queue fresh: it used to be built at status-selection
        // time, but the bot may loot, fight or trade in between, so the
        // pre-built queue was frequently stale and returned mail items had
        // to be injected through a separate prepend path.
        bool sellQueueBuilt{false};
        // FIFO queue of items to sell this visit, in order. Populated at AH
        // arrival time (see sellQueueBuilt). Returned-auction items -- those
        // whose entry ID appears in returnedEntries -- sit at the front so
        // they use the relist undercut band.
        std::vector<ObjectGuid> pendingSells;
        // Entries that came from returned auction mail. Any item whose entry
        // is in this list uses the relist undercut range.
        std::vector<uint32> returnedEntries;
        // Optional buy task. If buyAuctionId is 0, no buy was queued.
        uint32 buyAuctionId{0};
        uint32 buyPrice{0};
        uint32 buyMaxWilling{0};
        bool buyDone{false};
        // Tracked-bids pass flag. Set to true after this visit's
        // re-bid walk completes.
        bool trackedBidsDone{false};
        // Item IDs the bot reserves for a profitable craft this visit.
        // Populated on the first sell of the visit from the baseline
        // arbitrage advisor and consulted for every queued sell.
        std::vector<uint32> protectedReagentIds;
        bool protectedReagentsComputed{false};

        // Mail-collection sub-state. When the bot needs to walk away from
        // the auctioneer to reach a mailbox:
        //   0 = not walking
        //   1 = walking to the mailbox
        //   2 = walking back to the auctioneer after collecting
        // While this is non-zero, the auctioneer-approach and HELLO logic
        // is bypassed so the walk can actually complete.
        uint8 mailboxWalkState{0};
        // Set to true once the bot has been teleported to a city that hosts
        // this visit's auctioneer. Far-away bots (3000+ yards across a
        // continent) cannot walk to the auctioneer in the visit budget; the
        // teleport is the equivalent of a real player taking a flight path
        // to a city. Once teleported, the bot walks the remaining short
        // distance normally.
        bool cityTeleported{false};
    };
    // RPG_GO_VENDOR
    struct GoVendor
    {
        WorldPosition pos{};
        uint32 vendorEntry{0};
        uint32 lastReach{0};
        // One-shot flag: the vendor-sell pass has been run for this visit.
        // The sell pass is idempotent (SellVendorItemsVisitor iterates bags
        // and sells everything classified VENDOR), so it only needs to run
        // once per visit.
        bool vendorSellDone{false};
        // Disenchant pass. Each tick the action tries to start one disenchant
        // cast while the bot is not already casting. A successful start
        // increments disenchantSuccesses; two consecutive failures (nothing
        // left in the DISENCHANT bucket) or a hit on the safety cap end the
        // pass. The status update completes the visit once the sell pass has
        // run AND the disenchant pass has ended.
        uint32 disenchantAttempts{0};
        uint32 disenchantSuccesses{0};
        bool disenchantDone{false};
        // Set to true once the bot has been teleported to a city that hosts
        // this visit's vendor. See the same field on GoAuctioneer.
        bool cityTeleported{false};
    };
    // RPG_WANDER_NPC
    struct WanderNpc
    {
        ObjectGuid npcOrGo{};
        uint32 lastReach{0};
    };
    // RPG_WANDER_RANDOM
    struct WanderRandom
    {
        WanderRandom() = default;
    };
    // RPG_DO_QUEST
    struct DoQuest
    {
        Quest const* quest{nullptr};
        uint32 questId{0};
        int32 objectiveIdx{0};
        WorldPosition pos{};
        uint32 lastReachPOI{0};
    };
    // RPG_TRAVEL_FLIGHT
    struct TravelFlight
    {
        uint32 flightMasterEntry{0};
        WorldPosition flightMasterPos{};
        std::vector<uint32> path;
        bool inFlight{false};
    };
    // RPG_REST
    struct Rest
    {
        Rest() = default;
    };
    // RPG_OUTDOOR_PVP
    struct OutdoorPvP
    {
        ObjectGuid::LowType capturePointSpawnId{0};
    };
    // RPG_DO_GATHER
    struct DoGather
    {
        ObjectGuid::LowType nodeSpawnId{0};
        WorldPosition nodePos{};
        std::unordered_set<ObjectGuid::LowType> visited;
        uint32 lastReach{0};
        uint32 lastPassiveCheck{0};
        ObjectGuid::LowType lastSwitchedFrom{0};
    };
    struct Idle
    {
    };

    uint32 startT{0};  // start timestamp of the current status

    // MOVE_FAR
    float nearestMoveFarDis{FLT_MAX};
    uint32 stuckTs{0};
    uint32 stuckAttempts{0};
    WorldPosition moveFarPos;
    // END MOVE_FAR

    using RpgData = std::variant<
        Idle,
        GoGrind,
        GoCamp,
        //Add Auctioneer
        GoAuctioneer,
        //Add Vendor
        GoVendor,
        WanderNpc,
        WanderRandom,
        DoQuest,
        Rest,
        TravelFlight,
        OutdoorPvP,
        DoGather
    >;
    RpgData data;

    NewRpgStatus GetStatus();
    static NewRpgStatus StatusFromString(std::string const& name);
    bool HasStatusPersisted(uint32 maxDuration) { return GetMSTimeDiffToNow(startT) > maxDuration; }
    void ChangeToGoGrind(WorldPosition pos);
    void ChangeToGoCamp(WorldPosition pos);
    // Add Auctioneer
    void ChangeToGoAuctioneer(WorldPosition pos, uint32 auctioneerEntry,
                              std::vector<ObjectGuid> const& pendingSells = {},
                              uint32 buyAuctionId = 0,
                              uint32 buyPrice = 0,
                              uint32 buyMaxWilling = 0);
    // Add Vendor
    void ChangeToGoVendor(WorldPosition pos, uint32 vendorEntry);
    void ChangeToWanderNpc();
    void ChangeToWanderRandom();
    void ChangeToDoQuest(uint32 questId, Quest const* quest);
    void ChangeToTravelFlight(uint32 flightMasterEntry, WorldPosition flightMasterPos, std::vector<uint32> path);
    void ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId = 0);
    void ChangeToDoGather();
    void ChangeToRest();
    void ChangeToIdle();
    bool CanChangeTo(NewRpgStatus status);
    void Reset();
    void SetMoveFarTo(WorldPosition pos);
    std::string ToString();
};

struct NewRpgStatistic
{
    uint32 questAccepted{0};
    uint32 questCompleted{0};
    uint32 questAbandoned{0};
    uint32 questRewarded{0};
    uint32 questDropped{0};
    NewRpgStatistic operator+(NewRpgStatistic const& other) const
    {
        NewRpgStatistic result;
        result.questAccepted = this->questAccepted + other.questAccepted;
        result.questCompleted = this->questCompleted + other.questCompleted;
        result.questAbandoned = this->questAbandoned + other.questAbandoned;
        result.questRewarded = this->questRewarded + other.questRewarded;
        result.questDropped = this->questDropped + other.questDropped;
        return result;
    }
    NewRpgStatistic& operator+=(NewRpgStatistic const& other)
    {
        this->questAccepted += other.questAccepted;
        this->questCompleted += other.questCompleted;
        this->questAbandoned += other.questAbandoned;
        this->questRewarded += other.questRewarded;
        this->questDropped += other.questDropped;
        return *this;
    }
};

#endif
