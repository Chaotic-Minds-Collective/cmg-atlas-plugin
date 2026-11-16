#pragma once

#include <string>

namespace chaotic::atlas {

inline std::string escapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    escaped += "\\u00";
                    escaped += hex[(ch >> 4) & 0x0f];
                    escaped += hex[ch & 0x0f];
                } else {
                    escaped += static_cast<char>(ch);
                }
        }
    }
    return escaped;
}

inline std::string makeHeartbeatPayload(
    const std::string& serverVersion,
    const std::string& adapterVersion,
    bool online,
    unsigned int playerCount
) {
    return "{\"game\":\"atlas\",\"serverVersion\":\"" + escapeJsonString(serverVersion) +
        "\",\"adapterVersion\":\"" + escapeJsonString(adapterVersion) +
        "\",\"online\":" + (online ? "true" : "false") +
        ",\"playerCount\":" + std::to_string(playerCount) + "}";
}

} // namespace chaotic::atlas
