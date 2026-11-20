# ATLAS Compatibility Matrix

| Component | Target | Status |
|---|---|---|
| ATLAS dedicated server | Exact deployed build | Not verified; isolated load test required |
| ATLAS Server API | API 1.71, pinned SDK commit `a757d0c256e6d221742fb131d46d8049c12815a6` | Windows x64 API build and adapter compile are CI-validated; server runtime not verified |
| Compiler/toolchain | Windows x64 CMake/MSVC CI | Pinned through CI and build workflow; compile verified |
| Core adapter protocol | HTTPS heartbeat + server-scoped game-event ingestion | Heartbeat and `player.joined`/`player.left` payload contracts tested; opt-in detours are process-lifetime pinned; live credentials and end-to-end delivery not verified |

The pinned API revision includes dependency-first plugin loading, plugin path validation, and failure-safe hook registration/removal. A compatibility claim still requires a successful isolated load test with logs and exact version identifiers.
