# Kals Plugin Feature Rebuild Map

## Source and licensing boundary

The plugin catalogue contains eight Kals archives. The inspected ZIPs contain compiled DLLs (and for CrossChat, a separate EXE), configuration files and limited documentation. No editable source or source license was present in those archives. Do not run, redistribute, decompile, or copy the compiled binaries. Use the configuration/documentation only to understand observable feature requirements, and implement original CMG code against the pinned ATLAS SDK.

## Feature inventory and destination

### 1. Kals CorpseFinder → CMG Player / Recovery

Preserve:
- List and track player bodies; show remaining time, distance, map coordinates and cached item counts.
- List tribe-owned dead dinos and retrieve eligible dinos.
- Configurable suicide, body list, body tracking, body retrieval and dino retrieval commands.
- Permission groups, points/token costs, cooldowns, post-respawn delays and PvP cooldowns.
- Restrictions for knocked-out, handcuffed, carried or grappled players; optional structure-distance rules.

Improve:
- Verify Steam identity, body/dino ownership, tribe membership and current grid before any retrieval.
- Use durable recovery claims and idempotency keys; never duplicate inventory or dinos after disconnect/retry.
- Revalidate PvP/combat and player state immediately before committing a recovery.
- Make fees a Core wallet transaction only after a successful, validated recovery request.

### 2. Kals CrossChat → CMG Connect

Preserve:
- Cross-grid global chat and player-to-Discord chat.
- Tribe chat and tribe-log relays to player-managed Discord channels.
- Join/leave notices, live player lists, server-status channels and private messages.
- Permission-aware tags, icons, map labels, chat colors, localization and random tips.
- Mutes, un-mutes, word filtering, spam delay, ignore lists and role/permission synchronization.
- Administrative RCON functionality only as a separately secured, optional capability.

Improve:
- Do not reproduce the original README's warning that its RCON channel has no permission checks.
- Require explicit Discord guild/channel allowlists, trusted role IDs, per-command authorization, per-server scope and audit logs.
- Do not request Administrator bot permissions by default; use least-privilege Discord scopes and permissions.
- Validate and rate-limit all chat/command payloads; prevent chat text from becoming RCON syntax.
- Use Core outbox delivery and durable retries; never block game callbacks on network I/O.

### 3. Kals Engrams → CMG Player / Progression

Preserve:
- Level-based automatic engram unlocks and player opt-in/opt-out policy.
- Boss-specific engram additions/removals and map requirement overrides.
- Learnable allow/block lists, exact/partial blueprint matching and notification controls.

Improve:
- Make forced unlocks and boss overrides explicit, versioned server policy.
- Validate every engram identifier against the target build and map capability set.
- Audit admin policy changes and make progression behavior deterministic across grids.

### 4. Kals FastTravel → CMG Player / Travel

Preserve:
- Delayed teleport with progress/cancel messaging.
- Cancellation on damage or punching; standard and PvP cooldowns.
- Permission-group-specific delay, cooldown, point cost and passenger/dino eligibility.
- Allowed source/destination structures and optional bed destruction after use.

Improve:
- Revalidate destination, structure ownership, safe-zone restrictions, combat state, passenger consent and cross-grid availability at commit time.
- Use a durable teleport state machine so disconnects or server restarts cannot duplicate or strand entities.
- Define `accepted` as a durable Core teleport record created after all eligibility checks and before the countdown begins. Debit with a stable teleport idempotency key at that transition; if damage, timeout, disconnect or another failure cancels the teleport before confirmed arrival, issue exactly one compensating refund with its own stable idempotency key. Arrival is the terminal state that keeps the debit.

### 5. Kals Reusables → CMG Player / Equipment

Preserve configurable reusable behavior for grappling hooks, ziplines, bolas, spears, slings, flares, parachutes, whips, boomerangs, fishing rods, climbing picks, scissors and torches.

Improve:
- Gate each item behind a verified item/build capability.
- Define durability, stack and consumption behavior per item rather than one global bypass.
- Preserve PvP/server rules and prevent infinite-use behavior from bypassing intended cooldowns or item-state transitions.

### 6. Kals SoloFarm → CMG World / Farming

Preserve:
- Auto-farm while carrying or mounting configured farming creatures.
- Per-creature swing intervals, forced/normal swing mode and stop-at-weight thresholds.
- Permission groups, PvP cooldown blocks and per-tribe active farmer limits.

Improve:
- Treat a zero/unlimited tribe cap as unsafe by default; require explicit server configuration for unlimited use.
- Enforce global and per-tribe work budgets, adaptive scheduling and server tick/load protection.
- Revalidate creature ownership, allowed carrier, inventory capacity, resource rules and PvP state on every scheduled action.

### 7. Kals Spyglass → CMG Player / Spyglass

Preserve:
- Target coordinates and optional map-size override.
- Creature level, stats, health/torpor, wild/unconscious/dead state, rider, owner and tribe.
- Inventory/item counts and configurable stats/labels.
- Permission groups and optional requirement for spyglass in hotbar/targeting.

Improve:
- Replace the original 200 ms refresh loop with event-driven updates and bounded caching (the sample config also specifies a 1,000 ms info cache).
- Enforce privacy for enemy creature stats and inventory; expose only what server policy permits.
- Avoid per-frame scans and rate-limit expensive inventory reads.

### 8. Kals Tribemates → CMG Tribes

Preserve:
- Teammate arrows/names and living/dead status.
- Tribe base discovery and player-defined custom locations.
- Commands to show teammates, set/clear base and save/remove custom locations.
- Configurable display duration, cooldown, custom-location count and colors.

Improve:
- Scope all location queries to verified tribe membership and current grid permissions.
- Add multi-grid location freshness, privacy controls, stale-data indicators and per-player rate limits.
- Validate base discovery and custom-location ownership before publishing locations.

## Shared implementation requirements

- Core owns permissions, wallet transactions, reward claims and durable cross-grid records.
- The native adapter is the only component allowed to read or mutate live ATLAS state.
- No raw Unreal object pointer may be retained across callbacks; use validated weak/identity handles and game-thread revalidation.
- Network/database work stays off game-critical threads; all Unreal operations return to the game thread.
- Every privileged action is typed, allowlisted, permission-checked, rate-limited and audited.
- Test disconnects, server restart, duplicate requests, stale identities, cross-grid transfer and rollback before enabling a feature.
