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

`DebugAuction` sends its output through the `playerbots` logger. By default the logger is at level 4 (Info), which is too low to capture the `[AH-TRACE]` lines. Change it in `env/etc/worldserver.conf`:

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

All AH-specific output is prefixed with `[AH-TRACE]`. The most common queries:

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

```
grep '\[AH-TRACE\]' logs/Playerbots.log | head -5
```



If empty, either `DebugAuction = 0` or `Auction.Enabled = 0` or the bots have no reason to visit. Check the visit gates: `AuctionVisit.MinLevel`, `MinBagFillPercent`, `MinGoldCopper`. If those are all satisfied and the bots still do not visit, check `RpgStatusProbWeight.GoAuctioneer` — a value of 0 disables the status entirely.

Also check whether bots are standing at the AH for other reasons. A bot near an auctioneer without an `[AH-TRACE]` line is on a WanderNpc, quest-giver, or travel-hub path — it is not doing AH work, and the AH visit gates never fired for it.

### "My bots go to the AH but never list anything."

```
grep 'sellpick:' logs/Playerbots.log | grep -v 'candidates=[1-9]' | tail -20
```



That filters for `candidates=0`, meaning nothing survived the sell filters. Every `rej*` counter tells you which filter is the blocker. The most common ones on a fresh server are `rejValue` (items too cheap to list) and `rejUseful` (the bot wants to keep everything).

### "My bots list but never relist returned mail."

```
grep 'relist:' logs/Playerbots.log | tail -50
```



Watch for a bot that repeatedly shows `walking to mailbox` without ever reaching `mailbox reached`. That bot cannot path to any mailbox from its current position. The map may genuinely have no mailbox within reach, or the bot may be stuck against geometry.

### "My bots are listing at absurd prices."

```
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

```
grep -c 'arrived at vendor' logs/Playerbots.log
grep -c 'vendoring \[' logs/Playerbots.log
```



If arrivals are zero, either `Auction.Enabled = 0`, or `VendorVisit.MinLevel` is higher than your bot population, or bots cannot reach a vendor. Check `SelectVendorByMap` results by grepping for a bot's `GO_VENDOR` status.

If arrivals are nonzero but `vendoring [` is zero, the sell pass is firing but `SellVendorItemsVisitor` is finding nothing to sell. That would point to a classification issue in `ItemUsageValue`, and the `sellpick` output for those bots will tell you why.

### "The bot appears to walk through walls."

That is not an auctioneer issue — it is a general mod-playerbots movement characteristic. Pathfinding can produce splines that cut through thin geometry or through doorways that are not visible from the observer's angle. The auctioneer does not affect movement.

### "Bots are competing with each other (or with themselves) on the same listing."

The pricing code excludes the bot's own listings from the market reference, so a bot listing multiple stacks of the same item will not undercut itself. If you still see multiple listings at wildly different prices for the same item, check the `listed` lines for those visits — the two prices should be within the undercut band of each other, not a clean gap.

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



and revert the `Logger.playerbots` level in `worldserver.conf` back to `4` if you had raised it. `.reload config` picks up both. The log file will no longer receive new `[AH-TRACE]` lines but will keep whatever it already contains.
