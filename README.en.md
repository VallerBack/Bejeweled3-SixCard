# Bejeweled3-SixCard

[中文](README.md) · [Reusable components](docs/REUSABLE_COMPONENTS.md) · [Technical notes (Chinese)](docs/TECHNICAL_DRAFT.md)

A six-card Poker extension for a pinned Windows x86 build of Bejeweled 3, accompanied by reverse-engineering notes and development tools.

**GPT assistance disclosure: OpenAI GPT / Codex was used extensively for research assistance, implementation and fixes, test tooling, and technical documentation. The maintainer and participating players supplied gameplay design, ranking and balance decisions, community feedback, and repeated manual gameplay acceptance checks. AI assistance does not guarantee correctness; live acceptance, offline tests and coverage gaps are documented separately.**

## Features

- Six cards and eleven hand categories in standalone endless Poker and the three original Poker quests.
- Single-color, dual-color and wildcard cards with best-hand color assignment.
- Acquisition/settlement animations, special bonuses, an eleven-row table, hover text and statistics.
- Per-category danger progression, skull-buster integration and dedicated save/statistics files.
- A beta launcher with separate user data, no automatic original-profile import and no preset unlocks.

The original five-card storage boundary remains intact; the DLL owns the sixth card and extension state. The original game still supplies the board, assets and substantial underlying behavior. This is a fixed-build runtime extension, not a full reconstruction of the game's source code.

## Beyond Poker

Developers modifying other modes may find the launcher/helper, component pinning, installation checks, profile isolation, mode-scoped hooks, native UI/animation integration, statistics rendering and persistence-boundary examples useful. These are adaptable examples, not a general-purpose SDK. Many consumers depend on Poker fields, fixed addresses and x86 calling conventions. See the [bilingual reuse guide](docs/REUSABLE_COMPONENTS.md).

## Build

Requirements: Windows, Visual Studio 2022 with MSVC v143 x86/x64 tools, and a Windows SDK.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
```

Outputs are written to `out/`. The script builds the three x86 components, pins the generated launcher to the newly built DLL/helper, and runs two offline checks. This source layout has built successfully; newly generated binaries have not received the same runtime acceptance coverage as the original Beta 1 package.

No original game executable, assets or personal saves are included. For a local runtime layout, supply the matching complete game files under `out/game/`, preserve the signed configuration, and launch `out/BJ3SixCard.exe`. Target executable SHA256:

```text
28F033F191925A02D3F329B93EC8AEDA5396625C1A981218AAE3D36ACFF2F865
```

The launcher uses `%LOCALAPPDATA%/PopCap Games/BJ3SixBeta/`. Locally compiled builds share this namespace with Beta 1; they are not automatically isolated from existing beta progress.

## Review status and limitations

This is a private source/documentation candidate for maintainer review. A project license has not been selected. Provenance, generation and relocation notes for native-derived byte materials need completion. `docs/SOURCE_ORIGINS.json` records research-workspace origins, not a complete licensing review.

Player-reported 3D acceleration trouble is undiagnosed. Other machines, DPI/fullscreen combinations and rapid consecutive actions are not exhaustively covered. Rare double-Hypercube behavior and legacy-save migration have narrower live coverage than their offline checks. See the technical notes for exact limits.

Please report the build, mode, reproduction steps, actual result and expected result. Remove personal information before sharing screenshots or saves. No project license grant is declared yet; licensing terms remain a maintainer decision.
