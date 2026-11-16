#pragma once

#include "chaotic/atlas/heartbeat_payload.hpp"

#include <string>

namespace chaotic::atlas {

inline std::string makeGameEventId(
    const std::string& serverId,
    const std::string& occurredAt,
    unsigned long long sequence
) {
    return "atlas-" + serverId + "-" + occurredAt + "-" + std::to_string(sequence);
}

struct GameEventRequest {
    std::string eventId;
    std::string eventType;
    std::string occurredAt;
    std::string clusterId;
    std::string gridId;
    std::string serverId;
    std::string payloadJson;
};

inline std::string makePlayerLifecyclePayload(
    const std::string& platformId,
    const std::string& playerName,
    const std::string& gamePlayerId,
    bool isFromLogin
) {
    return "{\"platformId\":\"" + escapeJsonString(platformId) +
        "\",\"playerName\":\"" + escapeJsonString(playerName) +
        "\",\"gamePlayerId\":\"" + escapeJsonString(gamePlayerId) +
        "\",\"isFromLogin\":" + (isFromLogin ? "true" : "false") + "}";
}

inline std::string makeGameEventRequestBody(const GameEventRequest& event) {
    const std::string payload = event.payloadJson.empty() ? "{}" : event.payloadJson;
    return "{\"game\":\"atlas\",\"eventId\":\"" + escapeJsonString(event.eventId) +
        "\",\"eventType\":\"" + escapeJsonString(event.eventType) +
        "\",\"occurredAt\":\"" + escapeJsonString(event.occurredAt) +
        "\",\"scope\":{\"clusterId\":\"" + escapeJsonString(event.clusterId) +
        "\",\"gridId\":\"" + escapeJsonString(event.gridId) +
        "\",\"serverId\":\"" + escapeJsonString(event.serverId) +
        "\"},\"payload\":" + payload + "}";
}

} // namespace chaotic::atlas
