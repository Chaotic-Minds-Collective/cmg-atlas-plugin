# Chaotic Minds Gaming ATLAS Plugin Suite

Chaotic Minds Gaming's ATLAS work is organized as a coordinated suite of independently versioned modules. The modules are original CMG implementations. Existing community plugins and archived binaries may inform compatibility and feature research, but they are not dependencies or copied source.

## Shared foundations

- [CMG Core](https://github.com/Chaotic-Minds-Collective/cmg-atlas-core-plugin) — identity, authentication, scoped RBAC, server registry, platform APIs, PostgreSQL-backed domain data, event outbox, commerce, entitlements, reward queue and plugin metadata registry.
- [CMG ATLAS Adapter](https://github.com/Chaotic-Minds-Collective/cmg-atlas-plugin) — native integration boundary, C++ contracts, SDK pin and host-runtime experiments.
- [CMG ATLAS API SDK fork](https://github.com/Chaotic-Minds-Collective/cmg-atlas-api) — pinned API source, public headers and import library used by the native adapter.
- [CMG ATLAS Admin Plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-admin-plugin) — existing Python backend, Discord bot, frontend and backup scripts; reuse only after security and dependency review.
- [CMG ATLAS Resource Map Plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-resource-map-plugin) — existing icon/data material for GridForge; provenance and license must be checked before redistribution.

## Product modules

| Module | Repository | Domain |
|---|---|---|
| CMG Connect | [cmg-atlas-connect-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-connect-plugin) | Cross-grid chat, announcements, MOTDs, rules, invite links and Discord relay |
| CMG Commerce | [cmg-atlas-commerce-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-commerce-plugin) | Shop, wallet integration, catalogue, rewards, kits, entitlements and game delivery |
| CMG Player | [cmg-atlas-player-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-player-plugin) | Recovery, teleport, commands, tame tracking, spyglass, progression and equipment |
| CMG Tribes | [cmg-atlas-tribes-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-tribes-plugin) | Cluster-aware tribe membership, roles, ownership, teammate locations and activity |
| CMG Sentinel | [cmg-atlas-sentinel-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-sentinel-plugin) | RCON policy, admin/mod permissions, health, crash coordination and alerts |
| CMG World | [cmg-atlas-world-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-world-plugin) | Safe zones, collision, ownership, limits, farming policy, loot and spawns |
| CMG GridForge | [cmg-atlas-gridforge-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-gridforge-plugin) | Grid JSON authoring, visual maps and resource/creature validation |
| CMG Adventures | [cmg-atlas-adventures-plugin](https://github.com/Chaotic-Minds-Collective/cmg-atlas-adventures-plugin) | Quests, challenges, progression, events, travel objectives and rewards |

Each module repository contains its own README and `docs/IMPLEMENTATION_PLAN.md`.

## Legacy Kals feature rebuild map

Eight Kals archives were found in the 148-entry plugin catalogue: CorpseFinder, CrossChat, Engrams, FastTravel, Reusables, SoloFarm, Spyglass and Tribemates. The inspected archives contain compiled binaries and configuration/documentation, not editable source or an accompanying source license. Rebuild the feature sets independently; do not reuse DLL/EXE files or copy implementation code.

| Legacy feature set | CMG destination | Preserve and improve |
|---|---|---|
| Kals CorpseFinder | CMG Player — Recovery | Body and tribe-dino tracking/recovery, item-cache discovery, cooldowns, PvP and player-state restrictions; add ownership checks and anti-duplication |
| Kals CrossChat | CMG Connect | Global/tribe chat, Discord relay, tribe logs, player/server lists, private messages, moderation and role sync; add strict role/channel allowlists and audited server-scoped commands |
| Kals Engrams | CMG Player — Progression | Level-based unlock policy, boss unlock overrides, learnable/blocked lists and map requirement rules; make policy configurable and auditable |
| Kals FastTravel | CMG Player — Travel | Delayed teleport, cancel-on-damage, cooldown/PvP rules, allowed structures and passenger/dino eligibility; validate destination, ownership and combat state |
| Kals Reusables | CMG Player — Equipment | Grapple, zipline, bola, spear, sling, flare, parachute, whip, boomerang, fishing rod, climbing picks, scissors and torch behavior; capability-gate each item and preserve durability policy |
| Kals SoloFarm | CMG World — Farming | Auto-farm triggers, per-creature intervals, weight thresholds, permission groups, PvP cooldown and tribe limits; enforce server performance and anti-exploit caps |
| Kals Spyglass | CMG Player — Spyglass | Coordinates, creature stats/level, owner/tribe, wild/unconscious/dead state and inventory counts; add caching, throttling and privacy rules |
| Kals Tribemates | CMG Tribes | Teammate arrows/status, base and custom locations, tribe-scoped commands and colors; add multi-grid identity and privacy controls |

See [KALS_REBUILD_MAP.md](KALS_REBUILD_MAP.md) for the detailed feature inventory and safety requirements.

## Integration rules

1. Modules call Core through authenticated APIs. No game plugin or module receives Core database credentials.
2. The ATLAS adapter is the only boundary for native game callbacks and server operations.
3. Core is authoritative for CMG player identity, permissions, wallets, orders, entitlements and durable reward jobs.
4. ATLAS is authoritative for live in-game state such as current tribe membership, location, inventory and ship state until verified adapter hooks support controlled writes.
5. All events and state that cross grids retain cluster, grid and server context. Provider IDs are strings; display names are never identifiers.
6. Event consumers use idempotency keys and correlation IDs. Network failures must not block game-critical threads.
7. Feature capability checks prevent modules from calling hooks not confirmed by the installed server build.
8. Product configuration must never execute arbitrary RCON/console commands. Use typed, explicitly allowlisted actions with per-server authorization and audit records.

## Current implementation facts and gaps — 2026-11-17

- Core is a TypeScript/Node.js modular monolith using PostgreSQL and REST APIs. It supports public catalogue reads, idempotent order creation, Stripe test-mode checkout sessions, signed test-mode webhook verification, entitlement claim queueing and reward claim/acknowledgement leases. It does **not** expose a wallet balance/spend API, run a reward delivery worker, or grant ATLAS items itself.
- The Core commerce schema already contains wallet and ledger tables. Their presence is not a public wallet API; modules must not query them directly.
- The Commerce client now has a tested path for order creation, checkout capability/session requests and entitlement claim queueing. Stripe remains test-mode-only. Actual game delivery, wallet purchases, selling/transfers and full Atlas Shop parity remain pending.
- The native ATLAS 1.71 adapter uses the pinned SDK source and Windows x64 import library. It emits only `player.joined` and `player.left` through Core's server-scoped endpoint. Hooks are disabled by default and require isolated runtime validation; successful CI compilation is not proof of the deployed server ABI.
- Core's reward queue claim/ack contract is durable. A worker must resolve the player/server, perform a typed game grant, confirm the result and only then acknowledge delivery.
- Player, Tribes, Connect, World, Sentinel and Adventures contain service-side foundations and tests, not complete in-game native integrations. World remains observe-only; Adventures progress is not yet durable; Connect does not yet include a verified Discord runtime; Sentinel must not issue automated destructive RCON actions.
- GridForge remains gated on an authoritative, versioned ATLAS grid JSON sample/schema and a provenance/license review of map assets.
- The Drive `AtlasApi_1.71.zip` is incomplete: it lacks the public headers and `AtlasApi.lib`. Use the pinned `cmg-atlas-api` GitHub commit referenced in `ATLAS_SDK_INTEGRATION.md` instead.
- The Drive `AtlasPluginSuite.zip` variants contain scaffold/stub artifacts, including tiny placeholder `.lib` files; they are not native SDK inputs or production-ready plugins.
- The Drive central-hub archive and cluster-manager scripts are legacy references only. The hub has default RCON credentials and an automatic `DoExit` restart schedule; the cluster script contains a placeholder password and references a missing RCON helper. Do not deploy or run them.

## Recommended delivery sequence

1. Finish Commerce's Core order/checkout/claim client and define the Core wallet API/atomic wallet purchase contract.
2. Validate the native adapter in an isolated, disposable ATLAS 1.71 server before enabling any gameplay hooks.
3. Complete Core-backed Tribes, Player and Connect contracts, preserving read-only behavior until native capabilities are verified.
4. Implement the Kals-derived Player, Tribes, Connect and World feature slices with permissions, cooldowns, privacy, performance limits and anti-duplication controls.
5. Build the reward delivery worker and item/creature grant adapter; only then enable Commerce kits and purchases.
6. Add World enforcement, Adventures persistence/rewards and GridForge schema-backed tooling.
7. Run cross-module integration, security and multi-grid failure/recovery tests.

No production server changes, paid services, live payments or public binary releases are authorized by this planning document.
