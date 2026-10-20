/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CastCustomSpellAction.h"
#include "AuctionBaseline.h"
#include "AuctionHelpers.h"
#include "AuctionHouseMgr.h"
#include "ChatHelper.h"
#include "Event.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"
#include "ServerFacade.h"

size_t FindLastSeparator(std::string const text, std::string const sep)
{
    size_t pos = text.rfind(sep);
    if (pos == std::string::npos)
        return pos;

    size_t lastLinkBegin = text.rfind("|H");
    size_t lastLinkEnd = text.find("|h|r", lastLinkBegin + 1);
    if (pos >= lastLinkBegin && pos <= lastLinkEnd)
        pos = text.find_last_of(sep, lastLinkBegin);

    return pos;
}

static inline void ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

bool CastCustomSpellAction::Execute(Event event)
{
    // only allow proper vehicle seats
    if (botAI->IsInVehicle() && !botAI->IsInVehicle(false, false, true))
        return false;

    Player* master = GetMaster();
    Unit* target = nullptr;
    std::string text = event.getParam();

    GuidVector gos = chat->parseGameobjects(text);
    if (!gos.empty())
    {
        for (auto go : gos)
        {
            if (!target)
                target = botAI->GetUnit(go);

            if (!botAI->GetUnit(go) || !botAI->GetUnit(go)->IsInWorld())
                continue;

            chat->eraseAllSubStr(text, chat->FormatWorldobject(botAI->GetUnit(go)));
        }

        ltrim(text);
    }

    uint32 spell = 0;
    if (!target)
    {
        size_t onPos = FindLastSeparator(text, " on ");
        if (onPos != std::string::npos)
        {
            std::string targetName = text.substr(onPos + 4);
            ltrim(targetName);
            if (!targetName.empty())
            {
                // check if spell still exists after we remove " on PlayerName" part
                std::string truncatedText = text.substr(0, onPos);
                ltrim(truncatedText);
                spell = AI_VALUE2(uint32, "spell id", truncatedText);

                if (spell)
                {
                    if (Player* targetPlayer = ObjectAccessor::FindPlayerByName(targetName))
                    {
                        target = targetPlayer;
                        text = truncatedText;
                    }
                }
            }
        }
    }

    if (!target)
        if (master && master->GetTarget())
            target = botAI->GetUnit(master->GetTarget());

    if (!target)
        target = bot;

    if (!master)  // Use self as master for permissions.
        master = bot;

    Item* itemTarget = nullptr;

    size_t pos = FindLastSeparator(text, " ");
    uint32 castCount = 1;
    if (pos != std::string::npos)
    {
        std::string const param = text.substr(pos + 1);
        std::vector<Item*> items = InventoryAction::parseItems(param, ITERATE_ITEMS_IN_BAGS);
        if (!items.empty())
            itemTarget = *items.begin();
        else
        {
            if (atoi(param.c_str()) > 0)
                text = text.substr(0, pos);
        }
    }

    if (!spell)
        spell = AI_VALUE2(uint32, "spell id", text);

    std::ostringstream msg;
    if (!spell)
    {
        msg << "Unknown spell " << text;
        botAI->TellError(msg.str());
        return false;
    }

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell);
    if (!spellInfo)
    {
        msg << "Unknown spell " << text;
        botAI->TellError(msg.str());
        return false;
    }

    if (target != bot && !bot->HasInArc(CAST_ANGLE_IN_FRONT, target, sPlayerbotAIConfig.SightDistance))
    {
        ServerFacade::instance().SetFacingTo(bot, target);
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.ReactDelay);

        msg << "cast " << text;
        botAI->HandleCommand(CHAT_MSG_WHISPER, msg.str(), master);
        return true;
    }

    std::ostringstream spellName;
    spellName << ChatHelper::FormatSpell(spellInfo) << " on ";

    bool const hasItemTarget = itemTarget &&
        (spellInfo->Targets & TARGET_FLAG_ITEM || spellInfo->Targets & TARGET_FLAG_GAMEOBJECT_ITEM);

    if (bot->GetTrader())
        spellName << "trade item";
    else if (hasItemTarget)
        spellName << chat->FormatItem(itemTarget->GetTemplate());
    else if (target != bot)
        spellName << target->GetName();
    else
        spellName << "self";

    if (!bot->GetTrader() && !botAI->CanCastSpell(spell, target, true, itemTarget))
    {
        msg << "Cannot cast " << spellName.str();
        botAI->TellError(msg.str());
        return false;
    }

    bool result = spell ? botAI->CastSpell(spell, target, itemTarget) : botAI->CastSpell(text, target, itemTarget);
    if (result)
    {
        msg << "Casting " << spellName.str();

        if (castCount > 1)
        {
            std::ostringstream cmd;
            cmd << castString(target) << " " << text << " " << (castCount - 1);
            botAI->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), master);
            msg << "|cffffff00(x" << (castCount - 1) << " left)|r";
        }

        botAI->TellMasterNoFacing(msg.str());
    }
    else
    {
        msg << "Cast " << spellName.str() << " is failed";
        botAI->TellError(msg.str());
    }

    return result;
}

bool CastCustomNcSpellAction::isUseful() { return !bot->IsInCombat(); }

std::string const CastCustomNcSpellAction::castString(WorldObject* target)
{
    return "castnc " + chat->FormatWorldobject(target);
}

bool CastRandomSpellAction::AcceptSpell(SpellInfo const* spellInfo)
{
    bool isTradeSkill = spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM &&
                        spellInfo->ReagentCount[EFFECT_0] > 0 && spellInfo->SchoolMask == 1;
    return !isTradeSkill && spellInfo->GetRecoveryTime() < MINUTE * IN_MILLISECONDS;
}

// Returns true if the produced item can currently be listed on the AH by
// this bot. Used at craft-goal pin time and at craft-goal re-check time so
// that a bot whose random craft pick lands on an item the AH will reject
// does not stay pinned to it for the full 12-hour goal window, crafting
// stack after stack that nothing can sell. Non-AH-classified items (equip,
// use, quest, skill, ammo, disenchant) bypass this check - they are not
// intended for the AH and their goal does not depend on AH capacity.
static bool AhCanAcceptItem(Player* bot, uint32 itemId)
{
    if (sPlayerbotAIConfig.IsAuctionExcludedItem(itemId))
        return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    if (sPlayerbotAIConfig.auctionExcludeVendorSoldItems &&
        proto->Class != ITEM_CLASS_TRADE_GOODS &&
        proto->Class != ITEM_CLASS_RECIPE &&
        proto->Class != ITEM_CLASS_CONSUMABLE &&
        sPlayerbotAIConfig.IsVendorSoldItem(itemId))
        return false;

    if (sPlayerbotAIConfig.auctionBaselineMode != 2)
        return true;  // no baseline data, no per-item cap

    uint8 const faction = (bot->GetTeamId() == TEAM_ALLIANCE) ? 2 : 6;
    PlayerbotAuction::Baseline const* baseline =
        PlayerbotAuction::AuctionBaselineMgr::instance().LookupWithFallback(faction, itemId, 0);
    if (!baseline || baseline->listingTypical == 0)
        return true;

    uint32 liveForItem = 0;
    if (AuctionHouseObject* ahObj = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
    {
        for (auto it = ahObj->GetAuctionsBegin(); it != ahObj->GetAuctionsEnd(); ++it)
        {
            if (it->second && it->second->item_template == itemId)
                ++liveForItem;
        }
    }
    return liveForItem < baseline->listingTypical;
}

bool CastRandomSpellAction::Execute(Event event)
{
    std::vector<std::pair<uint32, std::string>> spellMap = GetSpellList();
    Player* master = GetMaster();

    Unit* target = nullptr;
    GameObject* got = nullptr;

    std::string name = event.getParam();
    if (name.empty())
        name = getName();

    GuidVector wos = chat->parseGameobjects(name);
    for (auto wo : wos)
    {
        target = botAI->GetUnit(wo);
        got = botAI->GetGameObject(wo);

        if (got || target)
            break;
    }

    if (!got && !target && bot->GetTarget())
    {
        target = botAI->GetUnit(bot->GetTarget());
        got = botAI->GetGameObject(bot->GetTarget());
    }

    if (!got && !target)
        if (master && master->GetTarget())
            target = botAI->GetUnit(master->GetTarget());

    if (!got && !target)
        target = bot;

    // 12 hours: matches the shortest supported listing duration. A goal is
    // abandoned once it has not made progress for one full listing cycle,
    // which is the point at which a stuck goal starts holding reagents that
    // could be doing something else. The AH-acceptability check further
    // below retires a goal earlier than this if the AH has filled up.
    constexpr uint32 CRAFT_GOAL_TIMEOUT_SEC = 12 * 60 * 60;  // 12 hours
    bool const isCraftAction = (getName() == "craft random item");
    uint32 const craftBotLow = bot->GetGUID().GetCounter();

    auto usageIsWorthGoal = [](ItemUsage u) -> bool {
        return u == ITEM_USAGE_EQUIP ||
               u == ITEM_USAGE_REPLACE ||
               u == ITEM_USAGE_AH ||
               u == ITEM_USAGE_SKILL ||
               u == ITEM_USAGE_USE ||
               u == ITEM_USAGE_AMMO ||
               u == ITEM_USAGE_DISENCHANT;
    };

    if (isCraftAction && sRandomPlayerbotMgr.IsRandomBot(bot))
    {
        uint32 goalItem = sRandomPlayerbotMgr.GetValue(craftBotLow, "craft.goal.item");

        if (goalItem)
        {
            uint32 const goalSince = sRandomPlayerbotMgr.GetValue(craftBotLow, "craft.goal.since");
            if ((time(nullptr) - static_cast<time_t>(goalSince)) > CRAFT_GOAL_TIMEOUT_SEC)
            {
                sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.item", 0);
                goalItem = 0;
            }
        }

        if (goalItem)
        {
            // Retire the goal if it is no longer useful (e.g. bot already
            // has enough bandages, or the item has been superseded as gear),
            // or if it is AH-classified and the AH can no longer accept
            // another listing - the baseline per-item cap, the excluded-item
            // list, or the vendor-sold filter has started rejecting it. The
            // AH case is what avoids a bot spending a full goal window
            // crafting stacks that the sell path will never list.
            ItemUsage goalUsage = AI_VALUE2(ItemUsage, "item usage", goalItem);
            bool retireGoal = !usageIsWorthGoal(goalUsage);
            if (!retireGoal && goalUsage == ITEM_USAGE_AH && !AhCanAcceptItem(bot, goalItem))
                retireGoal = true;
            if (retireGoal)
            {
                sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.item", 0);
                goalItem = 0;
            }
        }

        if (goalItem)
        {
            // Locate the recipe that produces the goal item.
            uint32 goalSpell = 0;
            for (auto const& spellState : bot->GetSpellMap())
            {
                if (spellState.second->State == PLAYERSPELL_REMOVED || !spellState.second->Active)
                    continue;
                SpellInfo const* si = sSpellMgr->GetSpellInfo(spellState.first);
                if (!si)
                    continue;
                if (si->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM)
                    continue;
                if (si->Effects[EFFECT_0].ItemType != goalItem)
                    continue;
                goalSpell = spellState.first;
                break;
            }

            if (goalSpell == 0)
            {
                // Bot no longer knows the recipe; retire the goal.
                sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.item", 0);
            }
            else if (botAI->CanCastSpell(goalSpell, bot, true, nullptr, nullptr, true) &&
                     castSpell(goalSpell, bot))
            {
                // Goal cast succeeded. Reset the timeout clock so the goal
                // remains the target until it stops being useful.
                sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.since",
                                             static_cast<uint32>(time(nullptr)));

                ItemTemplate const* producedProto = sObjectMgr->GetItemTemplate(goalItem);
                LOG_DEBUG("playerbots",
                          "[CRAFT] {} cast {} producing [{}] {} (goal)",
                          bot->GetName(), goalSpell, goalItem,
                          producedProto ? producedProto->Name1 : "<unknown>");

                if (MultiCast && bot->HasInArc(CAST_ANGLE_IN_FRONT, bot, sPlayerbotAIConfig.SightDistance))
                {
                    std::ostringstream cmd;
                    cmd << "castnc " << chat->FormatWorldobject(bot) + " " << goalSpell << " " << 19;
                    botAI->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), bot);
                }
                return true;
            }
            else
            {
                // Goal not castable right now. Look for a missing reagent the
                // bot could craft itself and craft that instead: this is the
                // "follow the path" step. Bag needs bolts: bot crafts one
                // bolt per visit until the bag recipe has its 5-6 bolts.
                SpellInfo const* goalInfo = sSpellMgr->GetSpellInfo(goalSpell);
                bool progressed = false;
                if (goalInfo)
                {
                    for (uint8 r = 0; r < MAX_SPELL_REAGENTS && !progressed; ++r)
                    {
                        uint32 const rId    = goalInfo->Reagent[r];
                        uint32 const rCount = goalInfo->ReagentCount[r];
                        if (!rId || !rCount)
                            continue;
                        if (bot->GetItemCount(rId, false) >= rCount)
                            continue;  // satisfied

                        // Is this reagent craftable by the bot?
                        uint32 rSpell = 0;
                        for (auto const& spellState : bot->GetSpellMap())
                        {
                            if (spellState.second->State == PLAYERSPELL_REMOVED || !spellState.second->Active)
                                continue;
                            SpellInfo const* si = sSpellMgr->GetSpellInfo(spellState.first);
                            if (!si)
                                continue;
                            if (si->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM)
                                continue;
                            if (si->Effects[EFFECT_0].ItemType != rId)
                                continue;
                            rSpell = spellState.first;
                            break;
                        }
                        if (rSpell == 0)
                            continue;  // reagent comes from world/gather/vendor, not craft

                        if (!botAI->CanCastSpell(rSpell, bot, true, nullptr, nullptr, true))
                            continue;

                        if (castSpell(rSpell, bot))
                        {
                            sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.since",
                                                         static_cast<uint32>(time(nullptr)));
                            ItemTemplate const* rProto = sObjectMgr->GetItemTemplate(rId);
                            LOG_DEBUG("playerbots",
                                      "[CRAFT] {} cast {} producing [{}] {} (reagent for goal [{}])",
                                      bot->GetName(), rSpell, rId,
                                      rProto ? rProto->Name1 : "<unknown>", goalItem);
                            progressed = true;
                        }
                    }
                }

                if (progressed)
                    return true;

                // No progress this visit; keep the goal for the next visit and
                // fall through to the normal random pick so the bot is not
                // idle. The timeout above will retire the goal if it stays
                // unreachable.
            }
        }
    }

    std::vector<std::pair<uint32, std::pair<uint32, WorldObject*>>> spellList;

    for (auto& spell : spellMap)
    {
        uint32 spellId = spell.first;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        if (!AcceptSpell(spellInfo))
            continue;

        if (bot->HasSpell(spellId))
        {
            uint32 spellPriority = GetSpellPriority(spellInfo);

            if (target && botAI->CanCastSpell(spellId, target, true, nullptr, nullptr, true))
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, target)));
            if (got && botAI->CanCastSpell(spellId, got->GetPositionX(), got->GetPositionY(), got->GetPositionZ(),
                                           true, nullptr))
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, got)));
            if (botAI->CanCastSpell(spellId, bot, true, nullptr, nullptr, true))
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, bot)));
        }
    }

    if (spellList.empty())
        return false;

    // bool isCast = false; //not used, line marked for removal.

    std::sort(spellList.begin(), spellList.end(),
              [](std::pair<uint32, std::pair<uint32, WorldObject*>> i,
                 std::pair<uint32, std::pair<uint32, WorldObject*>> j) { return i.second.first > j.second.first; });

    // When every candidate has the same priority (CraftRandomItemAction
    // returns 1 for every recipe), the sort order above is arbitrary
    // and the "top quarter" restriction below picks a fixed, small
    // subset of a bot's recipes. For a bot with 100 recipes, only ~11
    // are ever attempted, which collapses the AH catalogue back down to
    // whichever recipes happen to land in that quarter. Detect the
    // uniform case and let the pick span the full list. The top-quarter
    // restriction is retained when priorities actually differ, so the
    // EnchantRandomItemAction priority-10 skill-up bias still works.
    bool prioritiesUniform = true;
    if (!spellList.empty())
    {
        uint32 const firstPriority = spellList.front().second.first;
        for (auto const& entry : spellList)
        {
            if (entry.second.first != firstPriority)
            {
                prioritiesUniform = false;
                break;
            }
        }
    }

    uint32 rndBound;
    if (prioritiesUniform)
        rndBound = static_cast<uint32>(spellList.size() - 1);
    else
    {
        rndBound = static_cast<uint32>(spellList.size() / 4);
        rndBound = std::min(rndBound, (uint32)10);
        rndBound = std::max(rndBound, (uint32)0);
    }

    for (uint32 i = 0; i < 5; i++)
    {
        uint32 rnd = urand(0, rndBound);

        auto spell = spellList[rnd];

        uint32 spellId = spell.first;
        WorldObject* wo = spell.second.second;

        bool isCast = castSpell(spellId, wo);

        if (isCast)
        {
            // Diagnostic: identify what was crafted. The action classes that
            // derive from CastRandomSpellAction are crafting-type actions
            // (craft random item, enchant random item, etc.). The prefix is
            // uniform so a single grep catches all of them.
            SpellInfo const* castInfo = sSpellMgr->GetSpellInfo(spellId);
            if (castInfo)
            {
                uint32 producedItemId = 0;
                for (uint8 effectIndex = 0; effectIndex < MAX_SPELL_EFFECTS; ++effectIndex)
                {
                    if (castInfo->Effects[effectIndex].Effect == SPELL_EFFECT_CREATE_ITEM &&
                        castInfo->Effects[effectIndex].ItemType)
                    {
                        producedItemId = castInfo->Effects[effectIndex].ItemType;
                        break;
                    }
                }

                if (producedItemId)
                {
                    ItemTemplate const* producedProto = sObjectMgr->GetItemTemplate(producedItemId);
                    LOG_DEBUG("playerbots",
                              "[CRAFT] {} cast {} producing [{}] {}",
                              bot->GetName(),
                              castInfo->Id,
                              producedItemId,
                              producedProto ? producedProto->Name1 : "<unknown>");

                    if (isCraftAction && sRandomPlayerbotMgr.IsRandomBot(bot))
                    {
                        uint32 existingGoal = sRandomPlayerbotMgr.GetValue(craftBotLow, "craft.goal.item");
                        if (existingGoal == 0)
                        {
                            ItemUsage pickedUsage = AI_VALUE2(ItemUsage, "item usage", producedItemId);
                            bool canPin = usageIsWorthGoal(pickedUsage);
                            // For AH-classified items, only pin when the AH
                            // can currently accept another listing. Without
                            // this, a bot whose random pick lands on a
                            // mass-crafted trade good already at the
                            // baseline per-item cap (Adamantite Sharpening
                            // Stone, Adamantite Weightstone, etc.) stays
                            // pinned to that item for the full 12-hour goal
                            // window, burning reagents on stacks the sell
                            // path rejects. Self-use, upgrade, quest, skill,
                            // ammo and disenchant crafts are still tracked
                            // unconditionally.
                            if (canPin && pickedUsage == ITEM_USAGE_AH)
                                canPin = AhCanAcceptItem(bot, producedItemId);
                            if (canPin)
                            {
                                sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.item", producedItemId);
                                sRandomPlayerbotMgr.SetValue(craftBotLow, "craft.goal.since",
                                                             static_cast<uint32>(time(nullptr)));
                            }
                        }
                    }
                }
                else
                {
                    LOG_DEBUG("playerbots",
                              "[CRAFT] {} cast {}",
                              bot->GetName(),
                              castInfo->Id);
                }
            }

            if (MultiCast && ((wo && bot->HasInArc(CAST_ANGLE_IN_FRONT, wo, sPlayerbotAIConfig.SightDistance))))
            {
                std::ostringstream cmd;
                cmd << "castnc " << chat->FormatWorldobject(wo) + " " << spellId << " " << 19;
                botAI->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), bot);
            }
            return true;
        }
    }

    return false;
}

bool CraftRandomItemAction::isUseful()
{
    if (bot->IsInCombat())
        return false;

    return botAI->HasSkill(SKILL_ALCHEMY) || botAI->HasSkill(SKILL_BLACKSMITHING) ||
           botAI->HasSkill(SKILL_ENCHANTING) || botAI->HasSkill(SKILL_ENGINEERING) ||
           botAI->HasSkill(SKILL_JEWELCRAFTING) || botAI->HasSkill(SKILL_LEATHERWORKING) ||
           botAI->HasSkill(SKILL_TAILORING) || botAI->HasSkill(SKILL_INSCRIPTION) ||
           botAI->HasSkill(SKILL_COOKING) || botAI->HasSkill(SKILL_FIRST_AID);
}

bool CraftRandomItemAction::AcceptSpell(SpellInfo const* spellInfo)
{
    return spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM && spellInfo->ReagentCount[EFFECT_0] > 0 &&
           spellInfo->SchoolMask == 1;
}

uint32 CraftRandomItemAction::GetSpellPriority(SpellInfo const* /*spellInfo*/)
{
    // Original mod-playerbots behaviour: uniform random among all recipes the
    // bot has learned and can currently cast. Recipe diversity across the
    // whole bot population is what produces the mixed AH catalogue (bags,
    // quivers, ammo pouches, potions, elixirs, flasks, robes, weapons, food)
    // that the priority tiers this fork added were unintentionally
    // suppressing in favour of high-volume recipes like bandages and bolts.
    //
    // Focus is preserved by the craft.goal mechanism inside
    // CastRandomSpellAction::Execute: once a bot commits to a target item, it
    // keeps working toward that item across visits, crafting missing reagents
    // first, so multi-step recipes (bag = bolts = cloth) still finish even
    // though the initial pick is random.
    return 1;
}


bool CastRandomSpellAction::castSpell(uint32 spellId, WorldObject* wo)
{
    if (wo->GetGUID().IsUnit())
        return botAI->CastSpell(spellId, (Unit*)(wo));
    else
        return botAI->CastSpell(spellId, wo->GetPositionX(), wo->GetPositionY(), wo->GetPositionZ());
}

bool DisEnchantRandomItemAction::Execute(Event /*event*/)
{
    std::vector<Item*> items =
        AI_VALUE2(std::vector<Item*>, "inventory items", "usage " + std::to_string(ITEM_USAGE_DISENCHANT));
    std::reverse(items.begin(), items.end());

    for (auto& item : items)
    {
        // Don't touch rare+ items if with real player/guild
        if ((botAI->HasGameClientMaster() || botAI->IsInRealGuild()) &&
            item->GetTemplate()->Quality > ITEM_QUALITY_UNCOMMON)
            return false;

        if (CastCustomSpellAction::Execute(
                Event("disenchant random item", "13262 " + chat->FormatQItem(item->GetEntry()))))
            return true;
    }

    return false;
}

bool DisEnchantRandomItemAction::isUseful()
{
    return botAI->HasSkill(SKILL_ENCHANTING) && !bot->IsInCombat() &&
           AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_DISENCHANT)) > 0;
}

bool EnchantRandomItemAction::isUseful() { return botAI->HasSkill(SKILL_ENCHANTING) && !bot->IsInCombat(); }

bool EnchantRandomItemAction::AcceptSpell(SpellInfo const* spellInfo)
{
    return spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_ENCHANT_ITEM && spellInfo->ReagentCount[EFFECT_0] > 0;
}

uint32 EnchantRandomItemAction::GetSpellPriority(SpellInfo const* spellInfo)
{
    if (spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_ENCHANT_ITEM)
    {
        if (AI_VALUE2(Item*, "item for spell", spellInfo->Id) && ItemUsageValue::SpellGivesSkillUp(spellInfo->Id, bot))
            return 10;
    }

    return 1;
}
