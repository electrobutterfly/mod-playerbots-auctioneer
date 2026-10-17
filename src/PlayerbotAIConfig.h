/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTAICONFIG_H
#define PLAYERBOTS_PLAYERBOTAICONFIG_H

#include "DBCEnums.h"
#include "SharedDefines.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class BotCheatMask : uint32
{
    none = 0,
    taxi = 1,
    gold = 2,
    health = 4,
    mana = 8,
    power = 16,
    raid = 32,
    food = 64,
    maxMask = 128
};

enum class HealingManaEfficiency : uint8
{
    VERY_LOW = 1,
    LOW = 2,
    MEDIUM = 4,
    HIGH = 8,
    VERY_HIGH = 16,
    SUPERIOR = 32
};

enum class ShowHideCosmetic : uint8
{
    ALWAYS_HIDE = 0,
    ALWAYS_SHOW = 1,
    RANDOMIZE = 2
};

enum class AutoPartyBuffMode : uint8
{
    DISABLED = 0,
    RAID_ONLY = 1,
    GROUP_OR_RAID = 2
};

enum NewRpgStatus : int
{
    //Initial Status
    RPG_IDLE = 0,
    RPG_GO_GRIND = 1,
    RPG_GO_CAMP = 2,
    // Exploring nearby
    RPG_WANDER_RANDOM = 3,
    RPG_WANDER_NPC = 4,
    // Do Quest (based on quest status)
    RPG_DO_QUEST = 5,
    // Travel

    RPG_TRAVEL_FLIGHT = 6,
    // Taking a break
    RPG_REST = 7,
    RPG_OUTDOOR_PVP = 8,
    // Roam the current zone harvesting herb/ore nodes
    RPG_DO_GATHER = 9,
    // Auctioneer subsystem
    RPG_GO_AUCTIONEER = 10,
    // Add Vendor
    RPG_GO_VENDOR = 11,
    RPG_STATUS_END = 12
};

#define MAX_SPECNO 20

// One level range/bucket used by the random bot level brackets sub-feature (see RandomBotLevelMgr).
struct LevelBracketConfig
{
    uint8 Lower = 1;
    uint8 Upper = 80;
    uint8 Pct = 0;
};

class PlayerbotAIConfig
{
public:
    static PlayerbotAIConfig& Instance()
    {
        static PlayerbotAIConfig instance;

        return instance;
    }

    bool Initialize();
    void LoadRandomBotLevelConfig();
    bool IsInRandomAccountList(uint32 id);
    bool IsInRandomQuestItemList(uint32 id);
    bool IsPvpProhibited(uint32 zoneId, uint32 areaId);
    bool IsInPvpProhibitedZone(uint32 id);
    bool IsInPvpProhibitedArea(uint32 id);

    bool Enabled;
    bool DisabledWithoutRealPlayer;
    bool EnableICCBuffs;
    bool AllowAccountBots, AllowGuildBots, AllowTrustedAccountBots;
    bool RandomBotGuildNearby, RandomBotInvitePlayer, InviteChat;
    uint32 GlobalCoolDown, ReactDelay, MaxWaitForMove, DisableMoveSplinePath, MaxMovementSearchTime, ExpireActionTime,
        DispelAuraDuration, PassiveDelay, RepeatDelay, ErrorDelay, RpgDelay, SitDelay, ReturnDelay, LootDelay;
    // Add Auctioneer
    uint32 lootBagMinSlots;
    bool DynamicReactDelay;
    float SightDistance, SpellDistance, ReactDistance, GrindDistance, LootDistance, ShootDistance, FleeDistance,
        TooCloseDistance, MeleeDistance, FollowDistance, WhisperDistance, ContactDistance, AoeRadius, RpgDistance,
        TargetPosRecalcDistance, FarDistance, HealDistance, AggroDistance;
    uint32 CriticalHealth, LowHealth, MediumHealth, AlmostFullHealth;
    uint32 LowMana, MediumMana, HighMana;
    bool AutoSaveMana;
    uint32 SaveManaThreshold;
    AutoPartyBuffMode AutoGreaterBlessings;
    AutoPartyBuffMode AutoPartyBuffs;
    bool TellWhenMissingBuffReagents;
    uint32 MissingBuffReagentMessageCooldown;
    bool ForceRebuffOnReadyCheck;
    uint32 ForceRebuffMarginSecs;
    bool AutoAvoidAoe;
    float MaxAoeAvoidRadius;
    std::set<uint32> AoeAvoidSpellWhitelist;
    bool TellWhenAvoidAoe;
    std::set<uint32> DisallowedGameObjects;
    std::set<uint32> AttunementQuests;
    std::set<uint32> UnobtainableItems;

    uint32 OpenGoSpell;
    bool RandomBotAutologin;
    bool BotAutologin;
    std::string RandomBotMapsAsString;
    float ProbTeleToBankers;
    bool EnableWeightTeleToCityBankers;
    int WeightTeleToStormwind;
    int WeightTeleToIronforge;
    int WeightTeleToDarnassus;
    int WeightTeleToExodar;
    int WeightTeleToOrgrimmar;
    int WeightTeleToUndercity;
    int WeightTeleToThunderBluff;
    int WeightTeleToSilvermoonCity;
    int WeightTeleToShattrathCity;
    int WeightTeleToDalaran;
    std::vector<uint32> RandomBotMaps;
    std::vector<uint32> RandomBotQuestItems;
    std::vector<uint32> RandomBotAccounts;
    std::vector<uint32> RandomBotSpellIds;
    std::vector<uint32> RandomBotQuestIds;
    uint32 RandomBotTeleportDistance;
    float RandomGearLoweringChance;
    int32 RandomGearQualityLimit;
    int32 RandomGearScoreLimit;
    bool PreferClassArmorType;
    bool PreferredSpecWeapons;

    // Randombot starter bag mode. See playerbots.conf for the full description.
    //   1 = bots are granted a full bag set during randomization (historical)
    //   2 = bots keep only the containers granted at character creation and must
    //       earn further bags through gameplay
    uint32 RandomBotStarterBagMode;
    float RandomBotMinLevelChance, RandomBotMaxLevelChance;
    float RandomBotRpgChance;
    uint32 MinRandomBots, MaxRandomBots;
    uint32 RandomBotUpdateInterval, RandomBotCountChangeMinInterval, RandomBotCountChangeMaxInterval;
    uint32 MinRandomBotInWorldTime, MaxRandomBotInWorldTime;
    uint32 MinRandomBotRandomizeTime, MaxRandomBotRandomizeTime;
    uint32 MinRandomBotChangeStrategyTime, MaxRandomBotChangeStrategyTime;
    uint32 MinRandomBotReviveTime, MaxRandomBotReviveTime;
    uint32 MinRandomBotTeleportInterval, MaxRandomBotTeleportInterval;
    uint32 PermanentlyInWorldTime;
    uint32 MinRandomBotPvpTime, MaxRandomBotPvpTime;
    uint32 RandomBotsPerInterval;
    uint32 RandomBotPrintStatsInterval;
    uint32 MinRandomBotsPriceChangeInterval, MaxRandomBotsPriceChangeInterval;
    uint32 DisabledWithoutRealPlayerLoginDelay, DisabledWithoutRealPlayerLogoutDelay;
    bool RandomBotJoinLfg;
    bool RandomBotJoinLfgOutleveledExpansion;

    // Professions
    bool EnableFishingWithMaster;
    uint32 ClassMatchingProfessionChance;
    float FishingDistanceFromMaster, FishingDistance, EndFishingWithMaster;

    // chat
    bool RandomBotTalk;
    bool RandomBotEmote;
    bool RandomBotSuggestDungeons;
    bool EnableBroadcasts;
    bool EnableGreet;
    bool RandomBotSayWithoutMaster;
    bool AnnounceConsumableUse;

    uint32 BroadcastChanceMaxValue;

    uint32 BroadcastToGuildGlobalChance;
    uint32 BroadcastToWorldGlobalChance;
    uint32 BroadcastToGeneralGlobalChance;
    uint32 BroadcastToTradeGlobalChance;
    uint32 BroadcastToLFGGlobalChance;
    uint32 BroadcastToLocalDefenseGlobalChance;
    uint32 BroadcastToWorldDefenseGlobalChance;
    uint32 BroadcastToGuildRecruitmentGlobalChance;

    uint32 BroadcastChanceLootingItemPoor;
    uint32 BroadcastChanceLootingItemNormal;
    uint32 BroadcastChanceLootingItemUncommon;
    uint32 BroadcastChanceLootingItemRare;
    uint32 BroadcastChanceLootingItemEpic;
    uint32 BroadcastChanceLootingItemLegendary;
    uint32 BroadcastChanceLootingItemArtifact;

    uint32 BroadcastChanceQuestAccepted;
    uint32 BroadcastChanceQuestUpdateObjectiveCompleted;
    uint32 BroadcastChanceQuestUpdateObjectiveProgress;
    uint32 BroadcastChanceQuestUpdateFailedTimer;
    uint32 BroadcastChanceQuestUpdateComplete;
    uint32 BroadcastChanceQuestTurnedIn;

    uint32 BroadcastChanceKillNormal;
    uint32 BroadcastChanceKillElite;
    uint32 BroadcastChanceKillRareelite;
    uint32 BroadcastChanceKillWorldboss;
    uint32 BroadcastChanceKillRare;
    uint32 BroadcastChanceKillUnknown;
    uint32 BroadcastChanceKillPet;
    uint32 BroadcastChanceKillPlayer;

    uint32 BroadcastChanceLevelupGeneric;
    uint32 BroadcastChanceLevelupTenX;
    uint32 BroadcastChanceLevelupMaxLevel;

    uint32 BroadcastChanceSuggestInstance;
    uint32 BroadcastChanceSuggestQuest;
    uint32 BroadcastChanceSuggestGrindMaterials;
    uint32 BroadcastChanceSuggestGrindReputation;
    uint32 BroadcastChanceSuggestSell;
    uint32 BroadcastChanceSuggestSomething;

    uint32 BroadcastChanceSuggestSomethingToxic;

    uint32 BroadcastChanceSuggestToxicLinks;
    std::string ToxicLinksPrefix;
    uint32 ToxicLinksRepliesChance;

    uint32 BroadcastChanceSuggestThunderfury;
    uint32 ThunderfuryRepliesChance;

    uint32 BroadcastChanceGuildManagement;

    uint32 GuildRepliesRate;

    bool RandomBotJoinBG;
    bool RandomBotAutoJoinBG;

    std::string RandomBotAutoJoinICBrackets;
    std::string RandomBotAutoJoinEYBrackets;
    std::string RandomBotAutoJoinAVBrackets;
    std::string RandomBotAutoJoinABBrackets;
    std::string RandomBotAutoJoinWSBrackets;

    uint32 RandomBotAutoJoinBGICCount;
    uint32 RandomBotAutoJoinBGEYCount;
    uint32 RandomBotAutoJoinBGAVCount;
    uint32 RandomBotAutoJoinBGABCount;
    uint32 RandomBotAutoJoinBGWSCount;

    uint32 RandomBotAutoJoinArenaBracket;

    uint32 RandomBotAutoJoinBGRatedArena2v2Count;
    uint32 RandomBotAutoJoinBGRatedArena3v3Count;
    uint32 RandomBotAutoJoinBGRatedArena5v5Count;

    uint32 RandomBotTeleLowerLevel, RandomBotTeleHigherLevel;
    std::map<uint32, std::pair<uint32, uint32>> ZoneBrackets;
    bool LogInGroupOnly, LogValuesPerTick;

    // Add Auctioneer
    bool debugAuction;
    bool allowPlayerAuctionCommands;

    // Auction automation (see playerbots_auction.conf)
    bool auctionEnabled;
    uint32 auctionVisitMinMinutes;
    uint32 auctionVisitMaxMinutes;

    // When false, altbots and addclass bots (any bot whose IsAltBot()
    // returns true) are excluded from the AH automation pipeline. They
    // still visit vendors. See playerbots_auction.conf for the full
    // description and the reasoning behind the default.
    bool auctionAllowAltBots;

    bool auctionEnableSelling;
    uint32 auctionSellUndercutPercentMin;
    uint32 auctionSellUndercutPercentMax;
    uint32 auctionSellDurationMinutes;

    // Sell duration selection for auctions.
    //   0 = fixed   (every listing uses auctionSellDurationMinutes)
    //   1 = random  (every listing picks one entry from auctionSellDurationOptions)
    uint32 auctionSellDurationMode;
    std::vector<uint32> auctionSellDurationOptions;

    // Returns the duration (in minutes) to use for the next listing.
    // Fixed mode returns auctionSellDurationMinutes; random mode picks one
    // of auctionSellDurationOptions at random. The caller is expected to
    // invoke this once per listing so a bot that queues several items in
    // the same visit does not stamp them all with the same expiry.
    uint32 PickAuctionSellDuration() const;
    uint32 auctionMinSellValueCopper;
    uint32 auctionMinSellQuality;
    uint32 auctionKeepHealthPotions;
    uint32 auctionKeepManaPotions;
    uint32 auctionKeepHealthPotionsLargeBags;
    uint32 auctionKeepManaPotionsLargeBags;
    uint32 auctionLargeBagThreshold;
    bool auctionProtectClassReagents;
    bool auctionProtectQuestItems;
    bool auctionProtectBoundItems;

    bool auctionEnableBuying;
    uint32 auctionMaxSpendPercentPerVisit;
    bool auctionBuyGearUpgrades;
    bool auctionBuyBiggerBags;
    bool auctionBuyConsumables;
    bool auctionBuyProfessionMaterials;
    bool auctionBuyQuestItems;

    uint32 auctionStartingBidPercent;
    uint32 auctionBidIncrementPercent;
    bool auctionAllowBuyout;

    // Buy-side tuning
    std::vector<uint32> auctionAhBotGUIDs;
    uint32 auctionBuyPriceMultiplierMin;
    uint32 auctionBuyPriceMultiplierMax;
    uint32 auctionBuyScanChancePercent;
    uint32 auctionMaxTrackedBids;
    bool auctionProtectUsefulItems;
    bool auctionSellFullStacksOnly;
    bool auctionInitSpareInventory;

    // Cap on how many live auctions a single bot may have at once.
    // 0 disables selling entirely for bots (they may still buy from the AH).
    // Default: 100


    // Items that any NPC vendor sells, loaded once from npc_vendor at startup.
    // These are the "vendor junk" the AH should never carry: buying them on the
    // AH makes no sense when a supplier sells them for a few copper. Skips
    // Blacksmith Hammer, Mining Pick, Skinning Knife, Arclight Spanner, etc.
    bool auctionExcludeVendorSoldItems;
    std::unordered_set<uint32> auctionVendorSoldItems;
    bool IsVendorSoldItem(uint32 itemId) const
    {
        return auctionVendorSoldItems.find(itemId) != auctionVendorSoldItems.end();
    }

    std::set<uint32> auctionExcludedItemIds;
    bool IsAuctionExcludedItem(uint32 itemId) const
    {
        return auctionExcludedItemIds.find(itemId) != auctionExcludedItemIds.end();
    }

    uint32 auctionMaxActiveListingsPerBot;
    uint32 auctionMaxRelistRetries;
    uint32 auctionMaxSellsPerVisit;

    // Bag-buying target: the bot keeps buying bags from vendors and the
    // AH until every bag slot is at least this many slots. Once at the
    // target, a strictly larger affordable bag on offer is still bought
    // (bigger is always better). Set higher on well-developed servers.
    uint32 auctionBagBuyTarget;

    // Threshold of live container listings on the faction AH above which
    // tailors stop crafting bags purely for AH sale. Bags still get
    // crafted while any of the bot's own slots is empty or below
    // auctionBagBuyTarget. Set to 0 to disable bag-for-AH crafting.
    uint32 auctionBagAHCap;
    uint32 auctionRelistUndercutPercentMin;
    uint32 auctionRelistUndercutPercentMax;
    bool auctionRunAnalysis;

    // Auction baseline source.
    //   1 = legacy (live-scan + vendor multiple, previous behaviour)
    //   2 = consult auctioneer_baseline_item for pricing and startbids
    uint8 auctionBaselineMode;

    // Crafting arbitrage. When enabled and mode is 2, the sell path
    // reserves reagents for crafts whose (baseline) output value exceeds
    // reagent cost by at least auctionCraftMarginPercent.
    bool   auctionCraftArbitrageEnabled;
    uint32 auctionCraftMarginPercent;

    // How often the running worldserver polls auctioneer_baseline_item
    // for a newer snapshot and reloads the in-memory caches in place.
    // 0 disables the check entirely (reload only happens on boot).
    // Default 5 minutes.
    uint32 auctionBaselineReloadCheckMinutes;

    // Seller preference
    uint32 auctionSellerPreferenceAggression;
    uint32 auctionSellerPreferenceRealPlayer;
    uint32 auctionSellerPreferencePlayerbot;
    uint32 auctionSellerPreferenceAhBot;
    bool auctionDontOutbidRealPlayers;
    bool fleeingEnabled;
    bool SummonAtInnkeepersEnabled;
    std::string CombatStrategies, NonCombatStrategies;
    std::string RandomBotCombatStrategies, RandomBotNonCombatStrategies;
    std::string ReactStrategies, RandomBotReactStrategies;
    bool ApplyInstanceStrategies;
    uint32 RandomBotMinLevel, RandomBotMaxLevel;
    float RandomChangeMultiplier;

    // std::string premadeLevelSpec[MAX_CLASSES][10][91]; //lvl 10 - 100
    // ClassSpecs classSpecs[MAX_CLASSES];

    std::string PremadeSpecName[MAX_CLASSES][MAX_SPECNO];
    std::string PremadeSpecGlyph[MAX_CLASSES][MAX_SPECNO];
    std::vector<uint32> ParsedSpecGlyph[MAX_CLASSES][MAX_SPECNO];
    std::string PremadeSpecLink[MAX_CLASSES][MAX_SPECNO][MAX_LEVEL];
    std::string PremadeHunterPetLink[3][21];
    std::vector<std::vector<uint32>> ParsedSpecLinkOrder[MAX_CLASSES][MAX_SPECNO][MAX_LEVEL];
    std::vector<std::vector<uint32>> ParsedHunterPetLinkOrder[3][21];
    uint32 RandomClassSpecProb[MAX_CLASSES][MAX_SPECNO];
    uint32 RandomClassSpecIndex[MAX_CLASSES][MAX_SPECNO];

    std::string CommandPrefix, CommandSeparator;
    std::string RandomBotAccountPrefix;
    uint32 RandomBotAccountCount;
    bool RandomBotRandomPassword;
    bool DeleteRandomBotAccounts;
    uint32 RandomBotGuildCount, RandomBotGuildSizeMax;
    bool DeleteRandomBotGuilds;
    std::vector<uint32> PvpProhibitedZoneIds;
    std::vector<uint32> PvpProhibitedAreaIds;
    bool FastReactInBG;

    bool RandomBotsWalkingRPG;
    bool RandomBotsWalkingRPGInDoors;
    uint32 MinEnchantingBotLevel;
    uint32 LimitEnchantExpansion;
    uint32 LimitGearExpansion;
    uint32 RandomBotStartingLevel;
    bool EnablePeriodicOnlineOffline;
    float PeriodicOnlineOfflineRatio;
    bool GearScoreCheck;
    bool RandomBotPreQuests;
    bool BotSendMailEnabled;

    bool GuildTaskEnabled;
    uint32 MinGuildTaskChangeTime, MaxGuildTaskChangeTime;
    uint32 MinGuildTaskAdvertisementTime, MaxGuildTaskAdvertisementTime;
    uint32 MinGuildTaskRewardTime, MaxGuildTaskRewardTime;
    uint32 GuildTaskAdvertCleanupTime;
    uint32 GuildTaskKillTaskDistance;

    uint32 IterationsPerTick;

    std::mutex LogMutex;
    bool EnableAutoTradeOnItemMention;
    std::vector<std::string> TradeActionExcludedPrefixes;
    std::vector<std::string> AllowedLogFiles;
    std::unordered_map<std::string, std::pair<FILE*, bool>> LogFiles;

    std::vector<std::string> BotCheats;
    uint32 BotCheatMask = 0;

    struct WorldBuff
    {
        uint32 SpellId;
        uint32 FactionId;
        uint32 ClassId;
        uint32 SpecId;
        uint32 MinLevel;
        uint32 MaxLevel;
    };

    std::vector<WorldBuff> WorldBuffs;

    uint32 CommandServerPort;
    bool PerfMonEnabled;
    bool SummonWhenGroup;
    ShowHideCosmetic RandomBotShowHelmet;
    ShowHideCosmetic RandomBotShowCloak;
    bool RandomBotFixedLevel;
    bool DisableRandomLevels;
    float RandomBotXPRate;
    uint32 RandomBotAllianceRatio;
    uint32 RandomBotHordeRatio;
    bool DisableDeathKnightLogin;
    bool LimitTalentsExpansion;
    uint32 BotActiveAlone;
    uint32 BotActiveAloneDurationSeconds;
    uint32 BotActiveAloneForceWhenInRadius;
    bool BotActiveAloneForceWhenInZone;
    bool BotActiveAloneForceWhenInMap;
    bool BotActiveAloneForceWhenIsFriend;
    bool BotActiveAloneForceWhenInGuild;
    bool BotActiveAloneSmartScale;
    uint32 BotActiveAloneSmartScaleDiffLimitFloor;
    uint32 BotActiveAloneSmartScaleDiffLimitCeiling;
    uint32 BotActiveAloneSmartScaleWhenMinLevel;
    uint32 BotActiveAloneSmartScaleWhenMaxLevel;

    bool FreeMethodLoot;
    int32 LootNeedRollLevel;
    bool LootGreedRollLevel;
    bool LootRollRecipe;
    bool LootRollDisenchant;
    std::string AutoPickReward;
    bool AutoEquipUpgradeLoot;
    float EquipUpgradeThreshold;
    bool TwoRoundsGearInit;
    bool SyncQuestWithPlayer;
    bool SyncQuestForPlayer;
    bool DropObsoleteQuests;
    bool AllowLearnTrainerSpells;
    bool AutoPickTalents;
    bool AutoUpgradeEquip;
    int32 HunterWolfPet;
    int32 DefaultPetStance;
    int32 PetChatCommandDebug;
    bool AutoLearnTrainerSpells;
    bool AutoDoQuests;
    bool EnableNewRpgStrategy;
    std::unordered_map<NewRpgStatus, uint32> RpgStatusProbWeight;
    // Add Auctioneer
    int32 auctionVisitMinLevel;
    int32 auctionVisitMinBagFillPercent;
    int32 auctionVisitMinGoldCopper;

    // Add Vendor
    int32 vendorVisitMinLevel;
    int32 vendorVisitMinBagFillPercent;
    uint32 vendorVisitMinMinutes;
    uint32 vendorVisitMaxMinutes;
    bool SyncLevelWithPlayers;
    bool RandomBotConcentrateInPlayerZone;
    bool AutoLearnQuestSpells;
    bool AutoTeleportForLevel;
    bool RandomBotGroupNearby;
    int32 EnableRandomBotTrading;
    uint32 TweakValue;  // Debugging config

    uint32 RandomBotArenaTeamMaxRating;
    uint32 RandomBotArenaTeamMinRating;
    uint32 RandomBotArenaTeam2v2Count;
    uint32 RandomBotArenaTeam3v3Count;
    uint32 RandomBotArenaTeam5v5Count;
    bool DeleteRandomBotArenaTeams;

    uint32 SelfBotLevel;
    bool DowngradeMaxLevelBot;
    bool EquipAndSpecPersistence;
    int32 EquipAndSpecPersistenceLevel;
    int32 GroupInvitationPermission;
    bool AllowBattlegroundCommands;
    bool KeepAltsInGroup = false;
    bool AllowSummonInCombat;
    bool AllowSummonWhenMasterIsDead;
    bool AllowSummonWhenBotIsDead;
    int ReviveBotWhenSummoned;
    bool BotRepairWhenSummon;
    bool AutoInitOnly;
    bool ResetInstanceIdForAltBots;
    float AutoInitEquipLevelLimitRatio;
    int32 MaxAddedBots;
    int32 AddClassCommand;
    int32 AddClassAccountPoolSize;
    bool AddClassRandomCharacter;
    int32 MaintenanceCommand;
    bool AltMaintenanceAttunementQs,
            AltMaintenanceBags,
            AltMaintenanceAmmo,
            AltMaintenanceFood,
            AltMaintenanceReagents,
            AltMaintenanceConsumables,
            AltMaintenancePotions,
            AltMaintenanceTalentTree,
            AltMaintenancePet,
            AltMaintenancePetTalents,
            AltMaintenanceClassSpells,
            AltMaintenanceAvailableSpells,
            AltMaintenanceSkills,
            AltMaintenanceReputation,
            AltMaintenanceSpecialSpells,
            AltMaintenanceMounts,
            AltMaintenanceGlyphs,
            AltMaintenanceKeyring,
            AltMaintenanceGemsEnchants;
    int32 AutoGearCommand, AutoGearCommandAltBots, AutoGearQualityLimit, AutoGearScoreLimit;
    int32 AutoGearBisCommand;

    uint32 UseGroundMountAtMinLevel;
    uint32 UseFastGroundMountAtMinLevel;
    uint32 UseFlyMountAtMinLevel;
    uint32 UseFastFlyMountAtMinLevel;

    // stagger flightpath takeoff
    uint32 BotTaxiDelayMin;
    uint32 BotTaxiDelayMax;
    uint32 BotTaxiGapMs;
    uint32 BotTaxiGapJitterMs;

    std::string const GetTimestampStr();
    bool HasLog(std::string const fileName)
    {
        return std::find(AllowedLogFiles.begin(), AllowedLogFiles.end(), fileName) != AllowedLogFiles.end();
    };
    bool OpenLog(std::string const fileName, char const* mode = "a");
    bool IsLogOpen(std::string const fileName)
    {
        auto it = LogFiles.find(fileName);
        return it != LogFiles.end() && it->second.second;
    }
    void Log(std::string const fileName, char const* str, ...);

    void LoadWorldBuff();

    static std::vector<std::vector<uint32>> ParseTempTalentsOrder(uint32 cls, std::string temp_talents_order);
    static std::vector<std::vector<uint32>> ParseTempPetTalentsOrder(uint32 spec, std::string temp_talents_order);

    bool RestrictHealerDPS = false;
    std::vector<uint32> RestrictedHealerDPSMaps;
    bool IsRestrictedHealerDPSMap(uint32 mapId) const;

    std::vector<uint32> ExcludedHunterPetFamilies;

    // Random bot level brackets (periodic redistribution across per-faction level ranges). See
    // RandomBotLevelMgr; percentages here are the as-configured values, not the runtime working copy.
    bool LevelBracketsEnabled;
    uint32 LevelBracketsCheckFrequency;
    uint32 LevelBracketsFlaggedCheckFrequency;
    uint32 LevelBracketsFlaggedProcessLimit;
    bool LevelBracketsIgnoreGuildWithRealPlayers;
    bool LevelBracketsIgnoreArenaTeamBots;
    bool LevelBracketsIgnoreFriendListed;
    std::vector<std::string> LevelBracketsExcludeNames;
    uint8 LevelBracketsNumRanges;
    std::vector<LevelBracketConfig> LevelBracketsAlliance;
    std::vector<LevelBracketConfig> LevelBracketsHorde;
    bool LevelBracketsDynamicDistribution;
    float LevelBracketsRealPlayerWeight;
    bool LevelBracketsSyncFactions;

    // Random bot level reset (reset random bots reaching max level). See RandomBotLevelMgr.
    bool ResetBotLevelEnabled;
    uint8 ResetBotLevelMaxLevel;
    uint8 ResetBotLevelResetTo;
    uint8 ResetBotLevelSkipFrom;
    uint8 ResetBotLevelSkipTo;
    uint8 ResetBotLevelChance;
    bool ResetBotLevelScaledChance;
    bool ResetBotLevelRestrictTimePlayed;
    uint32 ResetBotLevelMinTimePlayed;
    uint32 ResetBotLevelPlayedTimeCheckFrequency;
    bool ResetBotLevelIgnoreGuildWithRealPlayers;
    std::vector<std::string> ResetBotLevelExcludeNames;

private:
    PlayerbotAIConfig() = default;
    ~PlayerbotAIConfig() = default;

    PlayerbotAIConfig(PlayerbotAIConfig const&) = delete;
    PlayerbotAIConfig& operator=(PlayerbotAIConfig const&) = delete;

    PlayerbotAIConfig(PlayerbotAIConfig&&) = delete;
    PlayerbotAIConfig& operator=(PlayerbotAIConfig&&) = delete;
};

#define sPlayerbotAIConfig PlayerbotAIConfig::Instance()

#endif
