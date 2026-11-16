// Compile/link smoke check for the pinned ATLAS SDK.
// This export is deliberately not called by Plugin_Init: live runtime behavior
// remains gated on an isolated server compatibility test.
#include <API/Atlas/Atlas.h>

extern "C" __declspec(dllexport) ArkApi::ICommands* __fastcall
CG_AtlasSdkLinkCheck() noexcept {
    return &ArkApi::GetCommands();
}
