#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chaotic::atlas {

using UtcTime = std::chrono::system_clock::time_point;

enum class ServerState {
  unknown,
  starting,
  online,
  stopping,
  offline,
  maintenance
};

enum class EventKind {
  player_connected,
  player_disconnected,
  player_died,
  character_created,
  character_transferred,
  tribe_created,
  tribe_member_joined,
  tribe_member_left,
  server_state_changed,
  player_joined,
  player_left,
  player_chat,
  player_killed,
  tribe_updated,
  chat_global,
  chat_tribe,
  ship_sunk,
  ship_retrieved,
  structure_destroyed,
  tame_created,
  tame_died,
  server_started,
  server_stopped
};

// Canonical names match Core's NORMALIZED_GAME_EVENT_TYPES contract.
constexpr std::string_view eventKindName(EventKind kind) noexcept {
  switch (kind) {
    case EventKind::player_joined: return "player.joined";
    case EventKind::player_left: return "player.left";
    case EventKind::player_connected: return "player.connected";
    case EventKind::player_disconnected: return "player.disconnected";
    case EventKind::player_chat: return "player.chat";
    case EventKind::player_died: return "player.died";
    case EventKind::player_killed: return "player.killed";
    case EventKind::character_created: return "character.created";
    case EventKind::character_transferred: return "character.transferred";
    case EventKind::tribe_created: return "tribe.created";
    case EventKind::tribe_member_joined: return "tribe.member.joined";
    case EventKind::tribe_member_left: return "tribe.member.left";
    case EventKind::tribe_updated: return "tribe.updated";
    case EventKind::chat_global: return "chat.global";
    case EventKind::chat_tribe: return "chat.tribe";
    case EventKind::ship_sunk: return "ship.sunk";
    case EventKind::ship_retrieved: return "ship.retrieved";
    case EventKind::structure_destroyed: return "structure.destroyed";
    case EventKind::tame_created: return "tame.created";
    case EventKind::tame_died: return "tame.died";
    case EventKind::server_started: return "server.started";
    case EventKind::server_stopped: return "server.stopped";
    case EventKind::server_state_changed: return "server.state.changed";
  }
  return {};
}

constexpr std::optional<EventKind> eventKindFromName(std::string_view name) noexcept {
  constexpr EventKind kinds[] = {
      EventKind::player_joined, EventKind::player_left,
      EventKind::player_connected, EventKind::player_disconnected,
      EventKind::player_chat, EventKind::player_died, EventKind::player_killed,
      EventKind::character_created, EventKind::character_transferred,
      EventKind::tribe_created, EventKind::tribe_member_joined,
      EventKind::tribe_member_left, EventKind::tribe_updated,
      EventKind::chat_global, EventKind::chat_tribe, EventKind::ship_sunk,
      EventKind::ship_retrieved, EventKind::structure_destroyed,
      EventKind::tame_created, EventKind::tame_died,
      EventKind::server_started, EventKind::server_stopped,
      EventKind::server_state_changed};
  for (const auto kind : kinds) {
    if (eventKindName(kind) == name) return kind;
  }
  return std::nullopt;
}

struct ServerIdentity {
  std::string cluster_id;
  std::string grid_id;
  std::string server_id;
};

struct PlayerIdentity {
  std::optional<std::string> platform_id;
  std::optional<std::string> chaotic_player_id;
  std::optional<std::string> character_id;
  std::optional<std::string> character_name;
};

struct PlayerSession {
  PlayerIdentity player;
  ServerIdentity server;
  UtcTime connected_at;
  std::optional<UtcTime> disconnected_at;
};

struct CharacterTransfer {
  PlayerIdentity player;
  std::string source_grid_id;
  std::string destination_grid_id;
  UtcTime occurred_at;
};

struct ServerStatus {
  ServerIdentity server;
  ServerState state{ServerState::unknown};
  std::optional<std::uint32_t> online_players;
  UtcTime observed_at;
};

struct EventEnvelope {
  std::string event_id;
  std::uint32_t schema_version{1};
  EventKind kind{EventKind::server_state_changed};
  ServerIdentity server;
  UtcTime occurred_at;
  std::optional<PlayerIdentity> player;
  std::optional<std::string> tribe_id;
  std::optional<std::string> correlation_id;
};

// Validate the host-neutral envelope before an adapter serializes it for Core.
// Identifiers remain opaque strings; this layer does not invent provider ID formats.
inline bool isValidContractText(const std::string& value) noexcept {
  return !value.empty() && value.size() <= 200 &&
         value.find('\0') == std::string::npos;
}

inline bool isValidPlayerIdentity(const PlayerIdentity& player) noexcept {
  const auto validOptional = [](const std::optional<std::string>& value) noexcept {
    return !value || isValidContractText(*value);
  };
  return validOptional(player.platform_id) &&
         validOptional(player.chaotic_player_id) &&
         validOptional(player.character_id) &&
         validOptional(player.character_name);
}

inline bool isValidEventEnvelope(const EventEnvelope& event) noexcept {
  if (event.schema_version != 1 || !isValidContractText(event.event_id) ||
      eventKindName(event.kind).empty()) {
    return false;
  }
  if (!isValidContractText(event.server.cluster_id) ||
      !isValidContractText(event.server.grid_id) ||
      !isValidContractText(event.server.server_id)) {
    return false;
  }
  if (event.player && !isValidPlayerIdentity(*event.player)) return false;
  if (event.tribe_id && !isValidContractText(*event.tribe_id)) return false;
  if (event.correlation_id && !isValidContractText(*event.correlation_id)) return false;
  return true;
}

}  // namespace chaotic::atlas
