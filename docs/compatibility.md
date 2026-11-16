# ATLAS Compatibility Matrix

| Component | Target | Status |
|---|---|---|
| ATLAS dedicated server | Exact deployed build | Not verified; isolated load test required |
| ATLAS Server API | API 1.71, pinned SDK commit `1bc2be00b5841069d816ff6c9d85486b13cfc252` | Windows x64 compile verified; server runtime not verified |
| Compiler/toolchain | Windows x64 CMake/MSVC CI | Pinned through CI and build workflow; compile verified |
| Core adapter protocol | HTTPS heartbeat + server-scoped game-event ingestion | Heartbeat and `player.joined`/`player.left` payload contracts tested; opt-in detours are process-lifetime pinned; live credentials and end-to-end delivery not verified |

A compatibility claim requires a successful isolated load test with logs and exact version identifiers.
