#if defined(_WIN32)
#include <Windows.h>
#include <WinHttp.h>
#include <ICommands.h>
#include <IHooks.h>
#include <Atlas/AtlasApiUtils.h>
#include <API/ARK/Inventory.h>
#include <json.hpp>
#include "chaotic/atlas/heartbeat_payload.hpp"
#include "chaotic/atlas/event_payload.hpp"
#include "chaotic/atlas/reward_delivery.hpp"
#include <atomic>
#include <cctype>
#include <cwctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define CG_EXPORT extern "C" __declspec(dllexport)
#define CG_CALL __fastcall

#ifndef CG_PLUGIN_VERSION
#define CG_PLUGIN_VERSION "0.2.0"
#endif
#ifndef CG_ATLAS_API_VERSION
#define CG_ATLAS_API_VERSION 1.71
#endif
#define CG_STRINGIFY_DETAIL(value) #value
#define CG_STRINGIFY(value) CG_STRINGIFY_DETAIL(value)

namespace {
struct PendingEvent {
    unsigned long long generation{};
    std::string eventId;
    std::string eventType;
    std::string occurredAt;
    std::string payloadJson;
};

struct PendingReward {
    std::string id;
    std::string leaseToken;
    std::string idempotencyKey;
    std::string targetExternalId;
    std::string itemKey;
    int quantity{};
};

enum class RewardWorkStatus { delivered, failed, manual_review, lease_lost };

struct RewardWorkResult {
    RewardWorkStatus status{RewardWorkStatus::failed};
    std::string error;
    nlohmann::json receipt{nlohmann::json::object()};
};

struct RewardWorkItem {
    PendingReward reward;
    std::mutex mutex;
    std::condition_variable condition;
    bool done{false};
    bool leaseLost{false};
    RewardWorkResult result;
};

struct RewardJournalEntry {
    std::string rewardId;
    std::string targetExternalId;
    std::string itemKey;
    int quantity{};
    std::string state;
};

enum class EventPublishResult { Drained, RetryLater, HeartbeatDue };

using HandleNewPlayerFunction = bool(CG_CALL*)(AShooterGameMode*, AShooterPlayerController*, UPrimalPlayerData*, AShooterCharacter*, bool);
using LogoutFunction = void(CG_CALL*)(AShooterGameMode*, AController*);
HandleNewPlayerFunction g_originalHandleNewPlayer = nullptr;
LogoutFunction g_originalLogout = nullptr;

constexpr char kPluginName[] = "Chaotic Gaming Core";
constexpr char kAdapterVersion[] = CG_PLUGIN_VERSION;
constexpr char kCandidateApiVersion[] = CG_STRINGIFY(CG_ATLAS_API_VERSION);

std::atomic<bool> g_stop{true};
// Serializes worker ownership and configuration across loader callbacks.
std::mutex g_lifecycleMutex;
std::mutex g_waitMutex;
std::condition_variable g_waitCondition;
std::thread g_heartbeatThread;
std::string g_coreApiUrl;
std::string g_serviceToken;
std::string g_serverId;
std::string g_clusterId;
std::string g_gridId;
std::string g_serverBuild{"unknown"};
unsigned int g_intervalSeconds{60};
std::atomic<bool> g_eventPublishingConfigured{false};
bool g_joinHookInstalled{false};
bool g_logoutHookInstalled{false};
bool g_hooksPinnedPassthrough{false};
std::atomic<bool> g_eventRequested{false};
std::atomic<unsigned long long> g_eventSequence{0};
// Guards event capture, configuration replacement, and queue generation.
std::mutex g_eventMutex;
unsigned long long g_eventGeneration{0};
std::deque<PendingEvent> g_eventQueue;
constexpr std::size_t kMaximumPendingEvents = 1000;
constexpr char kServerStateCallbackId[] = "ChaoticGamingCore.ServerState";
std::atomic<bool> g_serverReady{false};
std::atomic<bool> g_heartbeatRequested{true};
std::atomic<unsigned int> g_playerCount{0};
bool g_serverStateCallbackRegistered{false};

constexpr char kRewardCallbackId[] = "ChaoticGamingCore.RewardDelivery";
std::atomic<bool> g_rewardDeliveryConfigured{false};
bool g_rewardCallbackRegistered{false};
std::thread g_rewardThread;
std::mutex g_rewardMutex;
std::condition_variable g_rewardCondition;
std::deque<std::shared_ptr<RewardWorkItem>> g_pendingRewards;
std::mutex g_rewardJournalMutex;
std::unordered_map<std::string, RewardJournalEntry> g_rewardJournal;
std::unordered_map<std::string, std::string> g_rewardBlueprints;
std::string g_rewardJournalPath;

std::string readEnvironment(const char* name) {
    const DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
    if (required == 0 || required > 4096) return {};
    std::vector<char> buffer(required);
    const DWORD copied = GetEnvironmentVariableA(name, buffer.data(), required);
    if (copied == 0 || copied >= required) return {};
    return std::string(buffer.data(), copied);
}

void logMessage(const char* message) noexcept {
    OutputDebugStringA(message);
}

void logLifecycle(const char* eventName) noexcept {
    char serverBuild[128]{};
    char runtimeApiVersion[64]{};
    char message[512]{};

    const std::string build = readEnvironment("ATLAS_SERVER_BUILD");
    const std::string runtimeApi = readEnvironment("ATLAS_SERVER_API_VERSION");
    std::snprintf(serverBuild, sizeof(serverBuild), "%s", build.empty() ? "unreported" : build.c_str());
    std::snprintf(runtimeApiVersion, sizeof(runtimeApiVersion), "%s", runtimeApi.empty() ? "unreported" : runtimeApi.c_str());

    std::snprintf(
        message,
        sizeof(message),
        "[%s] %s | pluginVersion=%s | serverBuild=%s | runtimeApi=%s | candidateApi=%s\n",
        kPluginName, eventName, kAdapterVersion, serverBuild, runtimeApiVersion, kCandidateApiVersion
    );
    logMessage(message);
}

bool isUuid(const std::string& value) noexcept {
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' ||
        value[18] != '-' || value[23] != '-') return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    const char version = value[14];
    const char variant = static_cast<char>(std::tolower(static_cast<unsigned char>(value[19])));
    return version >= '1' && version <= '8' &&
        (variant == '8' || variant == '9' || variant == 'a' || variant == 'b');
}

bool isGridId(const std::string& value) noexcept {
    if (value.empty() || value.size() > 200) return false;
    for (const unsigned char ch : value) {
        if (std::isspace(ch) == 0) return true;
    }
    return false;
}

bool isServiceToken(const std::string& value) noexcept {
    constexpr char prefix[] = "cg_srv_";
    if (value.rfind(prefix, 0) != 0 || value.size() < 47) return false;
    for (std::size_t i = sizeof(prefix) - 1; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (!std::isalnum(ch) && ch != '_' && ch != '-') return false;
    }
    return true;
}

std::string formatUtcTimestamp(const SYSTEMTIME& value) {
    char timestamp[40]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute,
        value.wSecond, value.wMilliseconds);
    return timestamp;
}

std::string makeEventId(const SYSTEMTIME& value) {
    const auto sequence = g_eventSequence.fetch_add(1, std::memory_order_relaxed);
    return chaotic::atlas::makeGameEventId(g_serverId, formatUtcTimestamp(value), sequence);
}

std::string toUtf8(const FString& value) {
    const TCHAR* source = *value;
    if (!source || *source == 0) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, source, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string converted(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, source, -1,
        converted.data(), required, nullptr, nullptr);
    if (written <= 0) return {};
    converted.resize(static_cast<std::size_t>(written - 1));
    return converted;
}

bool makePlayerLifecycleEvent(const char* eventType, AController* controller,
    bool isFromLogin, PendingEvent& event) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        if (!g_eventPublishingConfigured.load(std::memory_order_acquire) || !controller) return false;
        event.generation = g_eventGeneration;
        const uint64 platformId = ArkApi::IApiUtils::GetSteamIdFromController(controller);
        if (platformId == 0) return false;

        std::string playerName;
        if (APlayerState* playerState = controller->PlayerStateField()) {
            const FString name = playerState->PlayerNameField();
            playerName = toUtf8(name);
        }
        const uint64 gamePlayerId = ArkApi::IApiUtils::GetPlayerID(controller);
        SYSTEMTIME now{};
        GetSystemTime(&now);
        event.eventId = makeEventId(now);
        event.eventType = eventType;
        event.occurredAt = formatUtcTimestamp(now);
        event.payloadJson = chaotic::atlas::makePlayerLifecyclePayload(
            std::to_string(platformId), playerName, std::to_string(gamePlayerId), isFromLogin);
        return true;
    } catch (...) {
        logMessage("[Chaotic Gaming Core] Could not capture ATLAS player identity for an event.\n");
        return false;
    }
}

void enqueueEvent(PendingEvent event) noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(g_eventMutex);
            // A logout callback can finish after unload or even after a reload.
            if (!g_eventPublishingConfigured.load(std::memory_order_acquire) ||
                event.generation != g_eventGeneration) return;
            if (g_eventQueue.size() >= kMaximumPendingEvents) {
                logMessage("[Chaotic Gaming Core] Event queue full; newest player event dropped.\n");
                return;
            }
            g_eventQueue.push_back(std::move(event));
        }
        g_eventRequested.store(true, std::memory_order_release);
        g_waitCondition.notify_all();
    } catch (...) {
        logMessage("[Chaotic Gaming Core] Could not queue an ATLAS player event.\n");
    }
}

bool hasPendingEvents() noexcept {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    return !g_eventQueue.empty();
}

bool CG_CALL handleNewPlayerHook(AShooterGameMode* gameMode,
    AShooterPlayerController* newPlayer, UPrimalPlayerData* playerData,
    AShooterCharacter* playerCharacter, bool isFromLogin) noexcept {
    const bool accepted = g_originalHandleNewPlayer &&
        g_originalHandleNewPlayer(gameMode, newPlayer, playerData, playerCharacter, isFromLogin);
    if (accepted && newPlayer) {
        PendingEvent event;
        if (makePlayerLifecycleEvent("player.joined", newPlayer, isFromLogin, event)) {
            enqueueEvent(std::move(event));
        }
    }
    return accepted;
}

void CG_CALL logoutHook(AShooterGameMode* gameMode, AController* exiting) noexcept {
    PendingEvent event;
    const bool captured = makePlayerLifecycleEvent("player.left", exiting, false, event);
    if (g_originalLogout) g_originalLogout(gameMode, exiting);
    if (captured) enqueueEvent(std::move(event));
}

struct WinHttpHandle {
    HINTERNET value{};
    ~WinHttpHandle() { if (value) WinHttpCloseHandle(value); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    explicit WinHttpHandle(HINTERNET handle) : value(handle) {}
};

void refreshServerState() noexcept {
    bool ready = false;
    unsigned int playerCount = 0;
    try {
        auto& apiUtils = ArkApi::GetApiUtils();
        if (apiUtils.GetStatus() == ArkApi::ServerStatus::Ready) {
            if (AShooterGameMode* gameMode = apiUtils.GetShooterGameMode()) {
                const int count = gameMode->GetNumPlayers();
                playerCount = count > 0 ? static_cast<unsigned int>(count) : 0;
                ready = true;
            }
        }
    } catch (...) {
        logMessage("[Chaotic Gaming Core] ATLAS state poll failed; retrying on the next timer tick.\n");
    }

    g_playerCount.store(playerCount, std::memory_order_relaxed);
    const bool previousReady = g_serverReady.exchange(ready, std::memory_order_acq_rel);
    if (previousReady != ready) {
        g_heartbeatRequested.store(true, std::memory_order_release);
        g_waitCondition.notify_all();
    }
}

bool sendCorePost(const std::wstring& requestPath, const std::string& body, const char* operation,
    std::string* responseBody = nullptr) {
    std::wstring wideUrl(g_coreApiUrl.begin(), g_coreApiUrl.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = static_cast<DWORD>(-1);
    parts.dwPasswordLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        logMessage("[Chaotic Gaming Core] Invalid CORE_API_URL; Core request skipped.\n");
        return false;
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring basePath(parts.lpszUrlPath, parts.dwUrlPathLength);
    if ((!secure && parts.nScheme != INTERNET_SCHEME_HTTP) ||
        (!basePath.empty() && basePath != L"/") || parts.dwExtraInfoLength != 0 ||
        parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) {
        logMessage("[Chaotic Gaming Core] CORE_API_URL must be an HTTP(S) origin without credentials, path or query.\n");
        return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring normalizedHost = host;
    for (wchar_t& ch : normalizedHost) ch = static_cast<wchar_t>(std::towlower(ch));
    const bool loopbackHost = normalizedHost == L"localhost" ||
        normalizedHost == L"127.0.0.1" || normalizedHost == L"::1";
    if (!secure && !loopbackHost) {
        logMessage("[Chaotic Gaming Core] HTTPS is required except for localhost development.\n");
        return false;
    }

    const std::wstring headers =
        L"Authorization: Bearer " + std::wstring(g_serviceToken.begin(), g_serviceToken.end()) +
        L"\r\nContent-Type: application/json\r\nAccept: application/json\r\n";
    WinHttpHandle session(WinHttpOpen(L"ChaoticGamingAtlasAdapter/0.2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) {
        logMessage("[Chaotic Gaming Core] WinHTTP session could not be created.\n");
        return false;
    }
    if (!WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000)) {
        logMessage("[Chaotic Gaming Core] WinHTTP timeouts could not be configured; request skipped.\n");
        return false;
    }

    WinHttpHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value) {
        logMessage("[Chaotic Gaming Core] Core API connection could not be opened.\n");
        return false;
    }
    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(connection.value, L"POST", requestPath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request.value) {
        logMessage("[Chaotic Gaming Core] Core request could not be created.\n");
        return false;
    }

    DWORD disableRedirects = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
        &disableRedirects, sizeof(disableRedirects))) {
        logMessage("[Chaotic Gaming Core] Redirect protection could not be enabled; request skipped.\n");
        return false;
    }

    const BOOL sent = WinHttpSendRequest(request.value, headers.c_str(),
        static_cast<DWORD>(headers.size()), const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request.value, nullptr)) {
        logMessage("[Chaotic Gaming Core] Core request transport failed; it will retry.\n");
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.value,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        statusCode < 200 || statusCode >= 300) {
        char message[192]{};
        std::snprintf(message, sizeof(message),
            "[Chaotic Gaming Core] %s rejected (HTTP %lu); it will retry.\n",
            operation, static_cast<unsigned long>(statusCode));
        logMessage(message);
        return false;
    }

    if (responseBody) {
        responseBody->clear();
        constexpr std::size_t kMaximumResponseBytes = 1024 * 1024;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.value, &available)) {
                logMessage("[Chaotic Gaming Core] Could not read the Core response body.\n");
                return false;
            }
            if (available == 0) break;
            if (responseBody->size() + available > kMaximumResponseBytes) {
                logMessage("[Chaotic Gaming Core] Core response exceeded the one-megabyte safety limit.\n");
                return false;
            }
            std::vector<char> buffer(available);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request.value, buffer.data(), available, &bytesRead)) {
                logMessage("[Chaotic Gaming Core] Core response body read failed.\n");
                return false;
            }
            if (bytesRead == 0) break;
            responseBody->append(buffer.data(), bytesRead);
        }
    }

    char message[128]{};
    std::snprintf(message, sizeof(message), "[Chaotic Gaming Core] %s accepted.\n", operation);
    logMessage(message);
    return true;
}


bool isSafeRewardItemKey(const std::string& value) noexcept {
    if (value.empty() || value.size() > 64) return false;
    for (const unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.') return false;
    }
    return true;
}

bool isSafeBlueprintPath(const std::string& value) noexcept {
    if (value.size() < 20 || value.size() > 512 ||
        value.rfind("Blueprint'", 0) != 0 || value.back() != '\'') return false;
    if (value.find("/Game/") == std::string::npos || value.find("..") != std::string::npos) return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f) return false;
    }
    return true;
}

bool loadRewardJournal() {
    std::ifstream input(g_rewardJournalPath, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        HANDLE file = CreateFileA(g_rewardJournalPath.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        CloseHandle(file);
        return true;
    }

    std::unordered_map<std::string, RewardJournalEntry> loaded;
    std::string line;
    try {
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            const auto record = nlohmann::json::parse(line);
            const std::string key = record.value("idempotencyKey", "");
            RewardJournalEntry entry{
                record.value("rewardId", ""),
                record.value("targetExternalId", ""),
                record.value("itemKey", ""),
                record.value("quantity", 0),
                record.value("state", "")
            };
            if (!chaotic::atlas::isRewardKey(key) || !isUuid(entry.rewardId) ||
                !chaotic::atlas::isSteamId(entry.targetExternalId) ||
                !isSafeRewardItemKey(entry.itemKey) || entry.quantity < 1 || entry.quantity > 100 ||
                (entry.state != "granting" && entry.state != "granted" &&
                 entry.state != "failed" && entry.state != "manual_review")) {
                return false;
            }
            loaded[key] = std::move(entry);
        }
    } catch (...) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_rewardJournalMutex);
    g_rewardJournal = std::move(loaded);
    return true;
}

bool appendRewardJournal(const PendingReward& reward, const char* state) {
    nlohmann::json record{
        {"rewardId", reward.id},
        {"idempotencyKey", reward.idempotencyKey},
        {"targetExternalId", reward.targetExternalId},
        {"itemKey", reward.itemKey},
        {"quantity", reward.quantity},
        {"state", state}
    };
    const std::string line = record.dump() + "\n";
    HANDLE file = CreateFileA(g_rewardJournalPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL writeOk = WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    const BOOL flushOk = writeOk && written == line.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!flushOk) return false;

    std::lock_guard<std::mutex> lock(g_rewardJournalMutex);
    g_rewardJournal[reward.idempotencyKey] = RewardJournalEntry{
        reward.id, reward.targetExternalId, reward.itemKey, reward.quantity, state
    };
    return true;
}

std::optional<RewardJournalEntry> findRewardJournalEntry(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_rewardJournalMutex);
    const auto found = g_rewardJournal.find(key);
    if (found == g_rewardJournal.end()) return std::nullopt;
    return found->second;
}

bool parsePendingReward(const nlohmann::json& row, PendingReward& reward) {
    try {
        if (!row.is_object() || row.value("game", "") != "atlas" ||
            !row.contains("deliveryPayload") || !row["deliveryPayload"].is_object()) return false;
        const auto& payload = row["deliveryPayload"];
        reward.id = row.value("id", "");
        reward.leaseToken = row.value("leaseToken", "");
        reward.idempotencyKey = row.value("idempotencyKey", "");
        reward.targetExternalId = row.value("targetExternalId", "");
        reward.itemKey = payload.value("itemKey", "");
        reward.quantity = payload.value("quantity", 0);
        return chaotic::atlas::isValidRewardDescriptor(
            reward.id, reward.leaseToken, reward.idempotencyKey,
            reward.targetExternalId, reward.itemKey, reward.quantity);
    } catch (...) {
        return false;
    }
}

bool acknowledgeReward(const PendingReward& reward, const char* outcome,
    const std::string& error = {}, const nlohmann::json& receipt = nlohmann::json::object()) {
    const std::wstring serverId(g_serverId.begin(), g_serverId.end());
    const std::wstring rewardId(reward.id.begin(), reward.id.end());
    const std::wstring requestPath = L"/integrations/servers/" + serverId +
        L"/rewards/" + rewardId + L"/ack";
    nlohmann::json body{
        {"leaseToken", reward.leaseToken},
        {"outcome", outcome}
    };
    if (!error.empty()) body["error"] = error;
    if (receipt.is_object() && !receipt.empty()) body["response"] = receipt;
    return sendCorePost(requestPath, body.dump(), "ATLAS reward acknowledgement");
}

bool renewRewardLease(const PendingReward& reward) {
    const std::wstring serverId(g_serverId.begin(), g_serverId.end());
    const std::wstring rewardId(reward.id.begin(), reward.id.end());
    const std::wstring requestPath = L"/integrations/servers/" + serverId +
        L"/rewards/" + rewardId + L"/renew";
    const nlohmann::json body{
        {"leaseToken", reward.leaseToken},
        {"leaseSeconds", 120}
    };
    std::string response;
    if (!sendCorePost(requestPath, body.dump(), "ATLAS reward lease renewal", &response)) return false;
    try {
        const auto parsed = nlohmann::json::parse(response);
        return parsed.value("data", nlohmann::json::object()).value("renewed", false) &&
            parsed["data"].value("rewardId", "") == reward.id;
    } catch (...) {
        return false;
    }
}

RewardWorkResult grantRewardOnGameThread(const PendingReward& reward) noexcept {
    try {
        const auto blueprint = g_rewardBlueprints.find(reward.itemKey);
        if (blueprint == g_rewardBlueprints.end()) {
            return {RewardWorkStatus::failed, "Reward itemKey is not in the server-side blueprint allowlist.", {}};
        }

        const auto steamId = static_cast<uint64>(std::stoull(reward.targetExternalId));
        AShooterPlayerController* player = ArkApi::GetApiUtils().FindPlayerFromSteamId(steamId);
        if (!player) {
            return {RewardWorkStatus::failed, "The reward recipient is not online on this server.", {}};
        }

        // The GiveItem wrapper is not declared by the pinned public headers. The uploaded
        // build guide documents this native signature; runtime use remains disabled until
        // an operator explicitly attests that it was verified on an isolated target server.
        if (!GetAddress("AShooterPlayerController.GiveItem")) {
            return {RewardWorkStatus::failed, "The pinned API does not expose AShooterPlayerController.GiveItem.", {}};
        }

        TArray<UPrimalItem*> grantedItems;
        FString blueprintPath(blueprint->second.c_str());
        const bool granted = NativeCall<bool, TArray<UPrimalItem*>*, FString*, int, float, bool, bool, float>(
            reinterpret_cast<void*>(player), "AShooterPlayerController.GiveItem",
            &grantedItems, &blueprintPath, reward.quantity, 1.0f, false, false, 0.0f);
        if (!granted && grantedItems.Num() == 0) {
            return {RewardWorkStatus::failed, "The ATLAS server rejected the item grant.", {}};
        }
        if (grantedItems.Num() == 0) {
            return {RewardWorkStatus::manual_review, "The native grant returned success without item receipts.", {}};
        }

        int totalQuantity = 0;
        nlohmann::json items = nlohmann::json::array();
        const FName markerName("CMGRewardDelivery", EFindName::FNAME_Add);
        for (UPrimalItem* item : grantedItems) {
            if (!item) continue;
            totalQuantity += item->GetItemQuantity();

            FCustomItemData marker{};
            marker.CustomDataName = markerName;
            marker.CustomDataStrings.Add(FString(reward.idempotencyKey.c_str()));
            item->SetCustomItemData(&marker);

            FCustomItemData verified{};
            if (!item->GetCustomItemData(markerName, &verified)) {
                return {RewardWorkStatus::manual_review, "The item was granted but its idempotency marker could not be verified.", {}};
            }
            bool markerFound = false;
            for (const FString& value : verified.CustomDataStrings) {
                if (toUtf8(value) == reward.idempotencyKey) markerFound = true;
            }
            if (!markerFound) {
                return {RewardWorkStatus::manual_review, "The item was granted but its idempotency marker did not persist.", {}};
            }

            const FItemNetID itemId = item->ItemIDField();
            items.push_back({
                {"itemId1", itemId.ItemID1},
                {"itemId2", itemId.ItemID2},
                {"quantity", item->GetItemQuantity()}
            });
        }

        if (totalQuantity < reward.quantity) {
            return {RewardWorkStatus::manual_review, "The native grant returned fewer items than requested.", {}};
        }
        return {RewardWorkStatus::delivered, {}, {
            {"idempotencyKey", reward.idempotencyKey},
            {"items", std::move(items)}
        }};
    } catch (...) {
        return {RewardWorkStatus::manual_review, "The native item grant raised an exception; verify the player's inventory before retrying.", {}};
    }
}

void processRewardQueue() noexcept {
    try {
        std::shared_ptr<RewardWorkItem> work;
        {
            std::lock_guard<std::mutex> lock(g_rewardMutex);
            if (g_pendingRewards.empty()) return;
            work = g_pendingRewards.front();
            g_pendingRewards.pop_front();
        }

        {
            std::lock_guard<std::mutex> lock(work->mutex);
            if (work->leaseLost || g_stop.load(std::memory_order_acquire)) {
                work->leaseLost = true;
                work->done = true;
                work->result.status = RewardWorkStatus::lease_lost;
                work->condition.notify_all();
                return;
            }
        }

        RewardWorkResult result = grantRewardOnGameThread(work->reward);
        {
            std::lock_guard<std::mutex> lock(work->mutex);
            if (!work->leaseLost) {
                work->result = std::move(result);
                work->done = true;
            }
        }
        work->condition.notify_all();
    } catch (...) {
        logMessage("[Chaotic Gaming Core] Unexpected game-thread reward dispatch error.\n");
    }
}

void completeRewardWork(const std::shared_ptr<RewardWorkItem>& work) {
    const auto& reward = work->reward;
    switch (work->result.status) {
    case RewardWorkStatus::delivered:
        if (!appendRewardJournal(reward, "granted")) {
            appendRewardJournal(reward, "manual_review");
            acknowledgeReward(reward, "manual_review", "MANUAL_REVIEW_REQUIRED: the game grant succeeded but its durable journal receipt could not be saved.", work->result.receipt);
            return;
        }
        acknowledgeReward(reward, "delivered", {}, work->result.receipt);
        return;
    case RewardWorkStatus::failed:
        if (!appendRewardJournal(reward, "failed")) {
            appendRewardJournal(reward, "manual_review");
            acknowledgeReward(reward, "manual_review", "MANUAL_REVIEW_REQUIRED: the failure state could not be durably recorded.");
            return;
        }
        acknowledgeReward(reward, "failed", work->result.error);
        return;
    case RewardWorkStatus::manual_review:
        appendRewardJournal(reward, "manual_review");
        acknowledgeReward(reward, "manual_review", "MANUAL_REVIEW_REQUIRED: " + work->result.error, work->result.receipt);
        return;
    case RewardWorkStatus::lease_lost:
        return;
    }
}

void rewardWorkerLoop() noexcept {
    while (!g_stop.load(std::memory_order_acquire)) {
        try {
            const std::wstring serverId(g_serverId.begin(), g_serverId.end());
            const std::wstring requestPath = L"/integrations/servers/" + serverId + L"/rewards/claim";
            const nlohmann::json claimBody{{"limit", 1}, {"leaseSeconds", 120}};
            std::string response;
            if (sendCorePost(requestPath, claimBody.dump(), "ATLAS reward claim", &response)) {
                const auto parsed = nlohmann::json::parse(response);
                const auto rows = parsed.value("data", nlohmann::json::array());
                if (rows.is_array()) {
                    for (const auto& row : rows) {
                        PendingReward reward;
                        if (!parsePendingReward(row, reward)) continue;

                        const auto previous = findRewardJournalEntry(reward.idempotencyKey);
                        if (previous) {
                            const bool sameGrant = previous->rewardId == reward.id &&
                                previous->targetExternalId == reward.targetExternalId &&
                                previous->itemKey == reward.itemKey &&
                                previous->quantity == reward.quantity;
                            if (!sameGrant || previous->state == "granting" || previous->state == "manual_review") {
                                acknowledgeReward(reward, "manual_review", "MANUAL_REVIEW_REQUIRED: a previous attempt has an uncertain or conflicting journal state.");
                                continue;
                            }
                            if (previous->state == "granted") {
                                acknowledgeReward(reward, "delivered", {}, {
                                    {"idempotencyKey", reward.idempotencyKey},
                                    {"duplicate", true},
                                    {"journalState", "granted"}
                                });
                                continue;
                            }
                        }

                        if (!appendRewardJournal(reward, "granting")) {
                            acknowledgeReward(reward, "failed", "The durable reward journal could not be written; no game grant was attempted.");
                            continue;
                        }

                        auto work = std::make_shared<RewardWorkItem>();
                        work->reward = reward;
                        {
                            std::lock_guard<std::mutex> lock(g_rewardMutex);
                            g_pendingRewards.push_back(work);
                        }
                        g_rewardCondition.notify_all();

                        std::unique_lock<std::mutex> lock(work->mutex);
                        auto nextRenewal = std::chrono::steady_clock::now() + std::chrono::seconds(45);
                        while (!work->done && !g_stop.load(std::memory_order_acquire)) {
                            if (work->condition.wait_until(lock, nextRenewal) == std::cv_status::timeout) {
                                lock.unlock();
                                const bool renewed = renewRewardLease(reward);
                                lock.lock();
                                if (!renewed) {
                                    work->leaseLost = true;
                                    break;
                                }
                                nextRenewal = std::chrono::steady_clock::now() + std::chrono::seconds(45);
                            }
                        }
                        const bool canAcknowledge = work->done && !work->leaseLost &&
                            !g_stop.load(std::memory_order_acquire);
                        lock.unlock();
                        if (canAcknowledge) completeRewardWork(work);
                    }
                }
            }
        } catch (...) {
            logMessage("[Chaotic Gaming Core] Reward worker encountered an error; it will retry.\n");
        }

        std::unique_lock<std::mutex> lock(g_rewardMutex);
        g_rewardCondition.wait_for(lock, std::chrono::seconds(5),
            [] { return g_stop.load(std::memory_order_acquire); });
    }
}

bool configureRewardDelivery() {
    if (readEnvironment("CHAOTIC_ATLAS_ENABLE_REWARD_DELIVERY") != "1" ||
        readEnvironment("CHAOTIC_ATLAS_GIVE_ITEM_ABI_VERIFIED") != "1") return false;

    g_rewardJournalPath = readEnvironment("CHAOTIC_ATLAS_REWARD_JOURNAL_PATH");
    const std::string mapJson = readEnvironment("CHAOTIC_ATLAS_REWARD_ITEM_MAP_JSON");
    if (g_rewardJournalPath.empty() || mapJson.empty()) return false;

    try {
        const auto parsed = nlohmann::json::parse(mapJson);
        if (!parsed.is_object() || parsed.empty()) return false;
        std::unordered_map<std::string, std::string> blueprints;
        for (auto it = parsed.begin(); it != parsed.end(); ++it) {
            if (!isSafeRewardItemKey(it.key()) || !it.value().is_string()) return false;
            const std::string path = it.value().get<std::string>();
            if (!isSafeBlueprintPath(path)) return false;
            blueprints.emplace(it.key(), path);
        }
        if (!GetAddress("AShooterPlayerController.GiveItem")) return false;
        g_rewardBlueprints = std::move(blueprints);
        if (!loadRewardJournal()) return false;
        return true;
    } catch (...) {
        return false;
    }
}

bool sendCoreHeartbeat() {
    const std::wstring serverId(g_serverId.begin(), g_serverId.end());
    const std::wstring requestPath = L"/integrations/servers/" + serverId + L"/heartbeat";
    const std::string body = chaotic::atlas::makeHeartbeatPayload(
        g_serverBuild, kAdapterVersion,
        g_serverReady.load(std::memory_order_relaxed),
        g_playerCount.load(std::memory_order_relaxed));
    return sendCorePost(requestPath, body, "ATLAS heartbeat");
}

bool sendCoreEvent(const PendingEvent& event) {
    const std::wstring serverId(g_serverId.begin(), g_serverId.end());
    const std::wstring requestPath = L"/integrations/servers/" + serverId + L"/events";
    const chaotic::atlas::GameEventRequest request{
        event.eventId, event.eventType, event.occurredAt,
        g_clusterId, g_gridId, g_serverId, event.payloadJson};
    return sendCorePost(requestPath, chaotic::atlas::makeGameEventRequestBody(request), "ATLAS player event");
}

EventPublishResult publishPendingEvents(const std::chrono::steady_clock::time_point& nextHeartbeat) noexcept {
    while (!g_stop.load(std::memory_order_relaxed)) {
        if (std::chrono::steady_clock::now() >= nextHeartbeat ||
            g_heartbeatRequested.load(std::memory_order_acquire)) {
            return EventPublishResult::HeartbeatDue;
        }
        PendingEvent event;
        {
            std::lock_guard<std::mutex> lock(g_eventMutex);
            if (!g_eventPublishingConfigured.load(std::memory_order_acquire) ||
                g_eventQueue.empty()) return EventPublishResult::Drained;
            event = std::move(g_eventQueue.front());
            g_eventQueue.pop_front();
        }
        bool accepted = false;
        try {
            accepted = sendCoreEvent(event);
        } catch (...) {
            logMessage("[Chaotic Gaming Core] Unexpected game-event publishing error; event retained for retry.\n");
        }
        if (!accepted) {
            std::lock_guard<std::mutex> lock(g_eventMutex);
            if (g_eventPublishingConfigured.load(std::memory_order_acquire) &&
                event.generation == g_eventGeneration) {
                g_eventQueue.push_front(std::move(event));
            }
            return EventPublishResult::RetryLater;
        }
    }
    return EventPublishResult::Drained;
}

bool pinPluginModule() noexcept {
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
    return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&handleNewPlayerHook), &module) != FALSE;
}

void startPlayerEventHooks() {
    if (g_hooksPinnedPassthrough) {
        logMessage("[Chaotic Gaming Core] Player hooks are pinned in passthrough mode after incomplete setup; restart the server to retry.\n");
        return;
    }
    if (g_joinHookInstalled && g_logoutHookInstalled) {
        logMessage("[Chaotic Gaming Core] Player lifecycle hooks are already attached; reusing process-lifetime hooks.\n");
        return;
    }
    if (g_joinHookInstalled || g_logoutHookInstalled) {
        g_hooksPinnedPassthrough = true;
        g_eventPublishingConfigured.store(false, std::memory_order_release);
        logMessage("[Chaotic Gaming Core] CRITICAL: partial player hooks remain attached in passthrough mode; restart the server to clear them.\n");
        return;
    }
    if (g_stop.load(std::memory_order_relaxed)) {
        logMessage("[Chaotic Gaming Core] Player event hooks skipped because Core integration is not configured.\n");
        return;
    }
    if (!g_eventPublishingConfigured.load(std::memory_order_acquire)) {
        logMessage("[Chaotic Gaming Core] Player events disabled; verify cluster/grid configuration and CHAOTIC_ATLAS_ENABLE_PLAYER_EVENTS=1.\n");
        return;
    }
    if (!pinPluginModule()) {
        g_eventPublishingConfigured.store(false, std::memory_order_release);
        logMessage("[Chaotic Gaming Core] CRITICAL: could not pin adapter module; player hooks were not installed.\n");
        return;
    }

    auto& hooks = ArkApi::GetHooks();
    const bool joinSet = hooks.SetHook("AShooterGameMode.HandleNewPlayer",
        reinterpret_cast<LPVOID>(&handleNewPlayerHook), &g_originalHandleNewPlayer);
    g_joinHookInstalled = joinSet || g_originalHandleNewPlayer != nullptr;
    const bool logoutSet = hooks.SetHook("AShooterGameMode.Logout",
        reinterpret_cast<LPVOID>(&logoutHook), &g_originalLogout);
    g_logoutHookInstalled = logoutSet || g_originalLogout != nullptr;

    if (!joinSet || !logoutSet || !g_originalHandleNewPlayer || !g_originalLogout) {
        g_eventPublishingConfigured.store(false, std::memory_order_release);
        g_hooksPinnedPassthrough = true;
        logMessage("[Chaotic Gaming Core] CRITICAL: both player hooks were not installed; any installed detours remain passthrough in the pinned module until server restart.\n");
        return;
    }
    logMessage("[Chaotic Gaming Core] ATLAS player join/leave hooks installed; adapter module pinned for safe process-lifetime callbacks.\n");
}

void disablePlayerEventPublishing() noexcept {
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        g_eventPublishingConfigured.store(false, std::memory_order_release);
        ++g_eventGeneration;
        g_eventQueue.clear();
        g_eventRequested.store(false, std::memory_order_release);
    }
    if (g_joinHookInstalled || g_logoutHookInstalled) {
        logMessage("[Chaotic Gaming Core] Player event publishing disabled; process-lifetime hooks remain passthrough until server restart.\n");
    }
}

void heartbeatLoop() noexcept {
    auto nextHeartbeat = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_relaxed)) {
        try {
            const bool heartbeatRequested = g_heartbeatRequested.exchange(false, std::memory_order_acq_rel);
            if (heartbeatRequested || std::chrono::steady_clock::now() >= nextHeartbeat) {
                try {
                    sendCoreHeartbeat();
                } catch (...) {
                    logMessage("[Chaotic Gaming Core] Unexpected heartbeat error; it will retry after the configured interval.\n");
                }
                nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(g_intervalSeconds);
            }

            bool deferEventRetry = false;
            if (g_eventRequested.exchange(false, std::memory_order_acq_rel) || hasPendingEvents()) {
                deferEventRetry = publishPendingEvents(nextHeartbeat) == EventPublishResult::RetryLater;
            }

            std::unique_lock<std::mutex> lock(g_waitMutex);
            if (deferEventRetry) {
                // Ignore new event wakeups until the next heartbeat deadline; this prevents a failed event
                // at the front of the queue from causing a tight retry loop.
                g_waitCondition.wait_until(lock, nextHeartbeat,
                    [] { return g_stop.load() || g_heartbeatRequested.load(); });
            } else {
                g_waitCondition.wait_until(lock, nextHeartbeat,
                    [] { return g_stop.load() || g_heartbeatRequested.load() || g_eventRequested.load(); });
            }
        } catch (...) {
            logMessage("[Chaotic Gaming Core] Unexpected integration worker error; it will retry.\n");
            std::unique_lock<std::mutex> lock(g_waitMutex);
            g_waitCondition.wait_for(lock, std::chrono::seconds(g_intervalSeconds),
                [] { return g_stop.load() || g_heartbeatRequested.load(); });
        }
    }
}

void startHeartbeat() {
    if (g_heartbeatThread.joinable()) {
        logMessage("[Chaotic Gaming Core] Heartbeat already started; duplicate initialization ignored.\n");
        return;
    }

    // The previous worker has joined before configuration can be replaced.
    // Serialize with callbacks retained by the process-lifetime detours.
    std::lock_guard<std::mutex> eventLock(g_eventMutex);
    g_eventPublishingConfigured.store(false, std::memory_order_release);
    g_coreApiUrl = readEnvironment("CHAOTIC_CORE_API_URL");
    g_serviceToken = readEnvironment("CHAOTIC_CORE_SERVICE_TOKEN");
    g_serverId = readEnvironment("CHAOTIC_CORE_SERVER_ID");
    g_clusterId = readEnvironment("CHAOTIC_CORE_CLUSTER_ID");
    g_gridId = readEnvironment("CHAOTIC_CORE_GRID_ID");
    const std::string serverBuild = readEnvironment("ATLAS_SERVER_BUILD");
    g_serverBuild = serverBuild.empty() ? "unknown" : serverBuild;

    if (g_coreApiUrl.empty() || g_serviceToken.empty() || g_serverId.empty()) {
        logMessage("[Chaotic Gaming Core] Heartbeat not configured; lifecycle-only mode.\n");
        return;
    }
    if (!isUuid(g_serverId) || !isServiceToken(g_serviceToken)) {
        logMessage("[Chaotic Gaming Core] Invalid server UUID or service token; heartbeat disabled.\n");
        return;
    }
    if (g_coreApiUrl.rfind("https://", 0) != 0 && g_coreApiUrl.rfind("http://", 0) != 0) {
        logMessage("[Chaotic Gaming Core] CORE_API_URL must use HTTPS (HTTP is allowed only for localhost).\n");
        return;
    }

    const std::string interval = readEnvironment("HEARTBEAT_INTERVAL_SECONDS");
    if (!interval.empty()) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(interval.c_str(), &end, 10);
        if (end != interval.c_str() && *end == '\0' && parsed >= 30 && parsed <= 300) {
            g_intervalSeconds = static_cast<unsigned int>(parsed);
        }
    }

    ArkApi::GetCommands().AddOnTimerCallback(FString(kServerStateCallbackId), &refreshServerState);
    g_serverStateCallbackRegistered = true;
    g_stop.store(false);
    g_heartbeatThread = std::thread(heartbeatLoop);
    g_eventPublishingConfigured.store(!g_hooksPinnedPassthrough && isUuid(g_clusterId) && isGridId(g_gridId) && readEnvironment("CHAOTIC_ATLAS_ENABLE_PLAYER_EVENTS") == "1", std::memory_order_release);

    g_rewardDeliveryConfigured.store(configureRewardDelivery(), std::memory_order_release);
    if (g_rewardDeliveryConfigured.load(std::memory_order_acquire)) {
        ArkApi::GetCommands().AddOnTimerCallback(FString(kRewardCallbackId), &processRewardQueue);
        g_rewardCallbackRegistered = true;
        g_rewardThread = std::thread(rewardWorkerLoop);
        logMessage("[Chaotic Gaming Core] Native reward delivery worker started with explicit ABI attestation, item allowlist, and durable journal.\n");
    } else {
        logMessage("[Chaotic Gaming Core] Native reward delivery remains disabled; explicit ABI attestation, item allowlist, and journal path are required.\n");
    }
}
} // namespace

CG_EXPORT void CG_CALL Plugin_Init() noexcept {
    logLifecycle("Plugin_Init");
    try {
        std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
        startHeartbeat();
        startPlayerEventHooks();
    } catch (...) {
        logMessage("[Chaotic Gaming Core] Heartbeat initialization failed; plugin remains loaded.\n");
    }
}

CG_EXPORT void CG_CALL Plugin_Unload() noexcept {
    std::lock_guard<std::mutex> lifecycleLock(g_lifecycleMutex);
    disablePlayerEventPublishing();
    g_serverReady.store(false, std::memory_order_relaxed);
    g_playerCount.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_waitMutex);
        g_stop.store(true);
    }
    g_waitCondition.notify_all();
    g_rewardCondition.notify_all();
    if (g_rewardThread.joinable()) g_rewardThread.join();
    if (g_rewardCallbackRegistered) {
        try {
            ArkApi::GetCommands().RemoveOnTimerCallback(FString(kRewardCallbackId));
        } catch (...) {
            logMessage("[Chaotic Gaming Core] Could not remove the reward timer callback cleanly.\n");
        }
        g_rewardCallbackRegistered = false;
    }
    if (g_serverStateCallbackRegistered) {
        try {
            ArkApi::GetCommands().RemoveOnTimerCallback(FString(kServerStateCallbackId));
        } catch (...) {
            logMessage("[Chaotic Gaming Core] Could not remove the ATLAS state timer callback cleanly.\n");
        }
        g_serverStateCallbackRegistered = false;
    }
    if (g_heartbeatThread.joinable()) g_heartbeatThread.join();
    g_rewardDeliveryConfigured.store(false, std::memory_order_release);
    logLifecycle("Plugin_Unload");
}

#else
#error "The ATLAS plugin proof of concept currently targets the Windows x64 server runtime."
#endif
