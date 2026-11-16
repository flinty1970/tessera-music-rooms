#include "music_rooms.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::music_rooms {

using tessera::Font;
namespace ui = tessera::ui;

// ---- Small pieces ----

// Icons of Tessera's set (tile_icons.GLYPHS).
constexpr uint32_t ICON_SPEAKER = 0xF04C3, ICON_ROOMS = 0xF0D38, ICON_PLAY = 0xF040A, ICON_PAUSE = 0xF03E4,
                   ICON_NEXT = 0xF04AD, ICON_PREVIOUS = 0xF04AE, ICON_MINUS = 0xF075E, ICON_PLUS = 0xF075D,
                   ICON_CHECK = 0xF012C, ICON_RIGHT = 0xF0142, ICON_LEFT = 0xF0141, ICON_HEART = 0xF02D1,
                   ICON_MUSIC = 0xF0387;

// A text into a fixed field, cut on a character boundary.
static void put(char *field, size_t size, const char *text) {
  if (!text) text = "";
  size_t n = std::min(strlen(text), size - 1);
  while (n > 0 && n < strlen(text) && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
  memcpy(field, text, n);
  field[n] = '\0';
}
template <size_t N> static void put(char (&field)[N], const char *text) { put(field, N, text); }

static bool gone(const char *state) { return !strcmp(state, "unavailable") || !strcmp(state, "unknown"); }

// The kinds of the Music Assistant library (music_assistant.get_library's media_type), in the order the card lists
// them: the word of each, its icon, and how many a page asks for. Albums and tracks carry their artists, so fewer of
// those fit the 3 KB an answer of the app may be whole.
struct Kind { const char *type, *word; uint32_t icon; int page; };
static const Kind KINDS[] = {
    {"playlist", "playlists", 0xF0CB8, 6}, {"radio", "radio", 0xF0439, 6},    {"album", "albums", 0xF0025, 4},
    {"artist", "artists", 0xF0803, 6},     {"track", "tracks", 0xF0387, 4},   {"podcast", "podcasts", 0xF0994, 6},
};
constexpr int KIND_COUNT = sizeof(KINDS) / sizeof(KINDS[0]);

// ---- One player of a tile ----
struct Room {
  char id[64] = "";      // its entity, "" for an empty slot
  char name[49] = "";
  char state[16] = "";
  char title[49] = "";
  char artist[49] = "";
  int volume = -1;       // 0 to 100, -1 when it has none
  bool group = false;    // a Music Assistant group (mass_player_type "group"): it plays alone, never joined to others
  bool used() const { return id[0] != '\0'; }
};

// ---- A tile's players: its own and its rooms, their states, which rooms play next, and how an item plays ----
// Kept by the plugin per tile in PSRAM, so the card has them also while the tile itself is not made (a board without
// PSRAM makes a tile only while its page is on the glass).
class Players : public tessera::InPsram {
 public:
  Players(MusicRooms *plugin, int index) : plugin_(plugin), index_(index) {}

  // From the tile's entity and options, as create() has them.
  void configure(const char *own, JsonObjectConst options) {
    put(rooms_[0].id, own);
    char key[8];
    for (int i = 1; i < SLOTS; ++i) {
      snprintf(key, sizeof(key), "room_%d", i);
      const char *id = options[key] | "";
      // A room that is the tile's own player, or one named twice, is the one already listed.
      bool twice = !strcmp(id, rooms_[0].id);
      for (int j = 1; j < i && !twice; ++j) twice = !strcmp(id, rooms_[j].id);
      put(rooms_[i].id, twice ? "" : id);
    }
    several_ = !strcmp(options["rooms_at_once"] | "one", "several");
    put(library_, options["library"] | "");
    put(open_on_, options["open_on"] | "kinds");
  }

  // The tile's data, as on_state has it: its own player, and the rooms under `entities`.
  void read(JsonObjectConst data) {
    if (!data["state"].isNull()) read(rooms_[0], data);
    char key[8];
    for (int i = 1; i < SLOTS; ++i) {
      snprintf(key, sizeof(key), "room_%d", i);
      JsonObjectConst room = data["entities"][key];
      if (room.isNull()) put(rooms_[i].state, "");
      else read(rooms_[i], room);
    }
  }

  const Room &room(int slot) const { return rooms_[slot]; }
  bool several() const { return several_; }
  const char *library() const { return library_; }
  const char *open_on() const { return open_on_; }
  // The slots of the rooms it plays in next, one bit each in the tile's order; its own player while none is picked.
  uint16_t chosen() const {
    const uint16_t picked = plugin_->chosen(index_) & used_mask();
    return picked ? picked : 1;
  }
  int lead_slot() const {
    const uint16_t picked = chosen();
    for (int i = 0; i < SLOTS; ++i) if (picked & (1u << i)) return i;
    return 0;
  }
  uint16_t used_mask() const {
    uint16_t mask = 0;
    for (int i = 0; i < SLOTS; ++i) if (rooms_[i].used()) mask |= 1u << i;
    return mask;
  }
  int room_count() const { return __builtin_popcount(used_mask()); }
  void choose(uint16_t rooms) {
    rooms &= used_mask();
    plugin_->set_chosen(index_, rooms ? rooms : 1);
    note_ = Note::NONE;
  }
  // "Kitchen", "Kitchen + 2" or "All rooms".
  std::string rooms_text() const {
    const uint16_t picked = chosen();
    const int count = __builtin_popcount(picked);
    const Room &lead = rooms_[lead_slot()];
    if (count > 1 && picked == used_mask()) return plugin_->text("all_rooms");
    if (count > 1)
      return tessera::fill(tessera::fill(plugin_->text("more_rooms"), "room", lead.name).c_str(), "n", std::to_string(count - 1));
    return lead.name;
  }
  // What plays in the first room, or what the card says instead.
  std::string now_playing() const {
    const Room &lead = rooms_[lead_slot()];
    if (note_ == Note::STARTING) return plugin_->text("starting");
    if (note_ == Note::NOT_STARTED) return plugin_->text("not_started");
    if (note_ == Note::NOT_SENT) return plugin_->text("not_sent");
    if (gone(lead.state)) return plugin_->text("unavailable");
    if (lead.title[0] && (!strcmp(lead.state, "playing") || !strcmp(lead.state, "paused") || !strcmp(lead.state, "buffering")))
      return lead.title;
    return plugin_->text("idle");
  }
  bool noted() const { return note_ == Note::NOT_STARTED || note_ == Note::NOT_SENT; }

  // Play an item in the rooms picked: those Home Assistant cannot reach are left out; with several, the others join the
  // first one first (media_player.join), and rooms this tile joined before that are no longer picked leave
  // (media_player.unjoin), so it plays in exactly the rooms picked. Then play_media on the first, with the item's own
  // Music Assistant address and kind. False when no room could take it or nothing could be sent.
  bool play(const char *uri, const char *type) {
    uint16_t rooms = 0;
    for (int i = 0; i < SLOTS; ++i)
      if ((chosen() & (1u << i)) && rooms_[i].used() && !gone(rooms_[i].state)) rooms |= 1u << i;
    if (!rooms) {
      note_ = Note::NOT_SENT;
      return false;
    }
    int lead = 0;
    while (!(rooms & (1u << lead))) ++lead;
    const uint16_t others = rooms & ~(1u << lead);
    // A Music Assistant group among the rooms is joined to nothing and nothing is joined to it: each room then gets
    // the item on its own, not in step, where rooms alone are joined to the first and play together.
    bool groups = false;
    for (int i = 0; i < SLOTS; ++i) if ((rooms & (1u << i)) && rooms_[i].group) groups = true;
    const uint16_t together = groups ? 0 : others;
    bool sent = true;
    const uint16_t leave = plugin_->joined(index_) & ~together & ~(1u << lead);
    for (int i = 0; i < SLOTS; ++i)
      if ((leave & (1u << i)) && rooms_[i].used() && !gone(rooms_[i].state))
        tessera::action("media_player.unjoin", rooms_[i].id);
    if (together) {
      JsonDocument join;
      JsonArray members = join["group_members"].to<JsonArray>();
      for (int i = 0; i < SLOTS; ++i) if (together & (1u << i)) members.add(rooms_[i].id);
      sent = tessera::action("media_player.join", rooms_[lead].id, join.as<JsonVariantConst>());
    }
    plugin_->set_joined(index_, together);
    // Home Assistant's play_media with the item's own Music Assistant address and kind: on the first room, and with a
    // group among them on every room.
    JsonDocument item;
    item["media_content_id"] = uri;
    item["media_content_type"] = type;
    const uint16_t give = groups ? rooms : (1u << lead);
    for (int i = 0; i < SLOTS; ++i)
      if (give & (1u << i))
        sent = tessera::action("media_player.play_media", rooms_[i].id, item.as<JsonVariantConst>()) && sent;
    note_ = sent ? Note::STARTING : Note::NOT_SENT;
    started_at_ = tessera::epoch();
    put(started_title_, rooms_[lead].title);
    put(started_state_, rooms_[lead].state);
    return sent;
  }

  // The keys of Now playing, on the first room (a group's lead plays for all of it).
  void play_pause() {
    const Room &lead = rooms_[lead_slot()];
    if (!lead.used() || gone(lead.state)) return;
    tessera::wish(lead.id, tessera::Wish::PLAYING, !strcmp(lead.state, "playing") ? "0" : "1");
  }
  void step_track(bool next) {
    const Room &lead = rooms_[lead_slot()];
    if (lead.used() && !gone(lead.state))
      tessera::action(next ? "media_player.media_next_track" : "media_player.media_previous_track", lead.id);
  }
  void step_volume(int by) {
    Room &lead = rooms_[lead_slot()];
    if (!lead.used() || gone(lead.state) || lead.volume < 0) return;
    // To the next step of five in that direction: 42 goes to 45 up and 40 down.
    const int stepped = by > 0 ? (lead.volume / 5 + 1) * 5 : ((lead.volume + 4) / 5 - 1) * 5;
    lead.volume = std::max(0, std::min(100, stepped));
    JsonDocument level;
    level["volume_level"] = lead.volume / 100.0f;
    tessera::action("media_player.volume_set", lead.id, level.as<JsonVariantConst>());
  }

  // A start that took: the first room plays something else than before, or plays where it did not. One that did not
  // take within 15 seconds says so.
  void settle(uint32_t now) {
    if (note_ != Note::STARTING || !now) return;
    const Room &lead = rooms_[lead_slot()];
    const bool playing = !strcmp(lead.state, "playing");
    if (playing && (strcmp(started_state_, "playing") || strcmp(started_title_, lead.title))) {
      note_ = Note::NONE;
      return;
    }
    if (started_at_ && now > started_at_ + 15) note_ = playing ? Note::NONE : Note::NOT_STARTED;
  }

 private:
  enum class Note : uint8_t { NONE, STARTING, NOT_STARTED, NOT_SENT };

  static void read(Room &room, JsonObjectConst data) {
    put(room.state, data["state"] | "");
    put(room.name, data["name"] | "");
    if (!room.name[0]) put(room.name, room.id);
    JsonObjectConst a = data["attributes"];
    put(room.title, a["media_title"] | "");
    put(room.artist, a["media_artist"] | "");
    room.volume = a["volume_level"].is<float>() ? static_cast<int>(a["volume_level"].as<float>() * 100.0f + 0.5f) : -1;
    room.group = !strcmp(a["mass_player_type"] | "", "group");
  }

  MusicRooms *plugin_;
  int index_;
  Room rooms_[SLOTS];
  char library_[49] = "";
  char open_on_[16] = "kinds";
  char started_title_[49] = "", started_state_[16] = "";
  bool several_ = false;
  uint32_t started_at_ = 0;
  Note note_ = Note::NONE;
};

// ---- The tile: the room it plays in and what plays there; a tap opens the card ----
class PlayerTile : public tessera::Tile {
 public:
  explicit PlayerTile(MusicRooms *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    index_ = c.tile;
    players_ = plugin_->players(index_);
    if (players_) players_->configure(c.entity, c.options);
    width_ = c.width;
    height_ = c.height;
    const int line = ui::line_height(Font::BODY);
    const bool small = height_ < line * 3 + ui::px(8);
    // A tile with room for it shows the cover of its own player at its left, as tall as the tile.
    int text_x = 0;
    if (ui::pictures() && !small && height_ >= ui::mm(18) && width_ >= height_ * 2 && width_ - height_ >= ui::mm(35)) {
      cover_ = ui::picture(c.parent);
      lv_obj_set_size(cover_, height_, height_);
      lv_obj_set_pos(cover_, 0, 0);
      text_x = height_ + ui::px(12);
    }
    const int text_w = std::max(1, width_ - text_x);
    where_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(where_, text_w);
    lv_obj_set_pos(where_, text_x, 0);
    // What plays in the largest face that fits the room under the room's name and over the artist.
    Font face = Font::TITLE;
    for (Font f : {Font::HEADLINE})
      if (!small && ui::line_height(f) + 2 * line + ui::px(8) <= height_) face = f;
    title_ = ui::label(c.parent, face, theme::INK);
    lv_obj_set_width(title_, text_w);
    artist_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(artist_, text_w);
    if (small) {
      // Two lines: the room, and what plays.
      lv_obj_set_pos(title_, text_x, std::max(line, height_ - ui::line_height(face)));
      lv_obj_add_flag(artist_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_pos(title_, text_x, height_ - ui::line_height(face) - line);
      lv_obj_set_pos(artist_, text_x, height_ - line);
    }
  }

  void on_state(JsonObjectConst data) override { if (players_) players_->read(data); }

  void on_tick(uint32_t now) override {
    if (!players_ || !players_->room(0).used()) {
      ui::set_text(where_, "");
      ui::set_text(title_, plugin_->text("choose_player"));
      ui::set_text(artist_, "");
      return;
    }
    players_->settle(now);
    const Room &lead = players_->room(players_->lead_slot());
    ui::set_text(where_, players_->rooms_text());
    ui::set_text(title_, players_->now_playing());
    ui::set_text(artist_, gone(lead.state) ? "" : lead.artist);
    if (cover_) {
      // The cover is the tile's own player's; another room's is not this tile's to show.
      const bool own = players_->lead_slot() == 0 && !gone(lead.state) && lead.title[0];
      if (own) ui::set_picture(cover_, std::string("cover:") + lead.id, tessera::Fit::COVER);
      if (own) lv_obj_remove_flag(cover_, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  void on_theme() override {
    ui::set_color(where_, theme::MUTED);
    ui::set_color(title_, theme::INK);
    ui::set_color(artist_, theme::MUTED);
  }

  void on_tap() override {
    if (players_ && players_->room(0).used()) tessera::open_card(plugin_->plugin_id(), "music", players_->room(0).id, index_);
  }

 private:
  MusicRooms *plugin_;
  Players *players_ = nullptr;
  int index_ = -1, width_ = 0, height_ = 0;
  lv_obj_t *cover_{}, *where_{}, *title_{}, *artist_{};
};

// ---- The card: Now playing, the rooms, the library ----
class MusicCard : public tessera::Card {
 public:
  explicit MusicCard(MusicRooms *plugin) : plugin_(plugin) {}
  ~MusicCard() override {
    if (plugin_->card_ == this) plugin_->card_ = nullptr;
  }

  void open(const tessera::CardContext &c) override {
    parent_ = c.parent;
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    // Opened without its tile's index (a settings row, another card): the tile of the player it was opened for.
    if (!plugin_->players_at(tile_) && c.entity && c.entity[0]) tile_ = plugin_->find(c.entity);
    plugin_->card_ = this;
    gap_ = ui::px(8);
    key_ = std::max(ui::mm(10), ui::line_height(Font::BODY_LARGE) + ui::px(12));
    // The keys of Now playing: six in a row and the volume between them, as large as the width allows, never smaller
    // than a finger.
    const int volume_w = ui::text_width("100 %", Font::BODY_LARGE) + ui::px(8);
    round_ = std::max(ui::mm(7), std::min(key_, (width_ - volume_w - 6 * gap_) / 6));

    // The head: on Now playing the rooms key and the library key; in the lists the rooms it plays in.
    head_ = button(0, 0, width_, key_, theme::RAISED);
    head_icon_ = glyph(head_, ICON_ROOMS, theme::INK);
    head_text_ = ui::label(head_, Font::BODY_LARGE, theme::INK);
    head_more_ = glyph(head_, ICON_RIGHT, theme::MUTED);
    const int library_w = std::min(width_ / 3, ui::text_width(plugin_->text("library"), Font::BODY_LARGE) + 2 * gap_ + ui::px(24));
    library_ = button(width_ - library_w, 0, library_w, key_, theme::RAISED);
    library_text_ = ui::label(library_, Font::BODY_LARGE, theme::INK);
    lv_obj_set_width(library_text_, library_w - 2 * gap_);
    lv_obj_set_style_text_align(library_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(library_text_, LV_ALIGN_CENTER, 0, 0);
    ui::set_text(library_text_, plugin_->text("library"));

    // Now playing: the cover of the tile's own player, what plays and who, and the keys.
    const int body_y = key_ + gap_, body_h = height_ - body_y - round_ - gap_;
    if (ui::pictures() && body_h > ui::mm(12)) {
      const int side = std::min(body_h, width_ * 2 / 5);
      cover_ = ui::picture(parent_);
      lv_obj_set_size(cover_, side, side);
      lv_obj_set_pos(cover_, 0, body_y + (body_h - side) / 2);
      cover_x_ = side + gap_ * 2;
    }
    const int text_x = cover_x_;
    // What plays in the largest face whose two lines and the artist under them fit the room between head and keys.
    const int artist_h = ui::line_height(Font::BODY_LARGE);
    Font face = Font::BODY_LARGE;
    for (Font f : {Font::HEADLINE, Font::TITLE})
      if (ui::line_height(f) * 2 + gap_ + artist_h <= body_h) { face = f; break; }
    const int title_h = ui::line_height(face) * 2;
    title_ = ui::text(parent_, face, theme::INK);
    lv_obj_set_width(title_, std::max(1, width_ - text_x));
    lv_obj_set_height(title_, title_h);
    artist_ = ui::label(parent_, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_width(artist_, std::max(1, width_ - text_x));
    const int text_h = title_h + gap_ + artist_h;
    const int text_y = body_y + std::max(0, (body_h - text_h) / 2);
    lv_obj_set_pos(title_, text_x, text_y);
    lv_obj_set_pos(artist_, text_x, text_y + title_h + gap_);
    const int keys_y = height_ - round_;
    const uint32_t icons[6] = {ICON_PREVIOUS, ICON_PLAY, ICON_NEXT, ICON_MINUS, 0, ICON_PLUS};
    int x = 0;
    for (int i = 0; i < 6; ++i) {
      if (i == 3) {
        // The volume stands between its two keys, at the right.
        x = width_ - (2 * round_ + volume_w + 2 * gap_);
      }
      if (i == 4) {
        volume_ = ui::label(parent_, Font::BODY_LARGE, theme::INK);
        lv_obj_set_size(volume_, volume_w, ui::line_height(Font::BODY_LARGE));
        lv_obj_set_style_text_align(volume_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(volume_, x, keys_y + (round_ - ui::line_height(Font::BODY_LARGE)) / 2);
        x += volume_w + gap_;
        continue;
      }
      keys_[i] = button(x, keys_y, round_, round_, i == 1 ? theme::ACCENT : theme::RAISED);
      lv_obj_set_style_radius(keys_[i], LV_RADIUS_CIRCLE, 0);
      key_icon_[i] = glyph(keys_[i], icons[i], i == 1 ? theme::ON_ACCENT : theme::INK);
      lv_obj_align(key_icon_[i], LV_ALIGN_CENTER, 0, 0);
      x += round_ + gap_;
    }

    // The lists: as many rows as fit between the head and the pager.
    const int row_gap = ui::px(6);
    rows_ = std::max(2, std::min(ROWS, (height_ - 2 * key_ - 2 * gap_ + row_gap) / (key_ + row_gap)));
    for (int i = 0; i < rows_; ++i) {
      Row &r = row_[i];
      r.y = key_ + gap_ + i * (key_ + row_gap);
      r.box = button(0, r.y, width_, key_, theme::CARD);
      r.icon = glyph(r.box, ICON_MUSIC, theme::MUTED);
      r.text = ui::label(r.box, Font::BODY_LARGE, theme::INK);
      r.side = ui::label(r.box, Font::BODY, theme::MUTED);
      r.mark = glyph(r.box, ICON_CHECK, theme::ACCENT);
    }
    const int pager_y = height_ - key_;
    prev_ = button(0, pager_y, key_ * 2, key_, theme::RAISED);
    prev_icon_ = glyph(prev_, ICON_LEFT, theme::INK);
    lv_obj_align(prev_icon_, LV_ALIGN_CENTER, 0, 0);
    next_ = button(width_ - key_ * 2, pager_y, key_ * 2, key_, theme::RAISED);
    next_icon_ = glyph(next_, ICON_RIGHT, theme::INK);
    lv_obj_align(next_icon_, LV_ALIGN_CENTER, 0, 0);
    page_ = ui::label(parent_, Font::BODY, theme::MUTED);
    lv_obj_set_size(page_, std::max(1, width_ - 4 * key_ - 2 * gap_), ui::line_height(Font::BODY));
    lv_obj_set_style_text_align(page_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(page_, key_ * 2 + gap_, pager_y + (key_ - ui::line_height(Font::BODY)) / 2);
    note_ = ui::text(parent_, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_size(note_, width_, ui::line_height(Font::BODY_LARGE) * 2);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, key_ + gap_ + ui::px(16));

    show(View::NOW);
  }

  // The tile's data comes to the card too: its players' states stay current while the tile is not made.
  void on_state(JsonObjectConst data) override {
    if (Players *tile = plugin_->players_at(tile_)) tile->read(data);
    paint();
  }
  void on_tick(uint32_t now) override {
    if (Players *tile = plugin_->players_at(tile_)) tile->settle(now);
    paint();
  }
  void on_theme() override {
    restyle(head_, theme::RAISED);
    restyle(library_, theme::RAISED);
    restyle(prev_, theme::RAISED);
    restyle(next_, theme::RAISED);
    for (int i = 0; i < 6; ++i) if (keys_[i]) restyle(keys_[i], i == 1 ? theme::ACCENT : theme::RAISED);
    for (int i = 0; i < rows_; ++i) restyle(row_[i].box, theme::CARD);
    for (lv_obj_t *l : {head_icon_, head_text_, library_text_, title_, volume_}) if (l) ui::set_color(l, theme::INK);
    for (lv_obj_t *l : {head_more_, artist_, page_, note_}) if (l) ui::set_color(l, theme::MUTED);
    for (int i = 0; i < 6; ++i) if (key_icon_[i]) ui::set_color(key_icon_[i], i == 1 ? theme::ON_ACCENT : theme::INK);
    if (prev_icon_) ui::set_color(prev_icon_, theme::INK);
    if (next_icon_) ui::set_color(next_icon_, theme::INK);
    for (int i = 0; i < rows_; ++i) {
      ui::set_color(row_[i].text, theme::INK);
      ui::set_color(row_[i].side, theme::MUTED);
      ui::set_color(row_[i].mark, theme::ACCENT);
    }
    paint();
  }

  // Back goes up a step of the card's own; on Now playing it closes the card.
  bool on_back() override {
    switch (view_) {
      case View::ITEMS: show(View::KINDS); return true;
      case View::KINDS:
        if (favourites_) { favourites_ = false; kind_page_ = 0; show(View::KINDS); return true; }
        show(View::NOW);
        return true;
      case View::ROOMS: show(View::NOW); return true;
      default: return false;
    }
  }

  void on_tap() override {
    Players *tile = plugin_->players_at(tile_);
    if (!tile) return;
    const tessera::Point p = tessera::tap_point();
    if (p.x < 0) return;
    if (view_ == View::NOW) {
      if (inside(library_, p)) { open_library(*tile); return; }
      if (inside(head_, p)) { show(View::ROOMS); return; }
      if (inside(keys_[0], p)) tile->step_track(false);
      else if (inside(keys_[1], p)) tile->play_pause();
      else if (inside(keys_[2], p)) tile->step_track(true);
      else if (inside(keys_[3], p)) tile->step_volume(-5);
      else if (inside(keys_[5], p)) tile->step_volume(5);
      paint();
      return;
    }
    if (inside(prev_, p) && !lv_obj_has_flag(prev_, LV_OBJ_FLAG_HIDDEN)) { turn(-1); return; }
    if (inside(next_, p) && !lv_obj_has_flag(next_, LV_OBJ_FLAG_HIDDEN)) { turn(1); return; }
    for (int i = 0; i < rows_; ++i) {
      if (lv_obj_has_flag(row_[i].box, LV_OBJ_FLAG_HIDDEN) || !inside(row_[i].box, p)) continue;
      tap_row(*tile, i);
      return;
    }
  }

  // An answer of the app: a page of the library.
  void answer(JsonObjectConst message) {
    if (!asked_ || (message["re"] | 0u) != asked_) return;
    asked_ = 0;
    loading_ = false;
    count_ = 0;
    if (!(message["ok"] | false)) {
      failed_ = true;
    } else {
      for (JsonObjectConst item : message["result"]["items"].as<JsonArrayConst>()) {
        if (count_ >= ROWS) break;
        Item &it = items_[count_];
        put(it.name, item["name"] | "");
        const char *uri = item["uri"] | "";
        // An address cut at the app's 48 bytes plays nothing: the row shows, faded, and takes no tap.
        it.playable = uri[0] && strlen(uri) < 48;
        put(it.uri, uri);
        put(it.type, item["media_type"] | KINDS[kind_].type);
        put(it.side, item["artists"][0]["name"] | "");
        if (it.name[0]) ++count_;
      }
      more_ = count_ >= page_size();
    }
    paint();
    tessera::refresh();
  }

 private:
  enum class View : uint8_t { NONE, NOW, ROOMS, KINDS, ITEMS };
  static constexpr int ROWS = 6;
  struct Row { lv_obj_t *box = nullptr, *icon = nullptr, *text = nullptr, *side = nullptr, *mark = nullptr; int y = 0; };
  struct Item { char name[49] = "", uri[49] = "", type[16] = "", side[49] = ""; bool playable = false; };

  lv_obj_t *button(int x, int y, int w, int h, theme::Role fill) {
    lv_obj_t *b = ui::block(parent_, fill);
    lv_obj_set_size(b, std::max(1, w), std::max(1, h));
    lv_obj_set_pos(b, x, y);
    return b;
  }
  static void restyle(lv_obj_t *block, theme::Role fill) {
    if (block) lv_obj_set_style_bg_color(block, ui::color(fill), 0);
  }
  lv_obj_t *glyph(lv_obj_t *parent, uint32_t icon, theme::Role role) {
    lv_obj_t *l = ui::label(parent, Font::ICON_SMALL, role);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, ui::line_height(Font::ICON_SMALL) + ui::px(4));
    ui::set_text(l, ui::icon(icon));
    return l;
  }
  static void set_icon(lv_obj_t *label, uint32_t icon) { ui::set_text(label, ui::icon(icon)); }
  static void hide(lv_obj_t *o, bool hidden) {
    if (!o) return;
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  bool inside(lv_obj_t *o, const tessera::Point &p) const {
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    const int x = lv_obj_get_x(o), y = lv_obj_get_y(o);
    return p.x >= x && p.x < x + lv_obj_get_width(o) && p.y >= y && p.y < y + lv_obj_get_height(o);
  }

  // A row: an icon at the left, the words, something muted at the right and a tick.
  void row(int i, uint32_t icon, const std::string &words, const std::string &side, bool ticked, bool faded = false,
           bool more = false) {
    Row &r = row_[i];
    hide(r.box, false);
    const int pad = gap_ + ui::px(4), icon_w = lv_obj_get_width(r.icon), mark_w = lv_obj_get_width(r.mark);
    set_icon(r.icon, icon);
    ui::set_color(r.icon, ticked ? theme::ACCENT : theme::MUTED);
    lv_obj_set_pos(r.icon, pad, (key_ - ui::line_height(Font::ICON_SMALL)) / 2);
    const int side_w = side.empty() ? 0 : std::min(ui::text_width(side, Font::BODY) + ui::px(4), width_ * 2 / 5);
    const int text_x = pad + icon_w + gap_;
    const int right = width_ - pad - mark_w - gap_;
    ui::set_text(r.side, side);
    lv_obj_set_size(r.side, std::max(1, side_w), ui::line_height(Font::BODY));
    lv_obj_set_pos(r.side, right - side_w, (key_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_size(r.text, std::max(1, right - side_w - gap_ - text_x), ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(r.text, text_x, (key_ - ui::line_height(Font::BODY_LARGE)) / 2);
    ui::set_text(r.text, words);
    ui::set_color(r.text, faded ? theme::SUBTLE : theme::INK);
    set_icon(r.mark, more ? ICON_RIGHT : ICON_CHECK);
    ui::set_color(r.mark, more ? theme::MUTED : theme::ACCENT);
    hide(r.mark, !ticked && !more);
    lv_obj_set_pos(r.mark, width_ - pad - mark_w, (key_ - ui::line_height(Font::ICON_SMALL)) / 2);
  }

  // The Library key: where the tile's Open library on says, else the list of kinds.
  void open_library(Players &tile) {
    favourites_ = !strcmp(tile.open_on(), "favourites");
    kind_page_ = 0;
    for (int k = 0; k < KIND_COUNT; ++k) {
      if (strcmp(tile.open_on(), KINDS[k].type)) continue;
      kind_ = k;
      offset_ = 0;
      show(View::ITEMS);
      return;
    }
    show(View::KINDS);
  }

  // How many items a page of the library asks for: as many as the card has rows, and no more than fit an answer whole.
  int page_size() const { return std::max(1, std::min(rows_, KINDS[kind_].page)); }

  void show(View view) {
    view_ = view;
    if (view == View::ROOMS) page_no_ = 0;
    if (view == View::ITEMS) ask();
    paint();
  }

  // The head of a list: where the next item plays, so a tap in the library never surprises.
  void paint_head(Players &tile, bool now) {
    const int pad = gap_ + ui::px(4);
    hide(library_, !now);
    const int head_w = now ? width_ - lv_obj_get_width(library_) - gap_ : width_;
    lv_obj_set_width(head_, head_w);
    set_icon(head_icon_, tile.room_count() > 1 ? ICON_ROOMS : ICON_SPEAKER);
    lv_obj_set_pos(head_icon_, pad, (key_ - ui::line_height(Font::ICON_SMALL)) / 2);
    const int text_x = pad + lv_obj_get_width(head_icon_) + gap_;
    const int more_w = now && tile.room_count() > 1 ? lv_obj_get_width(head_more_) : 0;
    hide(head_more_, more_w == 0);
    lv_obj_set_pos(head_more_, head_w - pad - lv_obj_get_width(head_more_), (key_ - ui::line_height(Font::ICON_SMALL)) / 2);
    lv_obj_set_size(head_text_, std::max(1, head_w - text_x - more_w - pad - gap_), ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(head_text_, text_x, (key_ - ui::line_height(Font::BODY_LARGE)) / 2);
    const std::string rooms = tile.rooms_text();
    ui::set_text(head_text_, now ? rooms : tessera::fill(plugin_->text("plays_in"), "rooms", rooms));
  }

  void paint() {
    Players *tile = plugin_->players_at(tile_);
    if (!parent_) return;
    const bool now = view_ == View::NOW;
    for (lv_obj_t *o : {cover_, title_, artist_, volume_}) hide(o, !now || !tile);
    for (int i = 0; i < 6; ++i) hide(keys_[i], !now || !tile);
    const bool list = !now;
    if (!list) for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
    hide(prev_, true);
    hide(next_, true);
    hide(page_, true);
    hide(note_, true);
    if (!tile) {
      hide(head_, true);
      hide(library_, true);
      hide(note_, false);
      ui::set_text(note_, plugin_->text("choose_player"));
      return;
    }
    hide(head_, false);
    paint_head(*tile, now);
    if (now) {
      paint_now(*tile);
      return;
    }
    if (view_ == View::ROOMS) paint_rooms(*tile);
    else if (view_ == View::KINDS) paint_kinds();
    else paint_items();
  }

  void paint_now(Players &tile) {
    const Room &lead = tile.room(tile.lead_slot());
    ui::set_text(title_, tile.now_playing());
    ui::set_color(title_, tile.noted() || gone(lead.state) ? theme::MUTED : theme::INK);
    ui::set_text(artist_, gone(lead.state) ? "" : lead.artist);
    const bool playing = !strcmp(lead.state, "playing");
    set_icon(key_icon_[1], playing ? ICON_PAUSE : ICON_PLAY);
    char words[16] = "";
    if (lead.volume >= 0) snprintf(words, sizeof(words), "%d", lead.volume);
    ui::set_text(volume_, lead.volume >= 0 ? tessera::fill(plugin_->text("volume"), "n", words) : std::string("-"));
    // The cover is the tile's own player's: Home Assistant's picture of another room is not this tile's to show.
    if (cover_) {
      const bool own = tile.lead_slot() == 0 && !gone(lead.state) && lead.title[0];
      hide(cover_, !own);
      if (own) ui::set_picture(cover_, std::string("cover:") + lead.id, tessera::Fit::COVER);
    }
  }

  void paint_rooms(Players &tile) {
    // With several at once, All rooms first; then every room, ticked when it plays the next item.
    entries_ = 0;
    int listed[SLOTS + 1];
    if (tile.several() && tile.room_count() > 1) listed[entries_++] = -1;
    for (int i = 0; i < SLOTS; ++i) if (tile.room(i).used()) listed[entries_++] = i;
    const int first = page_no_ * rows_;
    const uint16_t picked = tile.chosen();
    for (int i = 0; i < rows_; ++i) {
      const int at = first + i;
      if (at >= entries_) { hide(row_[i].box, true); continue; }
      row_entry_[i] = listed[at];
      if (listed[at] < 0) {
        row(i, ICON_ROOMS, plugin_->text("all_rooms"), "", picked == tile.used_mask());
        continue;
      }
      const Room &r = tile.room(listed[at]);
      row(i, ICON_SPEAKER, r.name, gone(r.state) ? plugin_->text("unavailable") : "", (picked >> listed[at]) & 1,
          gone(r.state));
    }
    pager(entries_);
  }

  void paint_kinds() {
    entries_ = (favourites_ ? 0 : 1) + KIND_COUNT;
    const int first = kind_page_ * rows_;
    for (int i = 0; i < rows_; ++i) {
      const int at = first + i;
      if (at >= entries_) { hide(row_[i].box, true); continue; }
      const int kind = favourites_ ? at : at - 1;
      row_entry_[i] = kind;
      if (kind < 0) row(i, ICON_HEART, plugin_->text("favourites"), "", false, false, true);
      else row(i, KINDS[kind].icon, plugin_->text(KINDS[kind].word), favourites_ ? plugin_->text("favourites") : "", false, false, true);
    }
    pager(entries_, kind_page_);
    if (!plugin_->players_at(tile_)->library()[0]) {
      for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
      hide(prev_, true);
      hide(next_, true);
      hide(page_, true);
      hide(note_, false);
      ui::set_text(note_, plugin_->text("no_library"));
    }
  }

  void paint_items() {
    if (loading_ || failed_ || count_ == 0) {
      for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
      hide(note_, false);
      ui::set_text(note_, plugin_->text(loading_ ? "loading" : failed_ ? "failed" : "empty"));
    }
    if (!loading_ && !failed_) {
      for (int i = 0; i < rows_; ++i) {
        if (i >= count_) { hide(row_[i].box, true); continue; }
        const Item &it = items_[i];
        row(i, KINDS[kind_].icon, it.name, it.side, false, !it.playable);
      }
    }
    // The pager: back while there is a page before, on while this page came full.
    hide(prev_, offset_ == 0);
    hide(next_, !more_ || loading_);
    hide(page_, loading_ && offset_ == 0);
    char n[8];
    snprintf(n, sizeof(n), "%d", offset_ / page_size() + 1);
    ui::set_text(page_, tessera::fill(plugin_->text("page"), "n", n));
  }

  // A pager over a list the card has whole.
  void pager(int entries, int page = -1) {
    if (page < 0) page = page_no_;
    const int pages = std::max(1, (entries + rows_ - 1) / rows_);
    hide(prev_, page == 0);
    hide(next_, page >= pages - 1);
    hide(page_, pages < 2);
    char n[8];
    snprintf(n, sizeof(n), "%d", page + 1);
    ui::set_text(page_, tessera::fill(plugin_->text("page"), "n", n));
  }

  void turn(int step) {
    if (view_ == View::ITEMS) {
      offset_ = std::max(0, offset_ + step * page_size());
      ask();
    } else if (view_ == View::KINDS) {
      kind_page_ = std::max(0, kind_page_ + step);
    } else {
      page_no_ = std::max(0, page_no_ + step);
    }
    paint();
  }

  void tap_row(Players &tile, int i) {
    if (view_ == View::ROOMS) {
      const int slot = row_entry_[i];
      if (slot < 0) { tile.choose(tile.used_mask()); paint(); return; }
      if (gone(tile.room(slot).state)) return;
      if (!tile.several()) {
        // One room at a time: this one, and back to Now playing.
        tile.choose(1u << slot);
        page_no_ = 0;
        show(View::NOW);
        return;
      }
      // Several: this one in or out, never the last one out.
      const uint16_t picked = tile.chosen() ^ (1u << slot);
      if (picked) tile.choose(picked);
      paint();
      return;
    }
    if (view_ == View::KINDS) {
      const int kind = row_entry_[i];
      if (kind < 0) { favourites_ = true; kind_page_ = 0; paint(); return; }
      kind_ = kind;
      offset_ = 0;
      show(View::ITEMS);
      return;
    }
    if (view_ == View::ITEMS && i < count_ && items_[i].playable) {
      tile.play(items_[i].uri, items_[i].type);
      show(View::NOW);
    }
  }

  // A page of the library, through the app: music_assistant.get_library of the tile's Music Assistant entry.
  void ask() {
    Players *tile = plugin_->players_at(tile_);
    count_ = 0;
    failed_ = false;
    more_ = false;
    if (!tile || !tile->library()[0]) { failed_ = true; return; }
    JsonDocument request;
    request["ask"] = "call_service:music_assistant.get_library";
    JsonObject data = request["data"].to<JsonObject>();
    data["config_entry_id"] = tile->library();
    data["media_type"] = KINDS[kind_].type;
    if (favourites_) data["favorite"] = true;
    data["limit"] = page_size();
    data["offset"] = offset_;
    data["order_by"] = "sort_name";
    asked_ = tessera::send(plugin_, request.as<JsonObjectConst>());
    loading_ = asked_ != 0;
    failed_ = asked_ == 0;
  }

  MusicRooms *plugin_;
  lv_obj_t *parent_{};
  int width_ = 0, height_ = 0, tile_ = -1, gap_ = 0, key_ = 0, round_ = 0, cover_x_ = 0, rows_ = 0;
  lv_obj_t *head_{}, *head_icon_{}, *head_text_{}, *head_more_{}, *library_{}, *library_text_{};
  lv_obj_t *cover_{}, *title_{}, *artist_{}, *volume_{}, *keys_[6] = {}, *key_icon_[6] = {};
  lv_obj_t *prev_{}, *prev_icon_{}, *next_{}, *next_icon_{}, *page_{}, *note_{};
  Row row_[ROWS];
  int row_entry_[ROWS] = {};
  View view_ = View::NOW;
  int page_no_ = 0, kind_page_ = 0, entries_ = 0;
  bool favourites_ = false;
  int kind_ = 0, offset_ = 0, count_ = 0;
  bool loading_ = false, failed_ = false, more_ = false;
  uint32_t asked_ = 0;
  Item items_[ROWS];
};

// ---- The plugin ----

void MusicRooms::setup() {
  add_tile("player", [this]() { return new PlayerTile(this); });
  add_card("music", [this]() { return new MusicCard(this); }, true);
}

Players *MusicRooms::players(int tile) {
  if (tile < 0) return nullptr;
  Entry *e = entry(tile);
  if (!e) {
    // A free entry; past 16 Music tiles on one screen the tile shows "Choose a player".
    for (Entry &free : entries_) if (free.tile < 0) { e = &free; break; }
    if (!e) return nullptr;
    e->tile = tile;
  }
  if (!e->players) e->players = new Players(this, tile);
  return e->players;
}

int MusicRooms::find(const char *entity) const {
  for (const Entry &e : entries_)
    if (e.players && !strcmp(e.players->room(0).id, entity)) return e.tile;
  return -1;
}

void MusicRooms::on_message(JsonObjectConst message) {
  if (card_) card_->answer(message);
}

}  // namespace esphome::music_rooms
