# CMG ATLAS Plugin

Original, modular server-side plugins for the **Chaotic Minds Gaming (CMG)** ATLAS network.

## Shared architecture

- [CMG Core](https://github.com/Chaotic-Minds-Collective/cmg-atlas-core-plugin) owns identity, scoped permissions, platform APIs, durable events, commerce, entitlements, and reward jobs.
- This repository owns the ATLAS-native adapter boundary and verified game integration contracts.
- Product modules live in separate repositories and use authenticated Core APIs; they do not connect directly to PostgreSQL.

See the [complete Chaotic Minds Gaming ATLAS Plugin Suite](docs/PLUGIN_SUITE.md) for module links, ownership boundaries, dependencies, known gaps, and delivery order.

## ATLAS SDK dependency

The native Windows target fetches **cmg-atlas-api** at pinned commit
[`144ee5301177428894eda4c318276f52f051d3f5`](https://github.com/Chaotic-Minds-Collective/cmg-atlas-api/commit/144ee5301177428894eda4c318276f52f051d3f5).
The headers are consumed from `version/Core/Public` and the matching import
library is `out_lib/AtlasApi.lib`. CMake verifies both exist before building.
The API repository's `LICENSE.TXT` is MIT; retain its copyright and license
notice when redistributing substantial portions or SDK artifacts.

The SDK link-check export references `ArkApi::GetCommands()` to ensure the
compiler sees the real ATLAS headers and the linker resolves the import
library. It is not called automatically and does not register gameplay hooks
or commands.

## Server-independent development

The host runtime now includes a deterministic mock event contract and test stream. This allows module development and integration testing before a dedicated ATLAS server is available.

- [Mock event contract and usage](runtime/host/docs/MOCK_EVENT_CONTRACT.md)
- Run host tests from `runtime/host` with `npm test`.

The mock stream is test-only. It does not connect to a game server, issue RCON commands, grant items, or modify game state.

## Compatibility status

**Not yet validated against a live ATLAS server.** The SDK headers/import library are now a pinned build dependency, but that does not prove the plugin loads against the installed server runtime. The supplied runtime PDB identifies an MSVC 14.34 linker while the API project file declares toolset v142. CI must verify compilation/linking; exact server build, loader, runtime API version, callback lifecycle, threading model, and ABI still require an isolated server test before native gameplay features are enabled.

## Repository rules

- Do not copy third-party source, assets, or proprietary designs.
- Each module has its own repository, manifest, tests, documentation, and version.
- No plugin receives database credentials; use authenticated, least-privilege Core APIs.
- Privileged actions fail closed when authorization cannot be verified.
- Network failures must not crash or block game-critical threads.
- Do not test on a live production server.
