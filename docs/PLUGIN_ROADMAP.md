# ATLAS Plugin Roadmap

The ATLAS API 1.71 SDK is pinned and the native adapter builds in Windows x64 CI. The player join/leave hooks are implemented but remain disabled by default. Before enabling them, prove the loader and callback ABI against the exact target server build in an isolated, disposable test environment. The next delivery gate is that isolated load test, followed by read-only Sentinel and Tribes integration. See the central [plugin catalogue](https://github.com/Chaotic-Minds-Collective/cmg-docs/blob/main/plugins/PLUGIN_CATALOG.md).
