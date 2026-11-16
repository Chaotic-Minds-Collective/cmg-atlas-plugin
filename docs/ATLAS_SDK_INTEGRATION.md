# ATLAS SDK integration boundary

## Pinned dependency

- Repository: https://github.com/Chaotic-Minds-Collective/cmg-atlas-api
- Commit: `1bc2be00b5841069d816ff6c9d85486b13cfc252`
- Public include root: `version/Core/Public`
- ATLAS import library: `out_lib/AtlasApi.lib`
- License: MIT, from the API repository's `LICENSE.TXT`

CMake fetches this exact commit and checks that both the public Atlas header and import library exist. Do not replace the commit with a moving branch or use one of the incomplete scaffold archives as a substitute.

## Link check

`plugins/ChaoticGamingCore/src/atlas_sdk_link_check.cpp` includes
`<API/Atlas/Atlas.h>` and exports a diagnostic function that references
`ArkApi::GetCommands()`. The function is not called by plugin initialization.
Its purpose is to force a real header compile and import-library link in CI
without activating game callbacks or making assumptions about server state.

## What this does and does not prove

The prior static comparison found the SDK import library's 85 named import
symbols aligned with the supplied runtime DLL's 85 named exports, and the
supplied PDB matches that DLL's embedded CodeView identity. This is useful
evidence that the SDK/runtime pair belongs together.

It is not a complete ABI or runtime-compatibility test. In particular:

- The supplied runtime PDB reports an MSVC 14.34 linker.
- The API project file declares platform toolset v142.
- The plugin loader must still be verified against the exact installed ATLAS
  server build and plugin metadata.
- No gameplay hook, chat command, RCON command, or player event should be
  enabled until the isolated loader test passes.

## Next validation gates

1. Confirm CI can fetch the pinned source and compile/link the SDK smoke export.
2. Confirm the exact server executable build and installed API runtime.
3. Load the plugin in an isolated, disposable ATLAS server instance.
4. Verify plugin initialization/unload and log behavior.
5. Only then enable the opt-in player event hooks in an isolated instance and
   verify the process-lifetime passthrough behavior on plugin unload/reload.

## Player lifecycle event bridge

The native adapter now contains read-only hooks for `AShooterGameMode.HandleNewPlayer` and `AShooterGameMode.Logout`. They map to Core's normalized `player.joined` and `player.left` event types and publish to `POST /integrations/servers/{serverId}/events` using the server-scoped `events.publish` permission. Events are captured on the game thread, queued in memory, and sent by the existing worker thread; network I/O never runs inside a game callback. The queue is bounded and retries retain the same event ID for Core's idempotent ingestion.

These hooks are **disabled by default**. Configure `CHAOTIC_CORE_CLUSTER_ID` and `CHAOTIC_CORE_GRID_ID`, issue a service credential with both `servers.heartbeat` and `events.publish`, then set `CHAOTIC_ATLAS_ENABLE_PLAYER_EVENTS=1` only after validating the exact server build in an isolated, disposable ATLAS instance. CI proves compilation and contract serialization, not live hook ABI or gameplay behavior. Do not enable this on a production server until that isolated test passes.

## Hook lifetime and unload behavior

Player lifecycle hooks are opt-in and remain disabled by default. Before installing either detour, the adapter pins its DLL module with the Windows module pin flag. This is deliberate: the pinned SDK hook manager does not provide a verified in-flight callback drain contract, and hook removal can fail. Destroying a trampoline while a callback is still executing could leave the server with a dangling callback or original-function pointer.

When the hooks are installed, they remain attached for the lifetime of the server process. `Plugin_Unload` atomically disables event capture and stops the network worker; the detours remain as pass-through wrappers that call the original ATLAS methods. A plugin reload reuses the already-installed hooks and resumes event capture only when `CHAOTIC_ATLAS_ENABLE_PLAYER_EVENTS=1` and the required cluster/grid configuration is valid. A partial hook setup fails closed in passthrough mode and requires a server restart before retrying.

This process-lifetime pin is a safety trade-off, not a claim that the hooks have passed live ABI validation. Keep the feature disabled until the exact server build passes an isolated test.
