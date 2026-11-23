# Bejeweled3-SixCard

[中文](README.md)

A six-card extension for the Windows x86 version of Bejeweled 3, covering standalone endless Poker and the three original Poker quests. This repository contains extension source, build tools and reverse-engineering notes for card acquisition, evaluation, scoring, animation, danger progression, UI, statistics and persistence.

OpenAI GPT / Codex assisted reverse-engineering research, implementation, fixes, test tooling and documentation. The maintainer and participating players provided gameplay design, hand rankings, balance decisions, feedback and repeated manual gameplay acceptance checks.

## Architecture

A dedicated launcher loads the DLL and connects it to native action, evaluation, settlement, drawing and persistence boundaries. The original game supplies the board, matches, assets and underlying behavior; the extension supplies new rules and their data, UI and state management. The original executable remains unchanged on disk.

Target executable SHA256:

```text
28F033F191925A02D3F329B93EC8AEDA5396625C1A981218AAE3D36ACFF2F865
```

The launcher checks the game, DLL, helper and signed configuration before installing runtime hooks. Function-pointer replacements and local call/jump changes connect the extension while preserving native register conventions, stack balance, flags and floating-point state.

## Extending five cards to six

Native storage, loops and animation objects assume five cards. The extension retains that boundary and stores the sixth card separately, providing a complete six-card view for evaluation and presentation.

State belongs to the current board and hand generation. Swap requests, accepted action completion and card commitment are separate stages. Invalid ordinary swaps revert without collecting a sixth card. Five cards wait for another accepted acquisition; settlement follows the sixth card's reveal. A hand generation is credited once.

Hypercube creation and use have separate paths: creating one can award a wildcard; using an existing Hypercube with an ordinary gem awards that color; matching two Hypercubes awards one full wildcard. Dual-color cards retain two allowed colors, while ordinary cards retain one.

## Evaluation and scoring

The evaluator represents each card as an allowed-color set, selects the highest-ranked legal six-card assignment, and returns the chosen colors. Raw card identity remains available for dual-color insets, wildcard glyphs and special bonuses.

Ordinary gem IDs are 0–6. Spectrum means six distinct colors. Rows below are ordered strongest to weakest; repeated letters denote matching colors.

| ID | Hand | Pattern | Endless base | Buster charge | Quest base |
|---:|---|---|---:|---:|---:|
| 1 | Flush | AAAAAA | 100000 | 100% | 900 |
| 2 | 5 of a Kind | AAAAAB | 75000 | 80% | 650 |
| 3 | 2 Triplets | AAABBB | 50000 | 65% | 600 |
| 4 | Tower | AAAABB | 35000 | 50% | 500 |
| 5 | 4 of a Kind | AAAABC | 25000 | 40% | 400 |
| 6 | 3 Pair | AABBCC | 20000 | 30% | 300 |
| 7 | Spectrum | ABCDEF | 15000 | 25% | 250 |
| 8 | Pyramid | AAABBC | 10000 | 20% | 200 |
| 9 | 3 of a Kind | AAABCD | 7500 | 15% | 150 |
| 10 | 2 Pair | AABBCD | 5000 | 10% | 100 |
| 11 | Pair | AABCDE | 2500 | 5% | 50 |

Flame/lightning card bonuses are 100/250 in endless Poker and 50/100 in quests. Actual score credit, central subtotal and individual popups are synchronized separately. The sixth popup uses an independent merge identifier to avoid combining with the fifth card's bonus.

## Danger progression

Some native consumers use seven-tier arrays. Mapping eleven hands onto those tiers would mark several categories together if the same mapping also determined danger.

The extension uses a separate progression:

```text
Pair → 2 Pair → 3 of a Kind → Pyramid → Spectrum
→ 3 Pair → 4 of a Kind → Tower → 2 Triplets
```

Each growth event adds at most one mark; existing marks accumulate, and the buster removes one. Five of a Kind and Flush remain safe. Compatibility tiers serve native arrays, while danger decisions, coin branches, the Need hint, table marks and buster targets use the new progression.

Skull growth retains the native table and hand multiplier. It is separate from the buster-charge percentages above.

## UI and animation

Six display records connect gem flights, card flips, collector positions, central expansion, wildcard resolution, bonus popups, card pulses and fading. Native first-five effects and extension-owned sixth-card effects retain their respective lifetimes and ownership.

The eleven-row table shares a model for labels, prices, danger, the current hand and hover bounds. The table draws below moving cards and skulls above borders. The current-hand border has a white glow without a fill. End statistics retain native backgrounds, axes, bar assets and growth animation with eleven data categories.

Coordinates scale with the internal layout height. Desktop window dimensions and internal coordinates are separate; hand evaluation reads game state rather than screen pixels.

## Mode isolation

The board factory selects rules using the parser, task record and creation arguments. Standalone Poker and the three quests use their respective prices and state; other modes restore their native scope.

Quest factory indices are 8, 18 and 37, with goals of 3000, 3500 and 4000 credits over ten hands. Quests use six cards without endless danger or charge. Exit-reset behavior, badges and quest-return flow remain native.

## Save format and profiles

Data lives in `%LOCALAPPDATA%/PopCap Games/BJ3SixBeta/`; settings use `HKCU/Software/PopCap/BJ3SixBeta`. Users create profiles and unlock modes through normal play. Original profiles and settings are not automatically imported.

Endless saves use `poker.bj6` and `poker.bj6.bj3stats`, backed up together. The game serializes the primary save. Extension statistics commit/import after successful native durable I/O and are paired with the primary file through SHA256 and native state checks.

The V3 companion is 108 bytes, with little-endian integers:

| Offset | Bytes | Content |
|---:|---:|---|
| 0 | 8 | Format-family magic `BJ3STAT2` |
| 8 | 4 | Version 3 |
| 12 | 4 | Payload length 88 |
| 16 | 32 | Primary-save SHA256 |
| 48 | 4 | Native hand counter |
| 52 | 4 | Score |
| 56 | 4 | Native card count, 0–5 |
| 60 | 44 | Eleven completed-category counts in ID order |
| 104 | 4 | Adler-32 of the first 104 bytes |

The native hand counter and completed-hand total have different meanings; statistics use per-game counts. Animation objects and pointers are not serialized. Save/close requests during an accepted sixth-card settlement are deferred to a stable boundary.

V2 imports convert the legacy danger stage while preserving marked categories. V3 identifies the per-category stage stored in the native save, preventing repeated conversion.

## Reuse in other modes

| Area | Entry point | Application |
|---|---|---|
| Startup and versions | `src/launcher/`, `src/helper/` | Component checks, loading, installation checks and profile identity |
| Mode scoping | `src/game/mode_scope.cpp` | Select and restore rule scopes at a shared creation boundary |
| Native UI integration | `src/game/pokerboard_face_draw.cpp` | Drawing, hover, curves, layering and effect lifetimes |
| Native-style charts | `src/game/pokerendlevel_stats.cpp` | Replace data while retaining assets and growth effects |
| Extension persistence | `src/game/game_stats_persistence.cpp` | Pair files, commit after native success and restore state |
| Stable save boundaries | `src/game/deferred_save.cpp` | Align save requests with gameplay transaction completion |

Startup and state-management techniques can support other modes. Game-side entry points, fields and coordinates need adaptation. Establish the target mode's objects, accepted actions, lifecycle and persistence conditions before connecting its consumers. See the [reuse guide](docs/REUSABLE_COMPONENTS.md).

## Source map

Game-side files below are under `src/game/`.

| Files | Purpose |
|---|---|
| `six_card_core.h/.cpp` | Ordinary, wildcard and allowed-color-set evaluation |
| `poker_sidecar.h/.cpp` | Extension hand and lifecycle state |
| `pokerboard_capture_hook.cpp`, `pokerboard_hook.cpp` | Action capture, rules and settlement |
| `poker_wait_six.cpp`, `sixth_acquisition.h` | Sixth-card wait/acquisition stages |
| `poker_rules.h`, `quest_poker_rules.h`, `poker_context.h` | Mode-specific rules |
| `danger_steps.h`, `danger_hooks.cpp` | Danger stages and native-consumer adaptation |
| `poker_table_model.h`, `poker_partial_hand.h` | Table, current-hand and hover models |
| `game_statistics_session.h` | Per-game statistics ownership |
| `game_stats_record.h`, `stats_pair_io.h` | Companion schema and file pairing |
| `native_*_bytes.inc` | Native-derived local rendering/update code materials |

## Build and run

Requirements: Windows, Visual Studio 2022, MSVC v143 x86/x64 tools and a Windows SDK. The DLL is x86/C++14; the launcher is x86/C++17.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
```

The script builds the DLL, helper and launcher, pins the generated launcher to the newly built components, and runs the mode-scope and danger-stage offline checks. Outputs are written to `out/`.

Place matching complete local game files under `out/game/`, preserve the signed `properties/partner.xml`, and launch `out/BJ3SixCard.exe`. This repository supplies extension source and tools; the local game installation supplies the executable and assets. Self-built versions use the same `BJ3SixBeta` data directory.

## Reverse-engineering landmarks

| Location | Purpose |
|---|---|
| PokerBoard `+0x3710` | Native current card count |
| `+0x3960` | Native hand counter |
| `+0x397C/+0x3980/+0x3984` | Danger stage, cap and growth progress |
| `0x006C42B6` | Cache/comparison adaptation before danger decisions |
| `0x007176CA`, `0x00721924` | Durable write/read call boundaries |
| `0x00811A40` | Restore entry slot |
| `0x00746B90` | Mode factory boundary |

## Validation and known issues

Beta 1 received manual gameplay acceptance for six-card settlement, dual/wildcard presentation, special bonuses, table/statistics, single-category danger increments, dedicated persistence and quest/endless switching. Fresh-profile startup and restarting from a relocated extraction were verified. This source layout builds all three components and passes two offline checks.

Double-Hypercube handling and legacy danger-stage conversion have offline tests with less live coverage. Player-reported 3D acceleration trouble awaits reproduction; beta feedback continues for other machines, DPI/fullscreen settings and rapid consecutive actions.

File origins are recorded in `docs/SOURCE_ORIGINS.json`. Licensing and generation/relocation notes for native-derived byte materials remain to be completed.
