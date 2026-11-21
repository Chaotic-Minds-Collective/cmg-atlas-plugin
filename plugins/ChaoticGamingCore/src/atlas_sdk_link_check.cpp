#include <string>

// Compile/link smoke check for the pinned ATLAS SDK.
// This export is deliberately not called by Plugin_Init: live runtime behavior
// remains gated on an isolated server compatibility test.
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
