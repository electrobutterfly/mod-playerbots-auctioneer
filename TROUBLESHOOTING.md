![](icon.png)

# Troubleshooting Playerbots-Auctioneer

This document covers what to do when the auctioneer is not behaving as expected. It is written for two audiences:

- **Server operators** who want to understand what the bots are doing.
- **Bug reporters** who need to gather the right information before filing an issue.

The diagnostic mode is verbose. It is meant to be enabled for a specific investigation and disabled when the investigation is done.

------

## 1. Enabling diagnostic mode

Three steps.

### Step 1 — enable the diagnostic switch

In `env/etc/modules/playerbots_auction.conf`:

```bash
Playerbots.DebugAuction = 1
```



That is the minimum. For the very verbose per-sale filter breakdown — every item scanned, every reason it was rejected — also set:

```bash
Playerbots.Auction.RunAnalysis = 1
```



`RunAnalysis` is intended for tuning `MinSellQuality`, `MinSellValueCopper`, `SellFullStacksOnly` and the other sell filters. **Do not leave it on in production.**

### Step 2 — raise the logger level

`DebugAuction` sends its output through the `playerbots` logger. By default the logger is at level 4 (Info), which is too low to capture the `[AH-TRACE]` and `[BAG]` lines. Change it in `env/etc/worldserver.conf`:

```bash
Logger.playerbots=5,Console Playerbots
Appender.Playerbots=2,5,0,Playerbots.log,w
Appender.Console=1,5,0,"1 9 3 6 5 8"
```



Level `5` means Debug. `Appender.Playerbots` writes to `logs/Playerbots.log`. The logger line sends Debug output to both the console and the file — remove `Console` if you want file-only. The `Appender.Console` line raises the console appender's own level to Debug so `Console` in the logger line actually produces output.

Restart the worldserver (or run `.reload config`) for the changes to take effect.

### Step 3 — reproduce the problem

Let the server run until the bot behavior you want to investigate has happened. A single AH visit is usually enough. If the problem is "no bots visit the AH at all", let it run for at least an hour.

Once you have reproduced the issue, disable `DebugAuction` again so the log does not grow indefinitely.

------

## 2. What to grep for

All AH-specific output is prefixed with `[AH-TRACE]`. All bag-equip and bag-purchase output is prefixed with `[BAG]`. The most common queries:

```bash
# Live tail of everything AH-related
tail -f logs/Playerbots.log | grep '\[AH-TRACE\]'

# All activity for one bot
tail -f logs/Playerbots.log | grep '\[AH-TRACE\] Avuni'

# Every successful listing in the last hour
grep '\[AH-TRACE\].*listed \[' logs/Playerbots.log | tail -50

# Every relist attempt
grep 'relist:' logs/Playerbots.log | tail -100

# Every baseline reload
grep 'Auction baseline snapshot changed' logs/Playerbots.log

# Vendor visits only
grep 'arrived at vendor' logs/Playerbots.log | tail -20
grep 'vendoring \[' logs/Playerbots.log | tail -50

# Live tail of every bag equip, purchase, and displacement
tail -f logs/Playerbots.log | grep '\[BAG\]'

# All bag activity for one bot
grep '\[BAG\]' logs/Playerbots.log | grep 'Kanil'

# Every container a bot has bought from a vendor
grep '\[BAG\].*bought' logs/Playerbots.log | tail -20

# Every container a bot has displaced during a swap
grep '\[BAG\].*displaced' logs/Playerbots.log | tail -20

# Bag size distribution across the population
grep '\[BAG\].*equipped' logs/Playerbots.log \
    | sed -E 's/.*\(([0-9]+) slots\).*/\1/' \
    | sort -n | uniq -c
```



------

## 3. Reading the output

### Startup markers

Emitted once at boot when `Baseline.Mode = 2`:

```bash
>> Loaded N auctioneer baseline items, snapshot 'YYYY-MM-DD_HH'
>> Loaded N auctioneer baseline categories
```



If these are absent, either the mode is 1 or the tables are empty.

### Live reload marker

Emitted when the running server notices a fresh import without a restart:

```
>> Auction baseline snapshot changed (old -> new), reloading
```



### The full AH visit sequence

One bot, one visit, in order:

```
[AH-TRACE] <bot> approaching: distToPos=42.17 entry=XXXX lastReach=0
[AH-TRACE] <bot> found auctioneer <Name> (entry XXXX), distToNPC=4.83
[AH-TRACE] <bot> sending MSG_AUCTION_HELLO to <Name> (entry XXXX), dist=4.83
[AH-TRACE] <bot> listing read: N auctions on this AH
[AH-TRACE] <bot> reserved K reagents for craft arbitrage
[AH-TRACE] <bot> relist: M returned items in mail
[AH-TRACE] <bot> relist: M returned items, walking to mailbox
[AH-TRACE] <bot> relist: mailbox reached, collecting
[AH-TRACE] <bot> relist: collected M items, queued M for relist
[AH-TRACE] <bot> sell queue built: N items (M returned, K fresh)
[AH-TRACE] <bot> listed [ITEM_ID] xCOUNT bid=X buyout=Y dur=720 relist=yes
[AH-TRACE] <bot> listed [ITEM_ID] xCOUNT bid=X buyout=Y dur=720 relist=no
[AH-TRACE] <bot> bid/bought auctionId=X price=Y max=Z
[AH-TRACE] <bot> re-bid auctionId=X price=Y (max=Z)
[AH-TRACE] <bot> dropping tracked bid auctionId=X (item no longer wanted)
```



The `listed` line is the one to watch for a successful sale. `relist=yes` means the item came back from an expired auction and is being relisted; `relist=no` means a fresh listing from the bot's bags.

### The full vendor visit sequence

```
[AH-TRACE] <bot> approaching vendor: distToPos=12.34 entry=XXXX lastReach=0
[AH-TRACE] <bot> found vendor <Name> (entry XXXX), distToNPC=3.42
[AH-TRACE] <bot> arrived at vendor <Name> (entry XXXX), starting sell pass
[AH-TRACE] <bot> vendoring [ITEM_ID] NAME xCOUNT quality=Q usage=12
[AH-TRACE] <bot> vendor sell pass complete (sold anything=yes|no)
[AH-TRACE] <bot> disenchant pass complete (N successes)
```



Each `vendoring` line is one item leaving the bot's bags for a vendor. `usage=12` means the item was classified `ITEM_USAGE_VENDOR` — the bot had already decided it was not AH-worthy, not useful, not a quest item, and not protected.

### Bag progression markers

Bag equips, purchases and swaps are logged with the `[BAG]` prefix, independent of `DebugAuction`. They are emitted whenever the `playerbots` logger is at level 5 in `worldserver.conf`:

```
[BAG] Kanil equipped [3914] (14 slots) into bag slot 19
[BAG] Kanil displaced [805] (6 slots) from bag slot 19
[BAG] Kanil bought [4499] (12 slots) from vendor Cuzi for 95000 copper
```



- `bought` — a vendor purchase from the buy-container pass. Only appears at the end of a vendor visit, after the sell and disenchant passes.
- `equipped` — a container went into a bag slot, either freshly looted, crafted, or bought. If the slot was previously empty, this is the only line for that swap.
- `displaced` — the container that was in the target slot before this one. Only appears when the slot was previously occupied.

Under `Playerbots.RandomBotStarterBagMode = 1` most bots already have full bags, so the `[BAG]` stream is dominated by same-size replacements. Under mode 2 a fresh server shows a clear growth curve: slot 19 fills first, then 20, then 21, then 22, then replacements march left-to-right starting at slot 19 again.

### Per-sale filter breakdown

With `RunAnalysis = 1`, every sell decision emits:

```
[AH-TRACE] <bot> FindItemToSell cfg: minQuality=... minValue=... sellEnabled=...
[AH-TRACE] <bot> sellpick: scanned=N candidates=M rejTrade=... rejQuality=...
                         rejEquipped=... rejValue=... rejQuest=... rejBound=...
                         rejFullStack=... rejClassReagent=... rejUseful=...
                         rejPotion=... rejVendorSold=... rejExcluded=...
                         rejExhausted=... bags=[...]
[AH-TRACE] <bot> candidate pool: [ITEM_ID qQ cC] [ITEM_ID qQ cC] ...
```



The `sellpick` line is the single most useful diagnostic for "why is my bot not listing this item". Every rejection counter maps to one config setting:

| Counter           | Rejected by                                                  |
| :---------------- | :----------------------------------------------------------- |
| `rejTrade`        | Item is soulbound or otherwise cannot be traded              |
| `rejQuality`      | Below `Auction.MinSellQuality`                               |
| `rejEquipped`     | Item is currently equipped                                   |
| `rejValue`        | Below `Auction.MinSellValueCopper`                           |
| `rejQuest`        | Quest item and `ProtectQuestItems = 1`                       |
| `rejBound`        | Soulbound and `ProtectBoundItems = 1`                        |
| `rejFullStack`    | Partial stack and `SellFullStacksOnly = 1`                   |
| `rejClassReagent` | Class reagent and `ProtectClassReagents = 1`                 |
| `rejUseful`       | Bot considers it useful and `ProtectUsefulItems = 1`         |
| `rejPotion`       | Potion count is at or below the keep cap                     |
| `rejVendorSold`   | Stocked by an NPC vendor and `ExcludeVendorSoldItems = 1`    |
| `rejExcluded`     | Item is in `ExcludedItemIds`                                 |
| `rejExhausted`    | Item has hit `MaxRelistRetries` and is no longer auctionable |

The `candidate pool` line shows the top-5 highest-value items that survived every filter. The bot picks one of these at random — that is why the same bot does not always list the same item.

------

## 4. Sample troubleshooting walks

### "My bots are not visiting the AH at all."

```bash
grep '\[AH-TRACE\]' logs/Playerbots.log | head -5
```

If empty, either `DebugAuction = 0` or `Auction.Enabled = 0` or the bots have no reason to visit. Check the visit gates: `AuctionVisit.MinLevel`, `MinBagFillPercent`, `MinGoldCopper`. If those are all satisfied and the bots still do not visit, check `RpgStatusProbWeight.GoAuctioneer` — a value of 0 disables the status entirely.

Also check whether bots are standing at the AH for other reasons. A bot near an auctioneer without an `[AH-TRACE]` line is on a WanderNpc, quest-giver, or travel-hub path — it is not doing AH work, and the AH visit gates never fired for it.


### "My bots go to the AH but never list anything."

```bash
grep 'sellpick:' logs/Playerbots.log | grep -v 'candidates=[1-9]' | tail -20
```

That filters for `candidates=0`, meaning nothing survived the sell filters. Every `rej*` counter tells you which filter is the blocker. The most common ones on a fresh server are `rejValue` (items too cheap to list) and `rejUseful` (the bot wants to keep everything).


### "My bots list but never relist returned mail."

```bash
grep 'relist:' logs/Playerbots.log | tail -50
```

Watch for a bot that repeatedly shows `walking to mailbox` without ever reaching `mailbox reached`. That bot cannot path to any mailbox from its current position. The map may genuinely have no mailbox within reach, or the bot may be stuck against geometry.


### "My bots are listing at absurd prices."

```bash
grep 'listed \[' logs/Playerbots.log | tail -20
```

Compare the `buyout=` values against what you expect for the item. If they are wildly high or low, either `Baseline.Mode = 1` (legacy pricing, no reference) or the baseline row for those items is thin. Verify with:

```sql
SELECT item_id, price_samples, price_low, price_median, price_high
FROM acore_characters.auctioneer_baseline_item
WHERE faction = 2 AND item_id IN (<the-item-ids-from-the-log>);
```

If `price_samples` is very low for those items, they are edge cases that the reference market barely saw, and the bot's draw from the band will be noisy.


### "No gear is appearing on the AH."

Expected. The AH only accepts **BoE** gear that is **not an upgrade** for the looter. Gear that is equipped becomes soulbound and can never reach the AH. Gear that is a quest reward or an elite drop is usually BoP by design. The remaining slice — a tradeable green that is not an upgrade — is a small fraction of total loot at every level.

If you want more gear on the AH:

- Set `Playerbots.Auction.InitSpareInventory = 1`. This seeds a few spare BoE items into each bot's bags during periodic randomization. The effect appears over 24–48 hours as the randomize wave sweeps the population.

- Lower `Playerbots.Auction.MinSellValueCopper`. The default of 25 lets low-level greens through; higher values filter them out.

- Raise `Playerbots.RpgStatusProbWeight.GoGrind`. Grinding produces more tradeable loot than questing.

  

### "Bots are not selling their soulbound gear to a vendor."

Check first that the feature is running:

```bash
grep -c 'arrived at vendor' logs/Playerbots.log
grep -c 'vendoring \[' logs/Playerbots.log
```



If arrivals are zero, either `Auction.Enabled = 0`, or `VendorVisit.MinLevel` is higher than your bot population, or bots cannot reach a vendor. Check `SelectVendorByMap` results by grepping for a bot's `GO_VENDOR` status.

If arrivals are nonzero but `vendoring [` is zero, the sell pass is firing but `SellVendorItemsVisitor` is finding nothing to sell. That would point to a classification issue in `ItemUsageValue`, and the `sellpick` output for those bots will tell you why.



### "The bot appears to walk through walls."

That is not an auctioneer issue — it is a general mod-playerbots movement characteristic. Pathfinding can produce splines that cut through thin geometry or through doorways that are not visible from the observer's angle. The auctioneer does not affect movement.



### "Bots are competing with each other (or with themselves) on the same listing."

The pricing code excludes the bot's own listings from the market reference, so a bot listing multiple stacks of the same item will not undercut itself. If you still see multiple listings at wildly different prices for the same item, check the `listed` lines for those visits — the two prices should be within the undercut band of each other, not a clean gap.



### "Bots don't seem to be upgrading their bags."

First confirm the pipeline is running at all:

```bash
grep -c '\[BAG\]' logs/Playerbots.log
```

If that is zero on a server that has been up for more than an hour, the bot population is not looting any containers. Check:

- `Playerbots.Loot.BagMinSlots` — surplus containers below this size are left on the corpse. This does not affect the equip path (a small container that upgrades a bot's bag is still looted and equipped), it only changes what happens to a container that would have been surplus.
- `Playerbots.RandomBotStarterBagMode` — under either mode, a Death Knight starts at 12×4 and can only upgrade with a 14+ slot container. That is rare as a mob drop. Non-DK classes at low level grow through the full 6 → 8 → 10 → 12 curve because those sizes are common drops.
- Zone level range — if bots are grinding level-appropriate zones, small containers drop at a normal rate. If the whole population is bunched into a small level range, the drop table is thinner than average.

If bags are being equipped but the population stays at small sizes, that is the drop-table limit, not a bug. The two levers are:

- Raise `Playerbots.Auction.BuyScanChancePercent` so more bots buy bag upgrades from the AH.

- Wait for the bot population's profession progression to unlock the higher-tier bag recipes. Bag recipes unlock at fixed skill thresholds (Runecloth Bag at Tailoring 260, Frostweave Bag at 350, etc.), so the population's average crafting skill determines the average bag size.

  

### "A bot bought a bag from a vendor but the database shows no bag on it."

Almost certainly a save-timing gap, not a bug. `SwapItem` writes to the in-memory container, not to `character_inventory`. The character row only updates on `SaveToDB`, which fires on logout, on randomize, or on a periodic save. A freshly-equipped bot can lag behind the database by several minutes.

Retry the query after a minute, or whisper the bot `refresh` to force a save. The query itself is:

```sql
SELECT ci.slot, it.entry, it.name, it.ContainerSlots
FROM acore_characters.character_inventory ci
JOIN acore_characters.characters c           ON c.guid = ci.guid
JOIN acore_characters.item_instance ii       ON ii.guid = ci.item
JOIN acore_world.item_template it            ON it.entry = ii.itemEntry
WHERE c.name = '<botname>'
  AND ci.bag = 0
  AND ci.slot BETWEEN 19 AND 22
ORDER BY ci.slot;
```



If the query returns nothing after a minute and after a `refresh`, check that the bot name matches exactly (case-sensitive, no trailing space) by running `SELECT guid, name FROM acore_characters.characters WHERE name LIKE '%<partial>%';` first.



### "My altbot is not visiting the auctioneer."

**That is the intended behavior when `Playerbots.Auction.AllowAltBots = 0` (the default). Altbots and addclass bots are excluded from the AH automation pipeline to prevent players from running unattended AH farming on their own characters.**

To confirm the block is in effect, grep the log for the bot's name with the AH trace prefix:

```bash
grep '\[AH-TRACE\]' logs/Playerbots.log | grep '<botname>'
```

If the bot produces no lines at all while clearly playing (grinding, looting, visiting vendors), the block is active. If it produces `approaching: distToPos=...` lines but never reaches the auctioneer, something else is wrong — that is a movement or pathing issue, not the altbot block.

To verify the bot is otherwise behaving normally, check its vendor visits and bag purchases:

```bash
grep '\[BAG\]' logs/Playerbots.log | grep '<botname>'
grep 'arrived at vendor' logs/Playerbots.log | grep '<botname>'
```

Vendor visits and vendor bag purchases are not gated by `AllowAltBots`; only the AH is. If you see vendor activity but no AH activity, the block is working as designed.


### "Hunters are not upgrading their quivers."

Two things must be true: the hunter must have picked up a bigger quiver (from loot, a vendor purchase, or the auction house), and the equip pipeline must accept the swap. Both are in place — `ItemUsageValue` accepts `ITEM_SUBCLASS_QUIVER` for hunters, `CraftRandomItemAction` allows Leatherworking bots to craft quivers of every size they know, and `BuyContainerAction` searches vendor and AH listings for larger quivers along with bags and pouches.

To trace a specific hunter's quiver upgrades:

```bash
grep '\[BAG\]' logs/Playerbots.log | grep 'quiver'
```

If nothing appears for a hunter that has clearly looted a quiver, check that it is a hunter (quivers are rejected for every other class) and that the quiver in question is larger than the hunter's smallest equipped container. A hunter with `[quiver(6), bag(10), bag(10), bag(10)]` who finds a 12-slot bag will displace the 6-slot quiver with the bag, because both are containers and the size comparison is size-only. On WotLK this is functionally equivalent (ammo was removed in 3.0 and quivers no longer provide a ranged haste bonus), so no fix is needed unless you want quivers to be sticky for hunters specifically.

------

## 5. Diagnostic bundle for bug reports

If you are filing a bug report against the auctioneer, attach all of the following in one message. This is almost always enough to reproduce the problem locally.

### 5.1 Attach these files

- `env/etc/modules/playerbots_auction.conf`
- `env/etc/modules/playerbots.conf` (only the `Playerbots.*` keys that are not defaults)
- `env/etc/worldserver.conf` (only the `Logger.playerbots*` and `Appender.Playerbots*` lines)
- `modules/mod-playerbots-auctioneer/auctioneer_baseline/auctioneer.conf`

### 5.2 Run these commands and attach the output

```bash
# Counts of the main events
grep -c 'sell queue built' logs/Playerbots.log
grep -c 'listed \[' logs/Playerbots.log
grep -c 'baseline policy rejected' logs/Playerbots.log
grep -c 'craft arbitrage' logs/Playerbots.log
grep -c 'arrived at vendor' logs/Playerbots.log
grep -c 'vendoring \[' logs/Playerbots.log
grep -c 'disenchant pass complete' logs/Playerbots.log
grep -c 'relist: walking to mailbox' logs/Playerbots.log
grep -c 'relist: mailbox reached, collecting' logs/Playerbots.log
grep -c '\[BAG\].*bought' logs/Playerbots.log
grep -c '\[BAG\].*equipped' logs/Playerbots.log
grep -c '\[BAG\].*displaced' logs/Playerbots.log

# Item IDs currently being listed and vendored
grep 'listed \[' logs/Playerbots.log | awk '{print $4}' | sort | uniq -c | sort -rn | head -40
grep 'vendoring \[' logs/Playerbots.log | awk '{print $4}' | sort | uniq -c | sort -rn | head -20

# Per-bot trace for the bot that is exhibiting the problem
grep 'BOTNAME' logs/Playerbots.log | tail -200
```

Replace `BOTNAME` with the character name of a bot that showed the issue.


### 5.3 Run this SQL and attach the output

```sql
SELECT snapshot FROM acore_characters.auctioneer_baseline_item LIMIT 1;
SELECT COUNT(*) FROM acore_characters.auctioneer_baseline_item;
SELECT COUNT(*) FROM acore_characters.auctioneer_baseline_category;
SELECT COUNT(*) FROM acore_characters.auctioneer_baseline_history;
```



### 5.4 Describe the problem

In one or two sentences, describe what you expected and what you observed. Screenshots of the AH interface are useful if the issue is about pricing or listing composition.

------

## 6. Turning diagnostics off

When you are done investigating:

```bash
Playerbots.DebugAuction = 0
Playerbots.Auction.RunAnalysis = 0
```



and revert the `Logger.playerbots` level in `worldserver.conf` back to `4` if you had raised it. `.reload config` picks up both. The log file will no longer receive new `[AH-TRACE]` lines but will keep whatever it already contains. The `[BAG]` lines stop when the `playerbots` logger drops back to level 4; they are not gated by `DebugAuction`.

------

Copyright (C) 2026, Klaus Simon aka [Electrobutterfly](https://github.com/electrobutterfly)

<img src="https://electrobutterfly.com/images/logo-small-github.png" alt="Logo" style="float:left; margin-right:10px; width:150px;">
