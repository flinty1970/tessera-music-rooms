#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::music_rooms {

class Players;
class MusicCard;

// The places a tile keeps its players in: its own (0) and its eight rooms (the options room_1 to room_8).
constexpr int SLOTS = 9;
// Tiles a layout has at most, for what the plugin keeps of each (its picked rooms) by the tile's index.
constexpr int TILES = 64;

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
  uint16_t chosen(int tile) const { return tile >= 0 && tile < TILES ? chosen_[tile] : 0; }
  void set_chosen(int tile, uint16_t rooms) { if (tile >= 0 && tile < TILES) chosen_[tile] = rooms; }
  uint16_t joined(int tile) const { return tile >= 0 && tile < TILES ? joined_[tile] : 0; }
  void set_joined(int tile, uint16_t rooms) { if (tile >= 0 && tile < TILES) joined_[tile] = rooms; }

  // The players of a layout's tile, made on first use and kept in PSRAM: the tile fills them, the card reads them.
  Players *players(int tile);
  Players *players_at(int tile) const { return tile >= 0 && tile < TILES ? players_[tile] : nullptr; }
  // The index of a tile of this player, -1 for none.
  int find(const char *entity) const;

  MusicCard *card_ = nullptr;  // the card that is open, to hand it the library's answers

 protected:
  uint16_t chosen_[TILES] = {};
  uint16_t joined_[TILES] = {};
  Players *players_[TILES] = {};
};

}  // namespace esphome::music_rooms
