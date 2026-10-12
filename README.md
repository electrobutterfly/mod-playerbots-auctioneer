<p align="center">
    <a href="https://github.com/mod-playerbots/azerothcore-wotlk">Playerbots-Auctioneer</a>
    |
    <a href="https://github.com/NathanHandley/mod-ah-bot-plus">AH Bot Plus</a>
    |
    <a href="https://github.com/mod-playerbots/azerothcore-wotlk">Azerothcore-Playerbots</a>
</p>
<div align="center">
  <img src="banner.jpg" alt="Playerbots Banner" width="700px">
</div>


# Playerbots-Auctioneer

`Playerbots-Auctioneer` is a heavily modified [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) fork which provides an auction-house subsystem for AzerothCore.

Out of the box, mod-playerbots bots do not touch the Auction House. They do not buy, they do not sell, they do not bid, they do not walk to an auctioneer, they do not collect mail. On a playerbot-only server the AH is populated by whatever the server operator places there by hand, or by mod-ah-bot-plus if it is enabled — it is not an economy driven by the bot population.

`Playerbots-Auctioneer` gives the bot population a complete auction-house economy of its own. Every bot walks to the AH, sells his by his professions crafted stuff and surplus inventory, buys items it needs, bids on and defends listings it wants, collects expired mail, and relists returned items — the same lifecycle a human player follows. The economy runs **autonomously**: no human players are required for the AH to fill up, and the buyer and seller features of mod-ah-bot-plus do not need to be enabled for the bots to trade with each other.

---

## Features

- **Automated selling.** Bots decide which inventory items are worth listing, price them against the live market and (optionally) a reference market, undercut competing listings by a bounded percentage, and list them for a configurable duration.
- **Automated buying.** On a configurable per-cycle chance, bots scan the AH for gear upgrades, larger bags, consumables, profession materials, and quest items they need, and buy or bid up to a willingness threshold.
- **Bid tracking.** Bids placed by bots are persisted. If a bot is outbid, it re-evaluates whether it still wants the item before defending the bid. Items that are no longer useful are dropped instead of chased up to the original cap.
- **Mail collection.** Bots walk to a mailbox when they have returned auction mail, collect the items, and relist them — up to a per-item retry cap, after which the item falls through to the normal vendor or destroy path.
- **Reference pricing.** A per-item price band (`price_low` / `price_median` / `price_high`) sourced from a live market. Because a bot-only economy has no outside liquidity, without a reference the bots race each other to the vendor floor within days. The reference band seeds prices on an empty market, floors undercutting so prices cannot walk into the ground, and caps how many concurrent listings of the same item can exist.
- **Starting-bid ratios.** The observed MINBID/BUYOUT ratio from the reference market, used to set a realistic starting bid instead of always halving the buyout.
- **Craft arbitrage.** When a bot knows a recipe whose output is worth more than its reagent cost at reference prices, the reagents are reserved instead of being dumped at material price.
- **Multi-snapshot trend.** Prices nudge up or down within the reference band based on how the reference market has moved over a retention window (default 90 days).
- **Fully optional.** With the reference tables empty or missing, the auctioneer falls back to legacy pricing with no error spam and no crash. The two modes are selected by a single config key.

### Relationship with mod-ah-bot-plus

mod-ah-bot-plus is installed as a dependency because the auctioneer's buyer and seller logic is benchmarked against the same `CalculateItemValue` implementation that mod uses. But the AH bot's own buyer and seller features can be independently enabled or disabled — the playerbots-auctioneer provides the buyer and seller behavior for the bot population itself, and works with those features off.

The AH bot GUIDs from mod-ah-bot-plus are used only to classify sellers for the buyer-preference logic (`AiPlayerbot.Auction.AhBotGUIDs`), so the bots know the difference between "another playerbot" and "the dedicated AH bot". If you are not running mod-ah-bot-plus, leave that config key empty and the classification falls back to `Unknown`.



### Bots sometimes seem to "fly or go through wall"

That is not an issue of playerbots-auctioneer but the underlying [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)



---

## Installation

### 1. AzerothCore Playerbots

Clone the AzerothCore playerbots fork:

```bash
git clone https://github.com/mod-playerbots/azerothcore-wotlk.git --branch=Playerbot
```

### 2. Modules

The auctioneer depends on the mod-ah-bot-plus module. Install both inside the `modules/` directory:

```bash
cd azerothcore-wotlk/modules

# Playerbots-Auctioneer — the auction-house subsystem (this module)
git clone https://github.com/electrobutterfly/mod-playerbots-auctioneer.git

# AH Bot Plus — the auction-house bot mod the auctioneer is benchmarked
# against. Pinned to a specific revision for compatibility.
git clone https://github.com/NathanHandley/mod-ah-bot-plus.git
cd mod-ah-bot-plus
git checkout f685832994c825f90aa5a3dc0e1620aa568e875b
cd ..
```

### 3. Build and install

Build the worldserver following the standard AzerothCore build process. Everything the auctioneer needs is compiled in automatically — there is no separate build step and no extra CMake target.

### 4. Configuration

Copy the distribution config to its live name and edit it:

```bash
cd env/etc/modules
cp playerbots_auction.conf.dist playerbots_auction.conf
$EDITOR playerbots_auction.conf
```



The two settings you should change:

```bash
AiPlayerbot.Auction.Enabled = 1
AiPlayerbot.Auction.Baseline.Mode = 2
```

`Auction.Enabled` is the master switch. `Baseline.Mode = 2` tells the bots to consult the reference table rather than falling back to legacy pricing. Both are safe to change at any time; `.reload config` applies them without a restart.

The full file is documented inline. Every setting has a comment describing what it does, what values it accepts, and what the default means.

### 5. Configure the baseline tool

The baseline pipeline ships inside the module. Its config example lives at `auctioneer_baseline/auctioneer.conf.example`. Copy it, edit the database credentials, and set an absolute path for `work_dir`:

```bash
cd modules/mod-playerbots-auctioneer/auctioneer_baseline
cp auctioneer.conf.example auctioneer.conf
$EDITOR auctioneer.conf

sudo mkdir -p /var/lib/auctioneer-baseline
sudo chown $USER /var/lib/auctioneer-baseline
```



The config has five sections:

- `[mysql]` — shared defaults for the two database connections below.
- `[world]` — read-only. Used to read `item_template` for the item filter and the (class, quality) classification.
- `[character]` — read-write. Where the baseline tables live.
- `[source]` — the Auctioneer mirror URL. Change this only if you point at a different realm.
- `[paths]` — where the tool downloads files and writes its intermediate SQL. Use an absolute path; a relative path resolves against the process working directory, which is unpredictable under cron.

`[world]` and `[character]` each inherit any value not set from `[mysql]`. If your world and character databases live on the same host under the same user, set it once in `[mysql]` and leave the two specific sections with only the `database` key set. If they live on different hosts or under different users, override them individually — the tool uses two fully independent MySQL connections and never assumes they share credentials.

### 6. Seed the baseline

Create the tables (idempotent — safe to run more than once, and only needed if your install does not apply them automatically):

```bash
python3 auctioneer.py schema | mysql -u acore -p acore_characters
```



Run the full pipeline once by hand:

```bash
python3 auctioneer.py run
```



This downloads both faction Lua files (~30 MB total), parses them, filters to items present in your `item_template`, computes per-item stats, writes `baseline.sql`, and pipes it into `acore_characters`. Takes 30–60 seconds and 200–500 MB peak memory.

When it finishes, the reference tables are populated and the running worldserver will pick them up on its next reload check — no restart needed.

### 7. Schedule the baseline refresh

The bundled baseline is a snapshot in time. To keep it fresh, run the Python tool on a timer. It can run while the worldserver is up — the running server notices new data within `Baseline.ReloadCheckMinutes` and reloads its in-memory caches without a restart.

*Option A — systemd (recommended):*

```bash
cd modules/mod-playerbots-auctioneer/auctioneer_baseline
sudo cp systemd/auctioneer.service systemd/auctioneer.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now auctioneer.timer
```



*Option B — cron:*

```bash
cd modules/mod-playerbots-auctioneer/auctioneer_baseline
sudo cp cron/auctioneer.cron /etc/cron.d/auctioneer
```



Both fire the same command every 12 hours. 12 hours matches how often the source mirror updates — running more often than that is wasted bandwidth.

If you placed the tool directory somewhere other than the module tree, edit the `ExecStart=` line in the systemd unit (or the command line in the cron file) to match. The README inside `systemd/` and `cron/` documents the two variables you might need to change: the path to the tool and the path to the config file.

### 8. Verify the install

Three quick checks:

```bash
# 1. Tables exist and have data
mysql -u acore -p acore_characters -e \
  "SELECT COUNT(*) AS items FROM auctioneer_baseline_item; \
   SELECT COUNT(*) AS categories FROM auctioneer_baseline_category;"

# 2. The worldserver loaded them at boot
grep 'auctioneer baseline' logs/Playerbots.log

# 3. Bots are visiting the AH
grep '\[AH-TRACE\]' logs/Playerbots.log | tail -20
```



If (1) shows zero rows, run `python3 auctioneer.py run` once by hand and recheck.

If (2) shows nothing, the config has `Baseline.Mode = 1`. Set it to `2` and reload.

If (3) shows nothing, either `DebugAuction` is off (see next section) or the bots are not choosing AH visits — check `RpgStatusProbWeight.GoAuctioneer` and the visit gates in the config.



## How the baseline works

Because a bot-only economy has no outside liquidity, the bots on a fresh server would otherwise price everything from `vendor × 5` and walk the market for each item down to the vendor floor within days. The baseline gives them a **reference market** to price against — a per-item price band sourced from a live Auctioneer scan of a large, mature realm.

The tool downloads two SavedVariables dumps (one per faction), parses out per-item price, stack-size, listing-count and bid-ratio statistics, and writes them into three tables in `acore_characters`:

- **`auctioneer_baseline_item`** — one row per `(faction, item_id, suffix)`. Carries the price band, stack statistics, listing depth, bid ratio, and multi-snapshot trend. This is the table the C++ side reads.
- **`auctioneer_baseline_category`** — one row per `(faction, class, quality)`. Describes how many auctions of a given item class and quality are live on the reference market at once. Used by the sell path to keep the local market's category mix looking real.
- **`auctioneer_baseline_history`** — one row per `(faction, item_id, suffix, snapshot_time)`. Every refresh appends a new row. The trend column on the item table is computed from the oldest retained history row vs. the current run. Rows older than the retention window are pruned on each refresh.

### What the C++ side does with it

When a bot is about to list an item, it looks up the baseline row for the `(faction, item_id, suffix)`. If found and the sample size is sufficient:

- The listing price is **drawn from the reference band** — a triangular distribution across `[price_low, price_high]` peaking at `price_median` — rather than falling through to `vendor × 5`. Different bots list at different prices, and the AH looks like a real market instead of a wall of identical listings.
- The result is **floored at `price_low`** so no undercut can walk it below the bottom of the reference band.
- The result is **capped at `price_high`** so no bot lists at an unreasonable price on a thin-sample item.
- The **starting bid** is set to the reference MINBID/BUYOUT ratio (per-item when the sample is reliable, class-level otherwise).
- The **number of concurrent listings** for that item is capped at the reference's typical depth, so one bot population cannot flood the AH with the same item.
- The **trend column** nudges the draw up or down within the band based on how the reference market has moved.

Every step degrades gracefully: if a row does not exist for a given item, the bot falls through to legacy behavior for that item only. If the whole table is missing, every bot falls through. There is no hard dependency at any point.

### Freshness

The worldserver polls the baseline table every `Baseline.ReloadCheckMinutes` (default 5). The poll is a single-row aggregate query on a small table and costs microseconds. When the newest snapshot tag differs from the currently loaded one, the worldserver re-reads both tables in place — a few hundred milliseconds — and the bots use the new values on their next AH decision. No restart, no service interruption, no bot state reset.

------

## Enabling debug mode

The auctioneer has a detailed diagnostic mode for tracking what individual bots are doing. It is off by default because it produces a lot of output per AH visit. It is intended for troubleshooting, tuning, and understanding bot behavior — **not for leaving on permanently**.

### Step 1 — enable the diagnostic switch

In `env/etc/modules/playerbots_auction.conf`:

```bash
AiPlayerbot.DebugAuction = 1
```



That is the minimum. For the very verbose per-sale filter breakdown — every item scanned, every reason it was rejected — also set:

```bash
AiPlayerbot.Auction.RunAnalysis = 1
```



`RunAnalysis` is intended for tuning `MinSellQuality`, `MinSellValueCopper`, `SellFullStacksOnly` and the other sell filters. Do not leave it on in production.



### Step 2 — raise the logger level

`DebugAuction` sends its output through the `playerbots` logger. By default the logger is at level 4 (Info), which is too low to capture the `[AH-TRACE]` lines. Change it in `env/etc/worldserver.conf`:

```bash
Logger.playerbots=5,Console Playerbots
Appender.Playerbots=2,5,0,Playerbots.log,w
Appender.Console=1,5,0,"1 9 3 6 5 8"
```



Level `5` means Debug. `Appender.Playerbots` writes to `logs/Playerbots.log`. The logger line sends Debug output to both the console and the file — remove `Console` if you want file-only. The `Appender.Console` line raises the console appender's own level to Debug so `Console` in the logger line actually produces output.

Restart the worldserver (or run `.reload config`) for the changes to take effect.

### Step 3 — what to grep for

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
```



### Reading the output

**Startup markers.** Emitted once at boot when `Baseline.Mode = 2`:

```
>> Loaded N auctioneer baseline items, snapshot 'YYYY-MM-DD_HH'
>> Loaded N auctioneer baseline categories
```



If these are absent, either the mode is 1 or the tables are empty.

**Live reload marker.** Emitted when the running server notices a fresh import without a restart:

```
>> Auction baseline snapshot changed (old -> new), reloading
```



**The full AH visit sequence.** One bot, one visit, in order:

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
[AH-TRACE] <bot> listed [ITEM_ID] xCOUNT bid=X buyout=Y dur=720 relist=yes
[AH-TRACE] <bot> listed [ITEM_ID] xCOUNT bid=X buyout=Y dur=720 relist=no
[AH-TRACE] <bot> bid/bought auctionId=X price=Y max=Z
[AH-TRACE] <bot> re-bid auctionId=X price=Y (max=Z)
[AH-TRACE] <bot> dropping tracked bid auctionId=X (item no longer wanted)
```



The `listed` line is the one to watch for a successful sale. `relist=yes` means the item came back from an expired auction and is being relisted; `relist=no` means a fresh listing from the bot's bags.

The mailbox walk sequence (`walking to mailbox` → `mailbox reached, collecting`) indicates the bot temporarily left the auctioneer, collected returned items, and will walk back. If the sequence starts but never reaches `mailbox reached`, either the bot cannot path to a mailbox or the visit's 5-minute timeout fired before it got there.

**Per-sale filter breakdown.** With `RunAnalysis = 1`, every sell decision emits:

```
[AH-TRACE] <bot> FindItemToSell cfg: minQuality=... minValue=... sellEnabled=...
[AH-TRACE] <bot> sellpick: scanned=N candidates=M rejTrade=... rejQuality=...
                         rejEquipped=... rejValue=... rejQuest=... rejBound=...
                         rejFullStack=... rejClassReagent=... rejUseful=...
                         rejPotion=... rejVendorSold=... rejExcluded=... bags=[...]
[AH-TRACE] <bot> candidate pool: [ITEM_ID qQ cC] [ITEM_ID qQ cC] ...
```



The `sellpick` line is the single most useful diagnostic for "why is my bot not listing this item". Every rejection counter maps to one config setting:

| Counter           | Rejected by                                               |
| :---------------- | :-------------------------------------------------------- |
| `rejTrade`        | Item is soulbound or otherwise cannot be traded           |
| `rejQuality`      | Below `Auction.MinSellQuality`                            |
| `rejEquipped`     | Item is currently equipped                                |
| `rejValue`        | Below `Auction.MinSellValueCopper`                        |
| `rejQuest`        | Quest item and `ProtectQuestItems = 1`                    |
| `rejBound`        | Soulbound and `ProtectBoundItems = 1`                     |
| `rejFullStack`    | Partial stack and `SellFullStacksOnly = 1`                |
| `rejClassReagent` | Class reagent and `ProtectClassReagents = 1`              |
| `rejUseful`       | Bot considers it useful and `ProtectUsefulItems = 1`      |
| `rejPotion`       | Potion count is at or below the keep cap                  |
| `rejVendorSold`   | Stocked by an NPC vendor and `ExcludeVendorSoldItems = 1` |
| `rejExcluded`     | Item is in `ExcludedItemIds`                              |

The `candidate pool` line shows the top-5 highest-value items that survived every filter. The bot picks one of these at random — that is why the same bot does not always list the same item.

### Sample troubleshooting walks

**"My bots are not visiting the AH at all."**

```bash
grep '\[AH-TRACE\]' logs/Playerbots.log | head -5
```



If empty, either `DebugAuction = 0` or `Auction.Enabled = 0` or the bots have no reason to visit. Check the visit gates: `AuctionVisit.MinLevel`, `MinBagFillPercent`, `MinGoldCopper`. If those are all satisfied and the bots still do not visit, check `RpgStatusProbWeight.GoAuctioneer` — a value of 0 disables the status entirely.

**"My bots go to the AH but never list anything."**

```bash
grep 'sellpick:' logs/Playerbots.log | grep -v 'candidates=[1-9]' | tail -20
```



That filters for `candidates=0`, meaning nothing survived the sell filters. Every `rej*` counter tells you which filter is the blocker. The most common ones on a fresh server are `rejValue` (items too cheap to list) and `rejUseful` (the bot wants to keep everything).

**"My bots list but never relist returned mail."**

```bash
grep 'relist:' logs/Playerbots.log | tail -50
```



Watch for a bot that repeatedly shows `walking to mailbox` without ever reaching `mailbox reached`. That bot cannot path to any mailbox from its current position. The map may genuinely have no mailbox within reach, or the bot may be stuck against geometry.

**"My bots are listing at absurd prices."**

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

------

## Configuration reference

The full configuration file lives at `modules/mod-playerbots-auctioneer/conf/playerbots_auction.conf.dist` (path depends on where the module was cloned), but it should automatically be copied to its server configuration into the modules directory when you run `make install`  after compiling the core. Every setting has an inline comment describing what it does, what values it accepts, and what the default means. The most commonly tuned settings:

| Setting                                       | Purpose                                                |
| :-------------------------------------------- | :----------------------------------------------------- |
| `Auction.Enabled`                             | Master switch for the whole subsystem                  |
| `Baseline.Mode`                               | 1 = legacy pricing, 2 = reference pricing              |
| `Auction.VisitMinMinutes` / `VisitMaxMinutes` | How often each bot returns to the AH                   |
| `Auction.MinSellQuality`                      | Lowest item quality that will be listed                |
| `Auction.SellUndercutPercentMin` / `Max`      | How aggressively bots undercut live listings           |
| `Auction.BuyScanChancePercent`                | Chance per RPG cycle that a bot scans the AH for buys  |
| `Auction.MaxActiveListingsPerBot`             | Per-bot cap on concurrent listings                     |
| `Baseline.ReloadCheckMinutes`                 | How often the running server checks for a new baseline |
| `Baseline.CraftArbitrageEnabled`              | Reserve reagents for profitable crafts                 |
| `Baseline.CraftMarginPercent`                 | Minimum margin before a craft is considered profitable |

### Modes explained

`Baseline.Mode` is the single switch that decides whether the reference table is consulted at all:

- **`1`** — legacy. Pricing is driven entirely by the local market: the cheapest live listing for the item, undercut by a bounded percentage, floored at vendor price. Starting bids are always `buyout / 2`. This is the behavior that shipped before the reference tables existed.
- **`2`** — reference. The bot consults the baseline for the item. If a row exists and is not too thin, prices are drawn from the reference band, floored and capped, startbids come from the observed ratio, and the concurrent listing cap applies. If no row exists (item never seen on the reference market), the bot falls through to Mode 1 behavior for that item only.

Mode 2 is a superset of Mode 1. There is no scenario where Mode 2 is worse than Mode 1, and no scenario where it breaks: with an empty baseline table, Mode 2 behaves exactly like Mode 1.

------

## The baseline tool in detail

### Commands

The Python tool has six subcommands:

| Command    | What it does                                              |
| :--------- | :-------------------------------------------------------- |
| `run`      | Full pipeline: download, parse, write SQL, insert         |
| `download` | Download both Lua files to `work_dir/scans/`              |
| `parse`    | Parse the Lua files, write `work_dir/baseline.sql`        |
| `insert`   | Pipe `work_dir/baseline.sql` into the character database  |
| `schema`   | Print the `CREATE TABLE` statements (for a fresh install) |
| `clean`    | Remove `work_dir/scans/*.lua` and `work_dir/baseline.sql` |

`run` is what the timer should call. The other five exist so you can break the pipeline apart for debugging — for example, run `parse` alone and inspect `baseline.sql` before committing it to the database.

### What `parse` produces

The intermediate `baseline.sql` file is a self-contained SQL script: `START TRANSACTION`, `DELETE FROM` both target tables, chunked `INSERT`s for the new data, `INSERT IGNORE` for the history append, an old-history prune, and `COMMIT`. Running it against `acore_characters` is transaction-safe — if any part fails, the whole thing rolls back and the previous baseline stays live.

### The two MySQL connections

The tool uses two independent connections:

- **`[world]`** — read-only. One query: `SELECT entry, class, Quality FROM item_template`. This is the source of the item filter (only items your realm actually has are imported) and the (class, quality) classification used by the category table.
- **`[character]`** — read-write. Where the baseline tables live. The tool never touches `acore_world` beyond the one `item_template` read, and never touches `acore_playerbots` at all.

They can be the same user or different users, on the same host or on different hosts. The config handles every combination through the inheritance mechanism described in section 5 above.[](https://github.com/electrobutterfly/mod-playerbots-auctioneer/pulls)

------

## Contributing

[Issues](https://github.com/electrobutterfly/mod-playerbots-auctioneer/issues) and [pull requests](https://github.com/electrobutterfly/mod-playerbots-auctioneer/pulls) are welcome. When filing a bug against the auctioneer specifically, please attach:

- The `[AH-TRACE]` output for a bot exhibiting the problem.

- Your `playerbots_auction.conf`

- The output of:

  ```sql
  SELECT snapshot FROM acore_characters.auctioneer_baseline_item LIMIT 1;
  ```

  

That is almost always enough to reproduce the problem locally.

------

## Acknowledgements

The baseline data format and the price-band approach are adapted from [mod-auctionsim](https://github.com/liyunfan1223/mod-auctionsim)'s Replay mode and its `compile-data.cpp` reference implementation. The scan data itself is pulled from the public Auctioneer mirror at `ah.nerfed.net`.

The seller and buyer pricing comparison logic is built against [mod-ah-bot-plus](https://github.com/NathanHandley/mod-ah-bot-plus), which provides the reference `CalculateItemValue` implementation that this subsystem benchmarks against.

The auction-house subsystem builds on the movement, chat, and personality infrastructure of [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots), without which none of this would be possible.

------

## Also check our other modules:

### mod-afk-kicker

A module that automatically kicks players who remain AFK (Away From Keyboard) for longer than a configurable duration, useful for high-population servers where inactive sessions consume player slots, memory, and CPU.

[mod-afk-kicker for AzerothCore-wotlk](https://github.com/electrobutterfly/mod-afk-kicker)

[mod-afk-kicker for TrinityCore 3.3.5](https://github.com/electrobutterfly/trinitycore-afk-kicker)



### mod-ip-limiter

A module that limits how many players may connect from a single IP address. When the limit is exceeded, every player from that IP is kicked. Specific IP addresses (e.g. internet cafés, LAN parties, or your own office) can be whitelisted and exempted from the limit.

[mod-ip-limiter for AzerothCore-wotlk](https://github.com/electrobutterfly/mod-ip-limiter)

[mod-ip-limiter for TrinityCore 3.3.5](https://github.com/electrobutterfly/trinitycore-ip-limiter)
