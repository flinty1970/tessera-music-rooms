#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::music_rooms {

class Players;
class MusicCard;

// The places a tile keeps its players in: its own (0) and its eleven rooms (the options room_1 to room_11).
constexpr int SLOTS = 12;
// Music tiles a layout has at most. The plugin keeps what it holds of each by the tile's slot on the screen, which can
// be any number (a screen of many pages and top bar items has slots past 80), so it is a short table, not an array.
constexpr int TILES = 16;

// The plugin: a tile ("player") and its card ("music"). The plugin keeps each tile's players (Players, in PSRAM) and
// which rooms it plays in next, so a tile made again (another size, a page that came back) keeps them and the card has
// them while the tile is not made; the card is Now playing, the rooms and the library.
class MusicRooms : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  // The rooms a tile plays in next, one bit per slot (bit 0: its own player), and those this plugin put together with
  // the first of them for an item, to take apart when they are no longer picked.
  uint16_t chosen(int tile) const { const Entry *e = entry(tile); return e ? e->chosen : 0; }
  void set_chosen(int tile, uint16_t rooms) { if (Entry *e = entry(tile)) e->chosen = rooms; }
  uint16_t joined(int tile) const { const Entry *e = entry(tile); return e ? e->joined : 0; }
  void set_joined(int tile, uint16_t rooms) { if (Entry *e = entry(tile)) e->joined = rooms; }

  // The players of a layout's tile, made on first use and kept in PSRAM: the tile fills them, the card reads them.
  Players *players(int tile);
  Players *players_at(int tile) const { const Entry *e = entry(tile); return e ? e->players : nullptr; }
  // The slot of a tile of this player, -1 for none.
  int find(const char *entity) const;

  // The Music Assistant entry of the screen (the input `library`), for the tiles that name none of their own.
  void set_library(const std::string &entry) { library_ = entry; }
  const char *library() const { return library_.c_str(); }
  // The script that plays a cut address exactly (script.music_rooms_play), "" to play such an item by name.
  void set_play_script(const std::string &script) { play_script_ = script; }
  const char *play_script() const { return play_script_.c_str(); }
  // The script that joins rooms to the lead of a player's group (script.music_rooms_join), "" to join to the player.
  void set_join_script(const std::string &script) { join_script_ = script; }
  const char *join_script() const { return join_script_.c_str(); }

  MusicCard *card_ = nullptr;  // the card that is open, to hand it the library's answers

 protected:
  struct Entry {
    int tile = -1;  // the tile's slot on the screen, -1 for an unused entry
    uint16_t chosen = 0, joined = 0;
    Players *players = nullptr;
  };
  Entry *entry(int tile) {
    for (Entry &e : entries_) if (tile >= 0 && e.tile == tile) return &e;
    return nullptr;
  }
  const Entry *entry(int tile) const {
    for (const Entry &e : entries_) if (tile >= 0 && e.tile == tile) return &e;
    return nullptr;
  }
  Entry entries_[TILES];
  std::string library_, play_script_, join_script_;
};

}  // namespace esphome::music_rooms
