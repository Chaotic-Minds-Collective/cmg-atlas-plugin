#pragma once

#include <cctype>
#include <string>

namespace chaotic::atlas {

enum class RewardJournalState {
    missing,
    granting,
    granted,
    failed,
    manual_review
};

enum class RewardAction {
    grant,
    acknowledge_delivered,
    manual_review
};

inline bool isRewardUuid(const std::string& value) noexcept {
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

inline bool isSteamId(const std::string& value) noexcept {
    if (value.size() != 17) return false;
    for (const unsigned char ch : value) if (!std::isdigit(ch)) return false;
    return true;
}

inline bool isRewardKey(const std::string& value) noexcept {
    if (value.size() < 8 || value.size() > 128) return false;
    for (const unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.' && ch != ':') return false;
    }
    return true;
}

inline bool isRewardItemKey(const std::string& value) noexcept {
    if (value.empty() || value.size() > 64) return false;
    for (const unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.') return false;
    }
    return true;
}

inline bool isValidRewardDescriptor(
    const std::string& rewardId,
    const std::string& leaseToken,
    const std::string& idempotencyKey,
    const std::string& targetExternalId,
    const std::string& itemKey,
    int quantity
) noexcept {
    return isRewardUuid(rewardId) && isRewardUuid(leaseToken) &&
        isRewardKey(idempotencyKey) && isSteamId(targetExternalId) &&
        isRewardItemKey(itemKey) && quantity >= 1 && quantity <= 100;
}

inline RewardAction decideRewardAction(RewardJournalState state) noexcept {
    if (state == RewardJournalState::granted) return RewardAction::acknowledge_delivered;
    if (state == RewardJournalState::granting || state == RewardJournalState::manual_review) {
        return RewardAction::manual_review;
    }
    return RewardAction::grant;
}

} // namespace chaotic::atlas
