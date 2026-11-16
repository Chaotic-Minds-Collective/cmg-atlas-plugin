# ATLAS SDK integration boundary

## Pinned dependency

- Repository: https://github.com/Chaotic-Minds-Collective/cmg-atlas-api
- Commit: `144ee5301177428894eda4c318276f52f051d3f5`
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
5. Only then implement a harmless diagnostic command and one read-only player
   event hook, with explicit cleanup on unload.
