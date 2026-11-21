# CMG ATLAS Plugin

Original, modular server-side plugins for the **Chaotic Minds Gaming (CMG)** ATLAS network.

## Shared architecture

- [CMG Core](https://github.com/Chaotic-Minds-Collective/cmg-atlas-core-plugin) owns identity, scoped permissions, platform APIs, durable events, commerce, entitlements, and reward jobs.
- This repository owns the ATLAS-native adapter boundary and verified game integration contracts.
- Product modules live in separate repositories and use authenticated Core APIs; they do not connect directly to PostgreSQL.

See the [complete Chaotic Minds Gaming ATLAS Plugin Suite](docs/PLUGIN_SUITE.md) for module links, ownership boundaries, dependencies, known gaps, and delivery order.

## ATLAS SDK dependency

The native Windows target fetches **cmg-atlas-api** at pinned commit
[`d46a44993d65f22d2497f040c6979248d0ad0dd4`](https://github.com/Chaotic-Minds-Collective/cmg-atlas-api/commit/d46a44993d65f22d2497f040c6979248d0ad0dd4).
This revision includes failure-safe hook management, plugin path validation,
dependency-first loading, ATLAS player-cache/world-save compatibility fixes,
and fail-closed handling for missing native offset symbols. The adapter also
runs a startup sentinel lookup diagnostic and reports the exact pinned API
commit in its lifecycle log. The public headers and matching import library remain under
`version/Core/Public` and `out_lib/AtlasApi.lib`; CMake verifies both exist
before building. The API repository's `LICENSE.TXT` is MIT; retain its
copyright and license notice when redistributing substantial portions or SDK
artifacts.

The SDK link-check export references `ArkApi::GetCommands()` to ensure the compiler sees the real ATLAS headers and the linker resolves the import library. The adapter also samples server readiness/player count through the ATLAS API and can publish player join/leave events to Core. Gameplay hooks remain disabled unless explicitly opted in after isolated-server validation. When enabled, the adapter pins its DLL module for the server process lifetime before installing detours. `Plugin_Unload` disables event publishing and leaves the detours as pass-throughs; a server process restart is required to fully remove them.

## Server-independent development

The host runtime now includes a deterministic mock event contract and test stream. This allows module development and integration testing before a dedicated ATLAS server is available.

- [Mock event contract and usage](runtime/host/docs/MOCK_EVENT_CONTRACT.md)
- Run host tests from `runtime/host` with `npm test`.

The mock stream is test-only. It does not connect to a game server, issue RCON commands, grant items, or modify game state.

## Compatibility status

**Not yet validated against a live ATLAS server.** The pinned ATLAS 1.71 headers/import library and adapter compile are validated by Windows x64 CI, but that does not prove the plugin loads against the installed server runtime. The supplied runtime PDB identifies an MSVC 14.34 linker while the API project file declares toolset v142. Exact server build, loader, runtime API version, callback lifecycle, threading model, and ABI still require an isolated server test before native gameplay features are enabled.

## Repository rules

- Do not copy third-party source, assets, or proprietary designs.
- Each module has its own repository, manifest, tests, documentation, and version.
- No plugin receives database credentials; use authenticated, least-privilege Core APIs.
- Privileged actions fail closed when authorization cannot be verified.
- Network failures must not crash or block game-critical threads.
- Do not test on a live production server.


## Core-backed native reward delivery

The adapter contains a disabled-by-default Core reward worker. It polls the server-scoped Core reward queue on a background thread, renews active leases, and dispatches any game-side action through the ATLAS timer callback on the game thread. A durable append-only journal prevents automatic re-grants after an ambiguous worker restart; uncertain cases are parked in Core's `manual_review` state for audited administrator reconciliation.

The worker requires all of the following environment variables and will not claim rewards unless every gate is satisfied:

- `CHAOTIC_ATLAS_ENABLE_REWARD_DELIVERY=1`
- `CHAOTIC_ATLAS_GIVE_ITEM_ABI_VERIFIED=1` — explicit operator attestation after validating the native `AShooterPlayerController.GiveItem` signature against the exact isolated server build. This method is described in the uploaded build guide but is not declared in the pinned public API headers.
- `CHAOTIC_ATLAS_REWARD_VERIFIED_SERVER_BUILD` — must exactly match `ATLAS_SERVER_BUILD` observed at runtime.
- `CHAOTIC_ATLAS_REWARD_VERIFIED_API_VERSION` — must exactly match `ATLAS_SERVER_API_VERSION` observed at runtime and the adapter's pinned API version.
- `CHAOTIC_ATLAS_REWARD_ITEM_MAP_JSON` — server-owned JSON object mapping safe item keys to exact, reviewed Blueprint paths.
- `CHAOTIC_ATLAS_REWARD_JOURNAL_PATH` — writable durable journal path on the same server host.

Do not set these variables on production until the native method ABI, item metadata persistence, player disconnect behavior, worker restart behavior, and acknowledgement-loss scenarios have passed on an isolated test server. The C++ build alone does not establish live ATLAS compatibility.
