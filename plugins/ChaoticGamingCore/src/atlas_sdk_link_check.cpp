#include <string>

// Compile/link smoke check for the pinned ATLAS SDK.
// The SDK link-check export is not called by Plugin_Init. The sentinel export
// below is called at startup; live gameplay compatibility still requires an
// isolated server test.
#include <API/Atlas/Atlas.h>

extern "C" __declspec(dllexport) ArkApi::ICommands* __fastcall
CG_AtlasSdkLinkCheck() noexcept {
    return &ArkApi::GetCommands();
}

extern "C" __declspec(dllexport) bool __fastcall
CG_AtlasMissingSymbolLookupFailsClosed() noexcept {
    // This sentinel is intentionally not a real SDK symbol. The merged API
    // revision must return nullptr rather than synthesize a base-relative address.
    static const std::string missingSymbol =
        "__CMG_ATLAS_DIAGNOSTIC_MISSING_SYMBOL_7F3A91D2__";
    return GetAddress(missingSymbol) == nullptr;
}
