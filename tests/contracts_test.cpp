#include "chaotic/atlas/contracts.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

#define ATLAS_CHECK(condition)                                                    \
  do {                                                                            \
    if (!(condition)) {                                                           \
      std::cerr << "Check failed: " #condition << '\n';                           \
      return EXIT_FAILURE;                                                       \
    }                                                                             \
  } while (false)

int main() {
  using namespace chaotic::atlas;

  static_assert(std::is_default_constructible<ServerStatus>::value,
                "ServerStatus must be default constructible");
  static_assert(std::is_default_constructible<EventEnvelope>::value,
                "EventEnvelope must be default constructible");
  static_assert(eventKindName(EventKind::player_connected) == "player.connected",
                "Event names must be available at compile time");

  ServerStatus status{};
  status.server.cluster_id = "main";
  status.server.grid_id = "A1";
  status.server.server_id = "A1-east";
  status.state = ServerState::online;
  status.online_players = 12;

  ATLAS_CHECK(status.state == ServerState::online);
  ATLAS_CHECK(status.online_players.value() == 12);

  EventEnvelope event{};
  event.event_id = "test-event";
  event.kind = EventKind::character_transferred;
  event.player = PlayerIdentity{};
  ATLAS_CHECK(event.schema_version == 1);
  ATLAS_CHECK(event.player.has_value());

  constexpr std::array<std::pair<EventKind, std::string_view>, 23> eventNames{{
      {EventKind::player_joined, "player.joined"},
      {EventKind::player_left, "player.left"},
      {EventKind::player_connected, "player.connected"},
      {EventKind::player_disconnected, "player.disconnected"},
      {EventKind::player_chat, "player.chat"},
      {EventKind::player_died, "player.died"},
      {EventKind::player_killed, "player.killed"},
      {EventKind::character_created, "character.created"},
      {EventKind::character_transferred, "character.transferred"},
      {EventKind::tribe_created, "tribe.created"},
      {EventKind::tribe_member_joined, "tribe.member.joined"},
      {EventKind::tribe_member_left, "tribe.member.left"},
      {EventKind::tribe_updated, "tribe.updated"},
      {EventKind::chat_global, "chat.global"},
      {EventKind::chat_tribe, "chat.tribe"},
      {EventKind::ship_sunk, "ship.sunk"},
      {EventKind::ship_retrieved, "ship.retrieved"},
      {EventKind::structure_destroyed, "structure.destroyed"},
      {EventKind::tame_created, "tame.created"},
      {EventKind::tame_died, "tame.died"},
      {EventKind::server_started, "server.started"},
      {EventKind::server_stopped, "server.stopped"},
      {EventKind::server_state_changed, "server.state.changed"},
  }};

  for (const auto& [kind, expectedName] : eventNames) {
    ATLAS_CHECK(eventKindName(kind) == expectedName);
  }
  ATLAS_CHECK(eventKindName(static_cast<EventKind>(255)).empty());

  return EXIT_SUCCESS;
}
