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

}  // namespace chaotic::atlas
