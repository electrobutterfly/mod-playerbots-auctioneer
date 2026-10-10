#!/usr/bin/env python3
# This file is part of the mod-playerbots-auctioneer subsystem for AzerothCore.
#
# Copyright (C) 2026 Klaus Simon
# Copyright (C) 2026 Alex Trowell  (compile-data.cpp, mod-auctionsim)
#
# This program is a derivative work of compile-data.cpp from the
# mod-auctionsim project, translated to Python. The rope parser, the
# per-item and per-category statistical pipeline, the record field
# layout, and the bucketing scheme are derived from that file.
#
# Modified from the C++ original on 2026-10-09.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
"""
auctioneer.py

Build a per-item price / stack / listing / bid-ratio baseline for the
mod-playerbots auctioneer module from Auctioneer scan data.

One script, one config, one command. Runs the full pipeline:

    download   fetch both faction Lua files from the mirror
    parse      strip the Lua rope syntax, bucket, compute per-item stats
    write      emit a ready-to-insert SQL file
    insert     pipe that SQL into the character database

The `run` command does all four in sequence.

Data model (two tables, both in the character database):

    auctioneer_baseline_item      one row per (faction, item_id, suffix)
    auctioneer_baseline_category  one row per (faction, item_class, quality)

Two independent MySQL connections are used:

    [world]      read-only.  Reads item_template for the item filter and
                 the (class, quality) used for category buckets and the
                 bid-ratio class priors.
    [character]  read-write. The two baseline tables live here.

Both connections have their own host, port, user, password and database.
Values not set in the specific section are inherited from [mysql].

Pure Python 3 stdlib. No pip, no C++ build step.
"""

from __future__ import annotations

import argparse
import array
import configparser
import io
import math
import os
import subprocess
import sys
import tempfile
import urllib.request
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path


# --------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------

ALLIANCE = 2
HORDE    = 6
UNKNOWN  = 0

# Auctioneer scan record layout (positional fields inside a rope):
#   0 LINK   1 ILEVEL 2 ITYPE  3 ISUB   4 IEQUIP  5 PRICE  6 TLEFT
#   7 TIME   8 NAME   9 DEP2  10 COUNT 11 QUALITY 12 CANUSE 13 ULEVEL
#  14 MINBID 15 MININC 16 BUYOUT 17 CURBID 18 AMHIGH 19 SELLER 20 FLAG
#  21 BONUSES 22 ITEMID 23 SUFFIX 24 FACTOR 25 ENCHANT 26 SEED
FIELD_ITYPE   = 2
FIELD_COUNT   = 10
FIELD_QUALITY = 11
FIELD_MINBID  = 14
FIELD_BUYOUT  = 16
FIELD_ITEMID  = 22
FIELD_SUFFIX  = 23
MIN_RECORD_FIELDS = 27

# Bid-ratio confidence thresholds. Below MIN_RELIABLE_BID_SAMPLES the
# per-item median is statistical noise, so the C++ consumer reads the
# per-class prior instead (already computed and stored in the table).
MIN_RELIABLE_BID_SAMPLES  = 10
MIN_CLASS_PRIOR_ROWS      = 100
GLOBAL_BID_RATIO_FALLBACK = 9400  # basis points


# --------------------------------------------------------------------
# Lua SavedVariables parsing
#
# The file is a normal Lua table assignment. The interesting part is a
# ["ropes"] array whose elements are string literals. Each literal, once
# unescaped, is itself Lua source: `return {{...},{...},...}`.
#
# Two layers of string escaping live in the file. This module undoes
# layer 1 (the SavedVariables literal), then scans layer 2 with a
# Lua-string-literal-aware tokenizer that also skips `--` comments.
# --------------------------------------------------------------------

def _read_lua_string(text: bytes, i: int) -> tuple[bytes, int]:
    """Read one Lua double-quoted string starting at text[i] (must be '"').
    Returns the unescaped bytes and the index just past the closing quote."""
    assert text[i] == 0x22, f"expected quote at {i}, got {text[i]:#x}"
    i += 1
    out = bytearray()
    escape_map = {0x6E: 0x0A, 0x74: 0x09, 0x72: 0x0D,
                  0x22: 0x22, 0x5C: 0x5C, 0x27: 0x27}
    while i < len(text) and text[i] != 0x22:
        if text[i] == 0x5C and i + 1 < len(text):
            c = text[i + 1]
            out.append(escape_map.get(c, c))
            i += 2
        else:
            out.append(text[i])
            i += 1
    if i < len(text):
        i += 1
    return bytes(out), i


def _skip_trivia(text: bytes, i: int) -> int:
    """Skip whitespace, commas and Lua '-- ...' line comments."""
    while True:
        while i < len(text) and text[i] in (0x20, 0x09, 0x0A, 0x0D, 0x2C):
            i += 1
        if i + 1 < len(text) and text[i] == 0x2D and text[i + 1] == 0x2D:
            nl = text.find(b"\n", i)
            i = len(text) if nl == -1 else nl + 1
            continue
        return i


def _detect_faction(text: bytes, before_pos: int) -> int:
    """Faction is positional: the nearest ["Horde"] or ["Alliance"] key
    above the rope block in the SavedVariables table."""
    horde = text.rfind(b'["Horde"]', 0, before_pos)
    ally  = text.rfind(b'["Alliance"]', 0, before_pos)
    if horde == -1 and ally == -1:
        return UNKNOWN
    if ally == -1:
        return HORDE
    if horde == -1:
        return ALLIANCE
    return HORDE if horde > ally else ALLIANCE


def extract_ropes(text: bytes):
    """Return a list of (rope_source_bytes, faction) tuples."""
    ropes = []
    search_from = 0
    marker = b'"ropes"'
    while True:
        pos = text.find(marker, search_from)
        if pos == -1:
            break
        faction = _detect_faction(text, pos)
        brace = text.find(b"{", pos)
        if brace == -1:
            break
        i = brace + 1
        while True:
            i = _skip_trivia(text, i)
            if i >= len(text) or text[i] == 0x7D:  # '}'
                i += 1
                break
            if text[i] != 0x22:
                break
            rope, i = _read_lua_string(text, i)
            ropes.append((rope, faction))
        search_from = i
    return ropes


def parse_rope_records(rope: bytes):
    """Return a list of field-byte-string lists from `return {{...},...}`."""
    records = []
    i = rope.find(b"{")
    if i == -1:
        return records
    i += 1
    while True:
        while i < len(rope) and rope[i] in (0x20, 0x09, 0x0A, 0x0D, 0x2C):
            i += 1
        if i >= len(rope) or rope[i] == 0x7D:
            break
        if rope[i] != 0x7B:
            break
        i += 1
        fields = []
        while True:
            while i < len(rope) and rope[i] in (0x20, 0x09, 0x0A, 0x0D):
                i += 1
            if i >= len(rope):
                break
            if rope[i] == 0x7D:
                i += 1
                break
            if rope[i] == 0x22:
                val, i = _read_lua_string(rope, i)
                fields.append(val)
            else:
                start = i
                while i < len(rope) and rope[i] not in (0x2C, 0x7D):
                    i += 1
                tok = rope[start:i]
                while tok and tok[-1:] in (b" ", b"\t", b"\r", b"\n"):
                    tok = tok[:-1]
                fields.append(tok)
            while i < len(rope) and rope[i] in (0x20, 0x09, 0x0A, 0x0D):
                i += 1
            if i < len(rope) and rope[i] == 0x2C:
                i += 1
                continue
            if i < len(rope) and rope[i] == 0x7D:
                i += 1
                break
        records.append(fields)
    return records


# --------------------------------------------------------------------
# Statistics
#
# Percentile interpolation is R-7 (same as numpy's default). The adjusted
# variants trim with a Tukey 1.5 * IQR fence before taking the central
# tendency, which removes fat-finger and troll listings from the band.
# --------------------------------------------------------------------

def _round_half_away(x: float) -> int:
    """Round half away from zero, matching std::llround in C++."""
    if x >= 0:
        return int(math.floor(x + 0.5))
    return int(math.ceil(x - 0.5))


def _percentile(sorted_vals: list, p: float) -> float:
    n = len(sorted_vals)
    if n == 0:
        return 0.0
    if n == 1:
        return float(sorted_vals[0])
    idx = p * (n - 1)
    lo = int(math.floor(idx))
    hi = min(lo + 1, n - 1)
    return sorted_vals[lo] + (idx - lo) * (sorted_vals[hi] - sorted_vals[lo])


def _central(sorted_vals: list) -> dict:
    n = len(sorted_vals)
    if n == 0:
        return dict(low=0, high=0, mean=0, median=0, mode=0)
    med = sorted_vals[n // 2] if n % 2 else \
          (sorted_vals[n // 2 - 1] + sorted_vals[n // 2]) / 2.0
    freq = defaultdict(int)
    for v in sorted_vals:
        freq[_round_half_away(v)] += 1
    best_count, best_val = -1, 0
    for v, c in freq.items():
        if c > best_count or (c == best_count and v < best_val):
            best_count, best_val = c, v
    return dict(
        low=_round_half_away(sorted_vals[0]),
        high=_round_half_away(sorted_vals[-1]),
        mean=_round_half_away(sum(sorted_vals) / n),
        median=_round_half_away(med),
        mode=best_val,
    )


def compute_stats(samples) -> dict:
    if not samples:
        return dict(sample_count=0, low=0, high=0, mean=0, median=0, mode=0,
                    q1=0, q3=0, adj_low=0, adj_high=0,
                    adj_mean=0, adj_median=0, adj_mode=0)
    s = sorted(samples)
    n = len(s)
    raw = _central(s)
    q1f = _percentile(s, 0.25)
    q3f = _percentile(s, 0.75)
    iqr = q3f - q1f
    lo, hi = max(0.0, q1f - 1.5 * iqr), q3f + 1.5 * iqr
    trimmed = [v for v in s if lo <= v <= hi] or s
    adj = _central(trimmed)
    return dict(
        sample_count=n,
        low=raw["low"], high=raw["high"], mean=raw["mean"],
        median=raw["median"], mode=raw["mode"],
        q1=_round_half_away(q1f), q3=_round_half_away(q3f),
        adj_low=adj["low"], adj_high=adj["high"], adj_mean=adj["mean"],
        adj_median=adj["median"], adj_mode=adj["mode"],
    )


# --------------------------------------------------------------------
# Bucketing
# --------------------------------------------------------------------

def _to_int(b: bytes, default: int = 0) -> int:
    s = b.decode("ascii", "replace").strip()
    if not s or s in ("nil", "true", "false"):
        return default
    try:
        return int(s)
    except ValueError:
        return default


class Buckets:
    """One sample array per stat block, keyed by (faction, item_id, suffix)
    for item stats, or (faction, class, quality) for the category depth."""
    def __init__(self):
        self.price    = defaultdict(lambda: array.array("d"))
        self.stack    = defaultdict(lambda: array.array("d"))
        self.listing  = defaultdict(lambda: array.array("d"))
        self.bidratio = defaultdict(lambda: array.array("d"))
        self.category = defaultdict(lambda: array.array("d"))


def accumulate_rope(rope: bytes, faction: int, b: Buckets,
                    item_lookup: dict) -> None:
    """
    Fold one rope's records into `b`.

    `item_lookup` maps item_id -> (class, quality) for items present in
    this realm's item_template. Records for items not in the lookup are
    skipped entirely -- the source realm has items ours does not, and
    keeping them would both bloat the table and be useless at runtime.
    Category counts use the realm's class/quality, not the rope's ITYPE
    string, so the depth numbers reflect our item pool.
    """
    per_item = defaultdict(int)
    per_cat  = defaultdict(int)

    for fields in parse_rope_records(rope):
        if len(fields) < MIN_RECORD_FIELDS:
            continue
        item_id = _to_int(fields[FIELD_ITEMID])
        if item_id == 0:
            continue
        if item_id not in item_lookup:
            continue

        suffix = _to_int(fields[FIELD_SUFFIX])
        count  = _to_int(fields[FIELD_COUNT], 1)
        if count <= 0:
            count = 1

        key = (faction, item_id, suffix)
        per_item[key] += 1

        item_class, item_quality = item_lookup[item_id]
        if 0 <= item_quality < 8:
            per_cat[(faction, item_class, item_quality)] += 1

        buyout = _to_int(fields[FIELD_BUYOUT])
        if buyout <= 0:
            continue

        b.price[key].append(buyout / count)
        b.stack[key].append(float(count))

        minbid = _to_int(fields[FIELD_MINBID])
        if minbid > 0:
            ratio = minbid / buyout
            if ratio < 0.01:
                ratio = 0.01
            elif ratio > 1.0:
                ratio = 1.0
            b.bidratio[key].append(ratio * 10000.0)

    for key, cnt in per_item.items():
        b.listing[key].append(float(cnt))
    for key, cnt in per_cat.items():
        b.category[key].append(float(cnt))


def parse_lua_file(path: Path, b: Buckets, item_lookup: dict) -> int:
    text = path.read_bytes()
    ropes = extract_ropes(text)
    if not ropes:
        print(f"  warning: no ropes found in {path.name}", file=sys.stderr)
        return 0
    for rope, faction in ropes:
        if faction == UNKNOWN:
            print(f"  warning: no faction above a rope in {path.name}",
                  file=sys.stderr)
        accumulate_rope(rope, faction, b, item_lookup)
    return len(ropes)


# --------------------------------------------------------------------
# Class priors for the bid-ratio fallback
# --------------------------------------------------------------------

def compute_class_priors(b: Buckets, item_lookup: dict) -> dict:
    """
    Return {(faction, item_class): mean_bp}.

    Only rows with bid_ratio_samples >= MIN_RELIABLE_BID_SAMPLES
    contribute. This is the mean of the item-level adjusted medians,
    not the mean over individual listings.
    """
    sums   = defaultdict(float)
    counts = defaultdict(int)

    for key, samples in b.bidratio.items():
        faction, item_id, _suffix = key
        if len(samples) < MIN_RELIABLE_BID_SAMPLES:
            continue
        item_class, _ = item_lookup.get(item_id, (None, None))
        if item_class is None:
            continue
        stats = compute_stats(samples)
        sums[(faction, item_class)]   += stats["adj_median"]
        counts[(faction, item_class)] += 1

    return {k: sums[k] / counts[k] for k in counts}


# --------------------------------------------------------------------
# SQL emission
# --------------------------------------------------------------------

ITEM_COLS = [
    "faction", "item_id", "suffix",
    "price_median", "price_low", "price_high", "price_q1", "price_q3",
    "price_samples",
    "stack_typical", "stack_low", "stack_high",
    "listing_typical", "listing_snapshots",
    "bid_ratio_median", "bid_ratio_low", "bid_ratio_high",
    "bid_ratio_samples", "bid_ratio_reliable", "bid_ratio_class_prior_bp",
    "trend_bp",
    "snapshot",
]

CAT_COLS = [
    "faction", "item_class", "quality",
    "sample_count", "q1", "median", "adj_low", "adj_high",
    "snapshot",
]


def _sql_lit(v):
    if v is None:
        return "NULL"
    if isinstance(v, str):
        return "'" + v.replace("'", "''") + "'"
    return str(v)


def _emit_inserts(out, table: str, cols: list, rows: list,
                  chunk: int = 500) -> None:
    col_list = ", ".join(f"`{c}`" for c in cols)
    for i in range(0, len(rows), chunk):
        part = rows[i:i + chunk]
        out.write(f"INSERT INTO `{table}` ({col_list}) VALUES\n")
        for j, r in enumerate(part):
            vals = ", ".join(_sql_lit(r.get(c)) for c in cols)
            out.write(f"  ({vals})")
            out.write(";\n" if j == len(part) - 1 else ",\n")
    out.write("\n")


def build_item_rows(b: Buckets, item_lookup: dict, class_priors: dict,
                    history: dict, snapshot: str):
    keys = set(b.price) | set(b.listing) | set(b.bidratio)
    for key in keys:
        faction, item_id, suffix = key

        p = compute_stats(b.price.get(key, []))
        s = compute_stats(b.stack.get(key, []))
        l = compute_stats(b.listing.get(key, []))

        # Multi-snapshot trend: percent change of the price median from the
        # oldest history entry to the current run. Positive means prices are
        # rising; negative means falling. Clamped to +/- 50% so a noisy first
        # few runs cannot produce an extreme multiplier. Zero when there is
        # no history or the current run has no price median.
        trend_bp = 0
        series = history.get(key)
        if series and p["adj_median"] > 0:
            oldest = series[0][1]
            if oldest > 0:
                raw = (p["adj_median"] - oldest) / oldest
                if raw > 0.5:
                    raw = 0.5
                elif raw < -0.5:
                    raw = -0.5
                trend_bp = int(round(raw * 10000))

        bid_samples = b.bidratio.get(key)
        if not bid_samples:
            # No minbid observations. Match the module's own fallback:
            # a one-element synthetic {10000} bucket, which makes
            # sample_count = 1 and every stat exactly 10000.
            bid_samples = array.array("d", [10000.0])
        r = compute_stats(bid_samples)

        bid_reliable = 1 if r["sample_count"] >= MIN_RELIABLE_BID_SAMPLES else 0

        item_class, _ = item_lookup.get(item_id, (None, None))
        prior = int(class_priors.get((faction, item_class),
                                     GLOBAL_BID_RATIO_FALLBACK))

        yield dict(
            faction=faction, item_id=item_id, suffix=suffix,
            price_median=p["adj_median"], price_low=p["adj_low"],
            price_high=p["adj_high"], price_q1=p["q1"], price_q3=p["q3"],
            price_samples=p["sample_count"],
            stack_typical=max(1, s["adj_mode"] or s["adj_median"] or 1),
            stack_low=max(1, s["adj_low"] or 1),
            stack_high=max(1, s["adj_high"] or s["adj_median"] or 1),
            listing_typical=max(1, l["adj_median"] or l["adj_mean"] or 1),
            listing_snapshots=l["sample_count"],
            bid_ratio_median=r["adj_median"],
            bid_ratio_low=r["adj_low"] or r["adj_median"],
            bid_ratio_high=r["adj_high"] or r["adj_median"],
            bid_ratio_samples=r["sample_count"],
            bid_ratio_reliable=bid_reliable,
            bid_ratio_class_prior_bp=prior,
            trend_bp=trend_bp,
            snapshot=snapshot,
        )


def build_category_rows(b: Buckets, snapshot: str):
    for (faction, cls, quality), samples in b.category.items():
        s = compute_stats(samples)
        yield dict(
            faction=faction, item_class=cls, quality=quality,
            sample_count=s["sample_count"],
            q1=s["q1"], median=s["median"],
            adj_low=s["adj_low"], adj_high=s["adj_high"],
            snapshot=snapshot,
        )


def _emit_history_inserts(out, item_rows: list, snapshot: str,
                          chunk: int = 500) -> None:
    """
    Append history rows for the current snapshot. IGNORE means a re-run of
    the parser within the same snapshot hour does not duplicate rows; the
    snapshot tag is hour-granular so a normal 24-hourly refresh always lands
    in its own bucket. STR_TO_DATE parses the 'YYYY-MM-DD_HH' tag into a
    real TIMESTAMP, and every chunk of the same run gets the same value
    (MySQL evaluates STR_TO_DATE once per statement).
    """
    if not item_rows:
        return
    cols = ["faction", "item_id", "suffix", "snapshot_time",
            "price_median", "price_low", "price_high", "price_samples"]
    col_list = ", ".join(f"`{c}`" for c in cols)
    for i in range(0, len(item_rows), chunk):
        part = item_rows[i:i + chunk]
        out.write(f"INSERT IGNORE INTO `auctioneer_baseline_history` "
                  f"({col_list}) VALUES\n")
        for j, r in enumerate(part):
            out.write(
                f"  ({r['faction']}, {r['item_id']}, {r['suffix']}, "
                f"STR_TO_DATE('{snapshot}', '%Y-%m-%d_%H'), "
                f"{r['price_median']}, {r['price_low']}, "
                f"{r['price_high']}, {r['price_samples']})"
            )
            out.write(";\n" if j == len(part) - 1 else ",\n")
    out.write("\n")


def write_sql(out, item_rows: list, cat_rows: list, snapshot: str,
              retention_days: int = 90) -> None:
    out.write("SET NAMES utf8mb4;\n")
    out.write("START TRANSACTION;\n\n")
    out.write("DELETE FROM `auctioneer_baseline_item`;\n")
    out.write("DELETE FROM `auctioneer_baseline_category`;\n\n")
    _emit_inserts(out, "auctioneer_baseline_item", ITEM_COLS, item_rows)
    _emit_inserts(out, "auctioneer_baseline_category", CAT_COLS, cat_rows)
    _emit_history_inserts(out, item_rows, snapshot)
    out.write(f"DELETE FROM `auctioneer_baseline_history` "
              f"WHERE `snapshot_time` < NOW() - INTERVAL {retention_days} DAY;\n\n")
    out.write("COMMIT;\n")


# --------------------------------------------------------------------
# Schema (run once, before the first `run`)
# --------------------------------------------------------------------

SCHEMA_SQL = """
DROP TABLE IF EXISTS `auctioneer_baseline_item`;
CREATE TABLE `auctioneer_baseline_item` (
  `faction`      TINYINT UNSIGNED NOT NULL COMMENT '2=Alliance, 6=Horde',
  `item_id`      INT UNSIGNED     NOT NULL COMMENT 'item_template.entry',
  `suffix`       INT              NOT NULL COMMENT 'item suffix: <0 enchant, >0 random prop, 0 none',

  `price_median`  INT UNSIGNED NOT NULL COMMENT 'adjusted median per-unit buyout, copper',
  `price_low`     INT UNSIGNED NOT NULL COMMENT 'adjusted low, copper',
  `price_high`    INT UNSIGNED NOT NULL COMMENT 'adjusted high, copper',
  `price_q1`      INT UNSIGNED NOT NULL COMMENT '25th percentile, copper',
  `price_q3`      INT UNSIGNED NOT NULL COMMENT '75th percentile, copper',
  `price_samples` INT UNSIGNED NOT NULL,

  `stack_typical` INT UNSIGNED NOT NULL COMMENT 'adjusted mode of listing stack size',
  `stack_low`     INT UNSIGNED NOT NULL,
  `stack_high`    INT UNSIGNED NOT NULL,

  `listing_typical`   INT UNSIGNED NOT NULL COMMENT 'adjusted median concurrent listings per snapshot',
  `listing_snapshots` INT UNSIGNED NOT NULL,

  `bid_ratio_median`  INT UNSIGNED NOT NULL COMMENT 'adjusted median MINBID/BUYOUT in basis points',
  `bid_ratio_low`     INT UNSIGNED NOT NULL,
  `bid_ratio_high`    INT UNSIGNED NOT NULL,
  `bid_ratio_samples` INT UNSIGNED NOT NULL COMMENT 'listings with both buyout and minbid',
  `bid_ratio_reliable` TINYINT(1)  NOT NULL COMMENT '1 if samples >= 10, else 0',
  `bid_ratio_class_prior_bp` INT UNSIGNED NOT NULL COMMENT 'class-mean fallback for thin rows',

  `trend_bp` INT NOT NULL DEFAULT 0 COMMENT 'price_median trend over history window, in basis points',

  `snapshot`  VARCHAR(32) NOT NULL COMMENT 'source tag, YYYY-MM-DD_HH (UTC)',
  `imported_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

  PRIMARY KEY (`faction`, `item_id`, `suffix`),
  KEY `idx_item` (`item_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `auctioneer_baseline_category`;
CREATE TABLE `auctioneer_baseline_category` (
  `faction`     TINYINT UNSIGNED NOT NULL,
  `item_class`  TINYINT UNSIGNED NOT NULL COMMENT '0..15 (AzerothCore ITEM_CLASS_*)',
  `quality`     TINYINT UNSIGNED NOT NULL COMMENT '0..7 (AzerothCore ITEM_QUALITY_*)',
  `sample_count` INT UNSIGNED NOT NULL COMMENT 'snapshots the category appeared in',
  `q1`          INT UNSIGNED NOT NULL,
  `median`      INT UNSIGNED NOT NULL,
  `adj_low`     INT UNSIGNED NOT NULL,
  `adj_high`    INT UNSIGNED NOT NULL,
  `snapshot`    VARCHAR(32) NOT NULL,
  `imported_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`faction`, `item_class`, `quality`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `auctioneer_baseline_history` (
  `faction`       TINYINT UNSIGNED NOT NULL,
  `item_id`       INT UNSIGNED     NOT NULL,
  `suffix`        INT              NOT NULL,
  `snapshot_time` TIMESTAMP        NOT NULL,
  `price_median`  INT UNSIGNED     NOT NULL,
  `price_low`     INT UNSIGNED     NOT NULL,
  `price_high`    INT UNSIGNED     NOT NULL,
  `price_samples` INT UNSIGNED     NOT NULL,
  PRIMARY KEY (`faction`, `item_id`, `suffix`, `snapshot_time`),
  KEY `idx_item_time` (`item_id`, `snapshot_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
"""


# --------------------------------------------------------------------
# MySQL helpers
# --------------------------------------------------------------------

class _MySQLConfigFile:
    """Writes a temp my.cnf for one connection so the password never hits
    the process list. Takes a single DB section dict."""
    def __init__(self, db_cfg: dict):
        self.cfg = db_cfg

    def __enter__(self) -> str:
        fd, path = tempfile.mkstemp(suffix=".cnf", prefix="auctioneer-")
        os.close(fd)
        with open(path, "w") as f:
            f.write(
                f"[client]\n"
                f"user={self.cfg['user']}\n"
                f"password={self.cfg['password']}\n"
                f"host={self.cfg['host']}\n"
                f"port={self.cfg['port']}\n"
            )
        self.path = path
        return path

    def __exit__(self, *exc):
        try:
            os.unlink(self.path)
        except OSError:
            pass


def mysql_query(db_cfg: dict, sql: str) -> bytes:
    """Run a query against one specific connection, return raw stdout."""
    with _MySQLConfigFile(db_cfg) as cnf:
        out = subprocess.run(
            ["mysql", f"--defaults-extra-file={cnf}",
             "--batch", "--skip-column-names",
             db_cfg["database"], "-e", sql],
            check=True, capture_output=True,
        )
    return out.stdout


def mysql_run_sql_file(db_cfg: dict, sql_text: str) -> None:
    """Pipe a full SQL script into one specific database."""
    with _MySQLConfigFile(db_cfg) as cnf:
        subprocess.run(
            ["mysql", f"--defaults-extra-file={cnf}",
             "--batch", "--skip-column-names", db_cfg["database"]],
            input=sql_text.encode("utf-8"),
            check=True,
        )


def fetch_item_lookup(db_cfg: dict) -> dict:
    """
    Return {item_id: (class, quality)} for every row in item_template.
    Uses the [world] connection, not [character].
    """
    raw = mysql_query(
        db_cfg,
        "SELECT entry, class, Quality FROM item_template")
    lookup = {}
    for line in raw.splitlines():
        parts = line.split(b"\t")
        if len(parts) != 3:
            continue
        try:
            entry   = int(parts[0])
            cls     = int(parts[1])
            quality = int(parts[2])
        except ValueError:
            continue
        lookup[entry] = (cls, quality)
    return lookup


def fetch_history(db_cfg: dict) -> dict:
    """
    Return {(faction, item_id, suffix): [(ts, median), ...]}, sorted by
    timestamp ascending. Empty if the table does not exist yet (first run
    after the migration) or the character DB is unreachable.

    The table is deliberately optional: a fresh install will not have it
    until the first history-inserting run has happened, and the parser
    must not fail in that case. Trend defaults to 0 (flat) when there is
    not enough history to compute it.
    """
    try:
        raw = mysql_query(
            db_cfg,
            "SELECT faction, item_id, suffix, "
            "       UNIX_TIMESTAMP(snapshot_time), price_median "
            "FROM auctioneer_baseline_history "
            "ORDER BY faction, item_id, suffix, snapshot_time")
    except Exception as e:
        print(f"  note: history table not readable ({e}); "
              f"trend will default to 0 for this run", file=sys.stderr)
        return {}

    series = defaultdict(list)
    for line in raw.splitlines():
        parts = line.split(b"\t")
        if len(parts) != 5:
            continue
        try:
            faction = int(parts[0])
            item_id = int(parts[1])
            suffix  = int(parts[2])
            ts      = int(parts[3])
            median  = int(parts[4])
        except ValueError:
            continue
        series[(faction, item_id, suffix)].append((ts, median))
    return series


# --------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------


def now_tag() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%d_%H")


def cmd_schema(_args, _cfg) -> None:
    sys.stdout.write(SCHEMA_SQL)


def cmd_download(args, cfg) -> None:
    tag = args.snapshot or now_tag()
    scans = Path(args.work_dir) / "scans"
    scans.mkdir(parents=True, exist_ok=True)

    for old in scans.glob("*.lua"):
        old.unlink()

    for name, url in (("horde",    cfg["horde_url"]),
                      ("alliance", cfg["alliance_url"])):
        dest = scans / f"{name}_{tag}.lua"
        print(f"downloading {name} -> {dest.name}", file=sys.stderr)
        req = urllib.request.Request(
            url, headers={"User-Agent": "mod-playerbots auctioneer baseline"})
        with urllib.request.urlopen(req, timeout=180) as r, dest.open("wb") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
        print(f"  {dest.stat().st_size / 1e6:.1f} MB", file=sys.stderr)


def cmd_parse(args, cfg) -> None:
    tag = args.snapshot or now_tag()
    scans = Path(args.work_dir) / "scans"
    out_path = Path(args.work_dir) / "baseline.sql"

    lua_files = sorted(scans.glob("*.lua"))
    if not lua_files:
        sys.exit(f"no .lua files in {scans}. Run `download` first.")

    print("loading item_template from world db...", file=sys.stderr)
    item_lookup = fetch_item_lookup(cfg["world"])
    print(f"  {len(item_lookup)} items", file=sys.stderr)

    b = Buckets()
    for lua in lua_files:
        print(f"parsing {lua.name}...", file=sys.stderr)
        n_ropes = parse_lua_file(lua, b, item_lookup)
        print(f"  {n_ropes} rope blocks", file=sys.stderr)

    print("computing class priors for bid-ratio fallback...", file=sys.stderr)
    class_priors = compute_class_priors(b, item_lookup)
    print(f"  {len(class_priors)} (faction, class) pairs", file=sys.stderr)

    print("reading historical snapshots for trend computation...", file=sys.stderr)
    history = fetch_history(cfg["character"])
    print(f"  {len(history)} items with history", file=sys.stderr)

    item_rows = list(build_item_rows(b, item_lookup, class_priors, history, tag))
    cat_rows  = list(build_category_rows(b, tag))

    print(f"writing {out_path} "
          f"({len(item_rows)} items, {len(cat_rows)} categories)...",
          file=sys.stderr)
    retention = cfg.get("history_retention_days", 90)
    with out_path.open("w", encoding="utf-8") as f:
        write_sql(f, item_rows, cat_rows, tag, retention)
    print(f"  {out_path.stat().st_size / 1e6:.1f} MB, snapshot {tag}",
          file=sys.stderr)


def cmd_insert(args, cfg) -> None:
    sql_path = Path(args.work_dir) / "baseline.sql"
    if not sql_path.is_file():
        sys.exit(f"no {sql_path}. Run `parse` first.")

    print(f"inserting {sql_path} into {cfg['character']['database']}...",
          file=sys.stderr)
    sql_text = sql_path.read_text(encoding="utf-8")
    mysql_run_sql_file(cfg["character"], sql_text)
    print("done.", file=sys.stderr)


def cmd_run(args, cfg) -> None:
    cmd_download(args, cfg)
    cmd_parse(args, cfg)
    cmd_insert(args, cfg)


def cmd_clean(args, _cfg) -> None:
    scans = Path(args.work_dir) / "scans"
    if scans.is_dir():
        for old in scans.glob("*.lua"):
            old.unlink()
            print(f"removed {old}", file=sys.stderr)
    sql = Path(args.work_dir) / "baseline.sql"
    if sql.is_file():
        sql.unlink()
        print(f"removed {sql}", file=sys.stderr)


# --------------------------------------------------------------------
# Config + main
# --------------------------------------------------------------------

def load_config(path: str) -> dict:
    p = configparser.ConfigParser()
    if not p.read(path):
        sys.exit(f"config not found: {path}")

    def resolve(section: str, key: str, fallback=None):
        # A value in the specific section wins.
        # Otherwise it is inherited from [mysql].
        # Otherwise the caller-provided fallback.
        # Otherwise it is an error.
        if p.has_option(section, key):
            return p.get(section, key)
        if p.has_option("mysql", key):
            return p.get("mysql", key)
        if fallback is not None:
            return fallback
        sys.exit(f"config missing [{section}] {key} (and no [mysql] fallback)")

    def db_section(name: str) -> dict:
        return {
            "host":     resolve(name, "host", "127.0.0.1"),
            "port":     resolve(name, "port", "3306"),
            "user":     resolve(name, "user"),
            "password": resolve(name, "password"),
            "database": resolve(name, "database"),
        }

    return {
        "world":        db_section("world"),
        "character":    db_section("character"),
        "horde_url":    p.get("source", "horde_url"),
        "alliance_url": p.get("source", "alliance_url"),
        "history_retention_days":
            int(p.get("history", "retention_days", fallback="90")),
    }


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("command",
                    choices=["run", "download", "parse", "insert",
                             "schema", "clean"])
    ap.add_argument("--config",   default="./auctioneer.conf")
    ap.add_argument("--work-dir", default=None,
                    help="override [paths] work_dir from the config")
    ap.add_argument("--snapshot", default=None,
                    help="override the YYYY-MM-DD_HH snapshot tag")
    args = ap.parse_args()

    if args.command == "schema":
        cmd_schema(args, None)
        return

    cfg = load_config(args.config)

    if args.work_dir:
        work_dir = args.work_dir
    else:
        p = configparser.ConfigParser()
        p.read(args.config)
        work_dir = p.get("paths", "work_dir",
                         fallback="/var/lib/auctioneer-baseline")
    args.work_dir = work_dir

    if args.command == "run":
        cmd_run(args, cfg)
    elif args.command == "download":
        cmd_download(args, cfg)
    elif args.command == "parse":
        cmd_parse(args, cfg)
    elif args.command == "insert":
        cmd_insert(args, cfg)
    elif args.command == "clean":
        cmd_clean(args, cfg)


if __name__ == "__main__":
    main()
