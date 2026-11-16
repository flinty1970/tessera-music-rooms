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
                   ICON_MUSIC = 0xF0387, ICON_BACKSPACE = 0xF004D, ICON_ALL = 0xF075A,  // arrow-left: the icon font has no backspace
                   ICON_SHUFFLE = 0xF049F, ICON_REPEAT = 0xF0456, ICON_REPEAT_ONE = 0xF0458;

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
// `found` is the list music_assistant.search answers it in.
struct Kind { const char *type, *word; uint32_t icon; int page; const char *found; };
static const Kind KINDS[] = {
    {"playlist", "playlists", 0xF0CB8, 6, "playlists"}, {"radio", "radio", 0xF0439, 6, "radio"},
    {"album", "albums", 0xF0025, 4, "albums"},          {"artist", "artists", 0xF0803, 6, "artists"},
    {"track", "tracks", 0xF0387, 4, "tracks"},          {"podcast", "podcasts", 0xF0994, 6, "podcasts"},
};
// The app gives a plugin at most 48 bytes of a text: a longer Music Assistant address (a Spotify artist's, a playlist's)
// comes cut, and plays by its name instead.
constexpr size_t WHOLE = 48;
constexpr int KIND_COUNT = sizeof(KINDS) / sizeof(KINDS[0]);
// A search of all music: these kinds (KINDS' indexes), a few of each, the artists first.
static const int EVERYTHING[] = {3, 4, 2, 0};
// The words in front of an item of a search of all music, by KINDS' index.
static const char *const ONE[] = {"one_playlist", "one_radio", "one_album", "one_artist", "one_track", "one_podcast"};
static int kind_of(const char *type) {
  for (int k = 0; k < KIND_COUNT; ++k) if (!strcmp(KINDS[k].type, type)) return k;
  return -1;
}

// ---- One player of a tile ----
struct Room {
  char id[64] = "";      // its entity, "" for an empty slot
  char name[49] = "";
  char state[16] = "";
  char title[49] = "";
  char artist[49] = "";
  char album[49] = "";
  int volume = -1;       // 0 to 100, -1 when it has none
  bool group = false;    // a Music Assistant group (mass_player_type "group"): it plays alone, never joined to others
  bool sync = false;     // it can be put together with others in step (media_player.join): Home Assistant's GROUPING
  // Where the song is: its length, and how far it was at `at` (seconds since 1970); -1 for a stream without a length.
  int duration = -1, position = -1;
  uint32_t at = 0;
  int8_t shuffle = -1;   // 1 on, 0 off, -1 not known
  char repeat[6] = "";   // off, all or one
  uint32_t updated = 0;  // when Home Assistant last heard of it (a room's): a new song changes it
  // A room other than the tile's own comes with its state, name and volume only, so eleven fit the tile's data; what
  // plays there comes from its Music Assistant queue (music_assistant.get_queue), asked by the screen: the question
  // that is out, when it went, and what the room was then.
  uint32_t asked = 0, asked_at = 0, asked_updated = 0;
  char asked_state[16] = "";
  bool used() const { return id[0] != '\0'; }
};

// How far a song is now, in seconds; -1 without a length (a stream). A playing song moves on from where it was.
static int position_now(const Room &r, uint32_t now) {
  if (r.duration <= 0 || r.position < 0) return -1;
  int at = r.position;
  if (!strcmp(r.state, "playing") && r.at && now > r.at) at += static_cast<int>(now - r.at);
  return std::min(at, r.duration);
}

// "3:07", or "1:02:03" past an hour.
static std::string clock_of(int seconds) {
  char words[16];
  seconds = std::max(0, seconds);
  if (seconds >= 3600) snprintf(words, sizeof(words), "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
  else snprintf(words, sizeof(words), "%d:%02d", seconds / 60, seconds % 60);
  return words;
}

// The cover of what plays in a room: the tile's own player's as the screen shows it (`cover:`); another room's from its
// image entity in Home Assistant (image.music_cover_<the player's object id>, made once per player, see the README),
// which Tessera may show on a tile of an entity as Home Assistant's own picture.
static std::string cover_of(const Room &r, bool own) {
  if (own) return std::string("cover:") + r.id;
  const char *dot = strchr(r.id, '.');
  return std::string("/api/image_proxy/image.music_cover_") + (dot ? dot + 1 : r.id);
}

// ---- A tile's players: its own and its rooms, their states, which rooms play next, and how an item plays ----
// Kept by the plugin per tile in PSRAM, so the card has them also while the tile itself is not made (a board without
// PSRAM makes a tile only while its page is on the glass).
class Players : public tessera::InPsram {
 public:
  Players(MusicRooms *plugin, int index) : plugin_(plugin), index_(index) {}

  // From the tile's entity and options, as create() has them.
  void configure(const char *own, JsonObjectConst options) {
    put(rooms_[0].id, own);
    char key[9];
    for (int i = 1; i < SLOTS; ++i) {
      snprintf(key, sizeof(key), "room_%d", i);
      const char *id = options[key] | "";
      // A room that is the tile's own player, or one named twice, is the one already listed.
      bool twice = !strcmp(id, rooms_[0].id);
      for (int j = 1; j < i && !twice; ++j) twice = !strcmp(id, rooms_[j].id);
      put(rooms_[i].id, twice ? "" : id);
    }
    // Several rooms at once, always since 0.8.0 (the setting gave its place to a tenth room); a tile saved with One
    // keeps it.
    several_ = strcmp(options["rooms_at_once"] | "several", "one") != 0;
    // The tile's own Music Assistant entry where it has one (a tile of 0.9.0 and before), else the screen's.
    put(library_, options["library"] | "");
    if (!library_[0]) put(library_, plugin_->library());
    put(open_on_, options["open_on"] | "kinds");
  }

  // The tile's data, as on_state has it: its own player, and the rooms under `entities`.
  void read(JsonObjectConst data) {
    if (!data["state"].isNull()) read(rooms_[0], data, true);
    char key[9];
    for (int i = 1; i < SLOTS; ++i) {
      snprintf(key, sizeof(key), "room_%d", i);
      JsonObjectConst room = data["entities"][key];
      if (room.isNull()) put(rooms_[i].state, "");
      else read(rooms_[i], room, false);
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
  // The room the others follow and the keys act on: of the rooms picked, the one that plays and that this tile did not
  // join to another (what plays leads, whatever its place in the list), else one that plays, else the first.
  int lead_slot() const { return lead_of(chosen()); }
  int lead_of(uint16_t picked) const {
    const uint16_t joined = plugin_->joined(index_);
    for (int pass = 0; pass < 3; ++pass)
      for (int i = 0; i < SLOTS; ++i) {
        if (!(picked & (1u << i)) || !rooms_[i].used()) continue;
        const bool playing = !strcmp(rooms_[i].state, "playing");
        if (pass == 0 && playing && !(joined & (1u << i))) return i;
        if (pass == 1 && playing) return i;
        if (pass == 2) return i;
      }
    return 0;
  }
  uint16_t used_mask() const {
    uint16_t mask = 0;
    for (int i = 0; i < SLOTS; ++i) if (rooms_[i].used()) mask |= 1u << i;
    return mask;
  }
  int room_count() const { return __builtin_popcount(used_mask()); }
  // The card takes this room over (what plays where, a tap on a room): its keys act on it and the next item plays
  // there; nothing is joined, taken apart or stopped, in this room or any other.
  void focus(int slot) {
    plugin_->set_chosen(index_, (1u << slot) & used_mask() ? (1u << slot) : 1);
    plugin_->set_joined(index_, 0);
    note_ = Note::NONE;
  }

  // Whether a room can be ticked to play with the rooms ticked now: always out again; in when it can play in step with
  // the room the keys act on (both can be grouped: Cast, AirPlay), or when it is a Music Assistant group (it takes the
  // music over), or when nothing plays in a room that cannot be grouped (the room is then switched, not added).
  // A screen's own speaker cannot play along with a group, and a second stream of one Spotify account is refused.
  enum class Add : uint8_t { YES, SWITCH, MOVE, NO };
  Add can_add(int slot) const {
    if (chosen() & (1u << slot)) return Add::YES;
    const Room &r = rooms_[slot];
    const Room &lead = rooms_[lead_slot()];
    const bool on = !strcmp(lead.state, "playing") || !strcmp(lead.state, "paused") || !strcmp(lead.state, "buffering");
    if (r.group) return Add::SWITCH;
    if (joinable(lead_slot()) && r.sync) return Add::YES;
    if (!on) return Add::SWITCH;
    // It cannot play in step with the room that plays (a screen's own speaker on either side): with the join script the
    // music moves to it; without it, it cannot be added.
    if (plugin_->join_script()[0] != '\0' && !lead.group) return Add::MOVE;
    return Add::NO;
  }

  void choose(uint16_t rooms) {
    const uint16_t before = chosen();
    rooms &= used_mask();
    plugin_->set_chosen(index_, rooms ? rooms : 1);
    note_ = Note::NONE;
    follow(before);
  }

  // While the first room plays, a room ticked joins it at once and one unticked leaves, so what plays follows the
  // ticks; a room that cannot join gets the next item. While nothing plays, the ticks wait for the next item.
  void follow(uint16_t before) {
    const uint16_t now = chosen();
    // The lead is what played before the tap: a room ticked now that plays something of its own joins it, not the
    // other way round.
    const uint16_t kept = before & now;
    if (!kept) return;
    const int lead = lead_of(kept);
    if (strcmp(rooms_[lead].state, "playing")) return;
    uint16_t joined = plugin_->joined(index_);
    // A room unticked leaves the group: one this tile joined, and one that plays and could be in it (joined from
    // another screen, the dashboard, or before the screen started again). Unjoining a room that plays alone does nothing.
    uint16_t gone_out = 0;
    for (int i = 0; i < SLOTS; ++i) {
      if (!(before & ~now & (1u << i)) || i == lead || !rooms_[i].used() || gone(rooms_[i].state)) continue;
      const bool on = !strcmp(rooms_[i].state, "playing") || !strcmp(rooms_[i].state, "paused");
      if ((joined & (1u << i)) || (on && joinable(i))) gone_out |= 1u << i;
    }
    for (int i = 0; i < SLOTS; ++i)
      if (gone_out & (1u << i)) tessera::action("media_player.unjoin", rooms_[i].id);
    joined &= ~gone_out;
    uint16_t come = 0, groups = 0;
    const bool script = plugin_->join_script()[0] != '\0';
    for (int i = 0; i < SLOTS; ++i) {
      if (!(now & ~before & (1u << i)) || i == lead || !rooms_[i].used() || gone(rooms_[i].state)) continue;
      // A Music Assistant group (Whole House) cannot join: with the join script what plays moves to it instead, and
      // the card then holds it.
      if (script && rooms_[i].group) groups |= 1u << i;
      // With the join script every other room goes to Home Assistant, which knows the groups: a room joins, one
      // already in a Music Assistant group that plays is left so, and when the lead cannot group (a screen's speaker) the
      // music moves to the room ticked; without it only rooms that can join a lead that can.
      else if (script || (joinable(lead) && joinable(i))) come |= 1u << i;
    }
    if ((come || groups) && join(lead, come | groups)) joined |= come & joinable_mask();
    plugin_->set_joined(index_, joined);
    for (int i = 0; i < SLOTS; ++i)
      if (groups & (1u << i)) { focus(i); break; }
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
    if (active(lead)) return lead.title;
    return plugin_->text("idle");
  }
  // Something on it: playing, paused or about to play, with a title. An idle player keeps what it last played, which is
  // no longer what plays.
  static bool active(const Room &r) {
    return r.title[0] && (!strcmp(r.state, "playing") || !strcmp(r.state, "paused") || !strcmp(r.state, "buffering"));
  }
  // Who and from what, under the title: the artist and the album ("The Reytons · What's Rock and Roll?"), either
  // alone when the other is missing, nothing while nothing plays.
  static std::string byline(const Room &r) {
    if (!active(r)) return "";
    if (r.artist[0] && r.album[0]) return std::string(r.artist) + " · " + r.album;
    return std::string(r.artist[0] ? r.artist : r.album);
  }
  bool noted() const { return note_ == Note::NOT_STARTED || note_ == Note::NOT_SENT; }

  // Play an item in the rooms picked: those Home Assistant cannot reach are left out; with several, the others join the
  // first one first (media_player.join), and rooms this tile joined before that are no longer picked leave
  // (media_player.unjoin), so it plays in exactly the rooms picked. Then play_media on the first, with the item's own
  // Music Assistant address and kind. False when no room could take it or nothing could be sent.
  bool play(const char *uri, const char *type, const char *name = "", const char *artist = "") {
    uint16_t rooms = 0;
    for (int i = 0; i < SLOTS; ++i)
      if ((chosen() & (1u << i)) && rooms_[i].used() && !gone(rooms_[i].state)) rooms |= 1u << i;
    if (!rooms) {
      note_ = Note::NOT_SENT;
      return false;
    }
    const int lead = lead_of(rooms);
    const uint16_t others = rooms & ~(1u << lead);
    // The rooms that can join the first one play in step with it; the others (a Music Assistant group, a player that
    // cannot be joined, or every room when the first cannot lead) get the item on their own, not in step.
    const uint16_t together = joinable(lead) ? (others & joinable_mask()) : 0;
    const uint16_t alone = others & ~together;
    bool sent = true;
    const uint16_t leave = plugin_->joined(index_) & ~together & ~(1u << lead);
    for (int i = 0; i < SLOTS; ++i)
      if ((leave & (1u << i)) && rooms_[i].used() && !gone(rooms_[i].state))
        tessera::action("media_player.unjoin", rooms_[i].id);
    if (together) sent = join(lead, together);
    plugin_->set_joined(index_, together);
    // Home Assistant's play_media with the item's own Music Assistant address and kind: on the first room, and on each
    // room that plays alone. An address the app cut (a Spotify artist's) plays by name and artist through Music
    // Assistant's own play_media, which finds the item itself.
    const bool whole = uri[0] && strlen(uri) < WHOLE;
    JsonDocument item;
    if (whole) {
      item["media_content_id"] = uri;
      item["media_content_type"] = type;
    } else {
      item["media_id"] = name;
      item["media_type"] = type;
      if (artist[0] && (!strcmp(type, "track") || !strcmp(type, "album"))) item["artist"] = artist;
    }
    const char *service = whole ? "media_player.play_media" : "music_assistant.play_media";
    const uint16_t give = alone | (1u << lead);
    // A cut address with the screen's play script: Home Assistant finds the one item whose address starts so, and
    // plays exactly that (a playlist of the same name from another account or provider plays nowhere).
    const char *script = plugin_->play_script();
    const bool exact = !whole && uri[0] && script[0];
    for (int i = 0; i < SLOTS; ++i) {
      if (!(give & (1u << i))) continue;
      if (exact) {
        JsonDocument call;
        JsonObject v = call["variables"].to<JsonObject>();
        v["player"] = rooms_[i].id;
        v["start"] = uri;
        v["name"] = name;
        v["kind"] = type;
        v["artist"] = artist;
        v["entry"] = library_;
        sent = tessera::action("script.turn_on", script, call.as<JsonVariantConst>()) && sent;
      } else {
        sent = tessera::action(service, rooms_[i].id, item.as<JsonVariantConst>()) && sent;
      }
    }
    note_ = sent ? Note::STARTING : Note::NOT_SENT;
    started_at_ = tessera::epoch();
    put(started_title_, rooms_[lead].title);
    put(started_state_, rooms_[lead].state);
    return sent;
  }

  bool joinable(int slot) const { return rooms_[slot].used() && rooms_[slot].sync && !rooms_[slot].group; }
  uint16_t joinable_mask() const {
    uint16_t mask = 0;
    for (int i = 0; i < SLOTS; ++i) if (joinable(i)) mask |= 1u << i;
    return mask;
  }
  // media_player.join: these rooms play what the first plays, in step with it.
  // With the screen's join script, Home Assistant joins them to the lead of the group the first room is in: Music
  // Assistant ignores a join to a room that is a member of another room's group, which the screen cannot tell.
  bool join(int lead, uint16_t rooms) {
    JsonDocument data;
    const char *script = plugin_->join_script();
    JsonObject into = script[0] ? data["variables"].to<JsonObject>() : data.to<JsonObject>();
    if (script[0]) into["player"] = rooms_[lead].id;
    JsonArray members = into[script[0] ? "members" : "group_members"].to<JsonArray>();
    for (int i = 0; i < SLOTS; ++i) if (rooms & (1u << i)) members.add(rooms_[i].id);
    if (script[0]) return tessera::action("script.turn_on", script, data.as<JsonVariantConst>());
    return tessera::action("media_player.join", rooms_[lead].id, data.as<JsonVariantConst>());
  }

  // What plays in the first room (its group, or the Music Assistant group it plays in) moves to this room alone: the
  // join script's move (music_assistant.transfer_queue). False without the join script or while nothing plays.
  bool move_to(int slot) {
    const char *script = plugin_->join_script();
    const int from = lead_slot();
    if (!script[0] || slot == from || !rooms_[slot].used() || !rooms_[from].used()) return false;
    JsonDocument data;
    JsonObject into = data["variables"].to<JsonObject>();
    into["player"] = rooms_[from].id;
    into["members"].to<JsonArray>().add(rooms_[slot].id);
    into["move"] = true;
    return tessera::action("script.turn_on", script, data.as<JsonVariantConst>());
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
    soon(rooms_[lead_slot()]);
  }
  // The volume keys of Now playing: the room the keys act on, or with all_volume every room it plays in, each from its
  // own level so their balance stays.
  bool all_volume() const { return all_volume_; }
  void toggle_all_volume() { all_volume_ = !all_volume_; }
  void step_volume_keys(int by) {
    if (!all_volume_) { step_volume(by); return; }
    for (int i = 0; i < SLOTS; ++i)
      if ((chosen() & (1u << i)) && rooms_[i].used()) step_volume(by, i);
  }
  void step_volume(int by, int slot = -1) {
    Room &lead = rooms_[slot < 0 ? lead_slot() : slot];
    if (!lead.used() || gone(lead.state) || lead.volume < 0) return;
    // To the next step of five in that direction: 42 goes to 45 up and 40 down.
    const int stepped = by > 0 ? (lead.volume / 5 + 1) * 5 : ((lead.volume + 4) / 5 - 1) * 5;
    lead.volume = std::max(0, std::min(100, stepped));
    JsonDocument level;
    level["volume_level"] = lead.volume / 100.0f;
    tessera::action("media_player.volume_set", lead.id, level.as<JsonVariantConst>());
  }

  // What plays in the rooms other than the tile's own: their Music Assistant queues, one question a second at most. A
  // room is asked when it starts or stops, when Home Assistant heard something new of it (a new song), and once a
  // minute while it plays; the room the keys act on sooner, so its song and its bar follow quickly.
  void ask(uint32_t now) {
    if (!now) return;
    const int lead = lead_slot();
    for (int i = 1; i < SLOTS; ++i) {
      Room &r = rooms_[i];
      if (!r.used() || gone(r.state)) continue;
      if (strcmp(r.state, "playing") && strcmp(r.state, "paused") && strcmp(r.state, "buffering")) continue;
      if (r.asked && now < r.asked_at + 10) continue;  // its answer is still on its way
      // Few questions: each goes through Home Assistant and the app to the screen, whose Wi-Fi also carries its own
      // speaker's music. The room the keys act on every 10 seconds at most when Home Assistant heard something new
      // of it, another room every 30, each at least every 2 minutes; a start or a stop at once.
      const uint32_t gap = i == lead ? 10 : 30;
      const bool due = !r.asked_at || strcmp(r.asked_state, r.state) ||
                       (r.updated != r.asked_updated && now >= r.asked_at + gap) || now >= r.asked_at + 120;
      if (!due) continue;
      JsonDocument request;
      request["ask"] = "call_service:music_assistant.get_queue";
      request["data"]["entity_id"] = r.id;
      r.asked = tessera::send(plugin_, request.as<JsonObjectConst>());
      r.asked_at = now;
      r.asked_updated = r.updated;
      put(r.asked_state, r.state);
      return;
    }
  }
  // An answer to one of ask()'s questions, as the manifest's `answers` map it; false when it is not one of them.
  bool take(JsonObjectConst message) {
    const uint32_t re = message["re"] | 0u;
    if (!re) return false;
    for (int i = 1; i < SLOTS; ++i) {
      Room &r = rooms_[i];
      if (r.asked != re) continue;
      r.asked = 0;
      if (!(message["ok"] | false)) return true;
      JsonObjectConst q = message["result"];
      // A stream (the radio) says its song in stream_title, its station is the item.
      const char *stream = q["stream"] | "";
      put(r.title, stream[0] ? stream : (q["title"] | ""));
      put(r.artist, stream[0] ? (q["title"] | "") : (q["artist"] | ""));
      put(r.album, stream[0] ? "" : (q["album"] | ""));
      r.duration = q["duration"].is<float>() ? static_cast<int>(q["duration"].as<float>()) : -1;
      r.position = q["elapsed"].is<float>() ? static_cast<int>(q["elapsed"].as<float>()) : -1;
      r.at = tessera::epoch();
      const char *on = q["shuffle"] | "";
      r.shuffle = on[0] ? (!strcmp(on, "True") || !strcmp(on, "true") || !strcmp(on, "1")) : -1;
      put(r.repeat, q["repeat"] | "");
      return true;
    }
    return false;
  }

  // Shuffle on and off, and repeat off, all and one, on the room the keys act on.
  void toggle_shuffle() {
    Room &lead = rooms_[lead_slot()];
    if (!lead.used() || gone(lead.state)) return;
    lead.shuffle = lead.shuffle == 1 ? 0 : 1;
    JsonDocument data;
    data["shuffle"] = lead.shuffle == 1;
    tessera::action("media_player.shuffle_set", lead.id, data.as<JsonVariantConst>());
    soon(lead);
  }
  void step_repeat() {
    Room &lead = rooms_[lead_slot()];
    if (!lead.used() || gone(lead.state)) return;
    put(lead.repeat, !strcmp(lead.repeat, "all") ? "one" : !strcmp(lead.repeat, "one") ? "off" : "all");
    JsonDocument data;
    data["repeat"] = lead.repeat;
    tessera::action("media_player.repeat_set", lead.id, data.as<JsonVariantConst>());
    soon(lead);
  }
  // Ask a room's queue again in a moment, after a key changed it.
  static void soon(Room &r) {
    const uint32_t now = tessera::epoch();
    r.asked_at = now > 3 ? now - 3 : 0;
    r.asked_updated = 0;
  }

  // A start that took: the first room plays something else than before, or plays where it did not. One that did not
  // take within 30 seconds says so, until the room plays.
  void settle(uint32_t now) {
    const Room &lead = rooms_[lead_slot()];
    const bool playing = !strcmp(lead.state, "playing");
    // A room that started late (a group, a Cast speaker waking up) is not "did not start" once it plays.
    if (note_ == Note::NOT_STARTED && playing) { note_ = Note::NONE; return; }
    if (note_ != Note::STARTING || !now) return;
    if (playing && (strcmp(started_state_, "playing") || strcmp(started_title_, lead.title))) {
      note_ = Note::NONE;
      return;
    }
    if (started_at_ && now > started_at_ + 30) note_ = playing ? Note::NONE : Note::NOT_STARTED;
  }

 private:
  enum class Note : uint8_t { NONE, STARTING, NOT_STARTED, NOT_SENT };

  // A player's data: the tile's own with what plays there; a room with its state, name and volume (what plays comes
  // from its queue, take()).
  static void read(Room &room, JsonObjectConst data, bool own) {
    put(room.state, data["state"] | "");
    put(room.name, data["name"] | "");
    if (!room.name[0]) put(room.name, room.id);
    room.updated = data["updated"] | 0u;
    JsonObjectConst a = data["attributes"];
    if (own) {
      put(room.title, a["media_title"] | "");
      put(room.artist, a["media_artist"] | "");
      put(room.album, a["media_album_name"] | "");
      room.duration = a["media_duration"].is<float>() ? static_cast<int>(a["media_duration"].as<float>()) : -1;
      room.position = a["media_position"].is<float>() ? static_cast<int>(a["media_position"].as<float>()) : -1;
      room.at = a["media_position_updated_at"] | 0u;
      room.shuffle = a["shuffle"].is<bool>() ? (a["shuffle"].as<bool>() ? 1 : 0) : -1;
      put(room.repeat, a["repeat"] | "");
    }
    room.volume = a["volume_level"].is<float>() ? static_cast<int>(a["volume_level"].as<float>() * 100.0f + 0.5f) : -1;
    room.group = !strcmp(a["mass_player_type"] | "", "group");
    // MediaPlayerEntityFeature.GROUPING: Music Assistant can join it to another player (a Cast, a Sonos), where a
    // player it reaches through another integration (a screen's own speaker) plays alone.
    room.sync = ((a["supported_features"] | 0u) & 524288u) != 0;
  }

  MusicRooms *plugin_;
  int index_;
  Room rooms_[SLOTS];
  char library_[49] = "";
  char open_on_[16] = "kinds";
  char started_title_[49] = "", started_state_[16] = "";
  bool several_ = false, all_volume_ = false;
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
    // As tall as the tile where it is wide, at most two fifths of its width where it is not (a 3 x 2 tile of a square
    // screen), always with the words' room beside it.
    const int side = std::min(height_, width_ * 2 / 5);
    if (ui::pictures() && !small && side >= ui::mm(18) && width_ - side >= ui::mm(30)) {
      cover_ = ui::picture(c.parent);
      lv_obj_set_size(cover_, side, side);
      lv_obj_set_pos(cover_, 0, (height_ - side) / 2);
      text_x = side + ui::px(12);
    }
    text_x_ = shifted_ = text_x;
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
    lv_label_set_long_mode(artist_, LV_LABEL_LONG_DOT);  // a long album ends in dots, on one line
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
    players_->ask(now);
    const int slot = players_->lead_slot();
    const Room &lead = players_->room(slot);
    ui::set_text(where_, players_->rooms_text());
    ui::set_text(title_, players_->now_playing());
    ui::set_text(artist_, Players::byline(lead));
    if (cover_) {
      // The cover of what plays in the room the keys act on: the tile's own player's, or another room's from its image
      // entity. A room without one shows none.
      const std::string address = Players::active(lead) ? cover_of(lead, slot == 0) : std::string();
      if (address != address_) {
        address_ = address;
        missing_ = false;
      }
      const bool show = !address.empty() && !missing_;
      if (show) ui::set_picture(cover_, address, tessera::Fit::COVER);
      if (show) lv_obj_remove_flag(cover_, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
      // Without the cover the words take the whole tile, so no empty frame stands where it was.
      shift(show ? text_x_ : 0);
    }
  }

  // A room without an image entity, or one whose player has no picture now: the words take the tile.
  void on_picture(lv_obj_t *place, tessera::PictureState state) override {
    if (place != cover_) return;
    missing_ = state != tessera::PictureState::SHOWN;
    if (missing_) {
      lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
      shift(0);
    }
  }

  // The words' left edge, kept here: LVGL's own coordinates are not laid out yet right after create().
  void shift(int x) {
    if (x == shifted_) return;
    shifted_ = x;
    for (lv_obj_t *o : {where_, title_, artist_}) {
      lv_obj_set_x(o, x);
      lv_obj_set_width(o, std::max(1, width_ - x));
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
  int index_ = -1, width_ = 0, height_ = 0, text_x_ = 0, shifted_ = 0;
  lv_obj_t *cover_{}, *where_{}, *title_{}, *artist_{};
  std::string address_;
  bool missing_ = false;
};

// ---- The card: Now playing, the rooms, the library ----
class MusicCard : public tessera::Card {
 public:
  explicit MusicCard(MusicRooms *plugin) : plugin_(plugin) {}
  ~MusicCard() override {
    if (plugin_->card_ == this) plugin_->card_ = nullptr;
    delete shelf_;
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
    // The keys of Now playing: previous, play, next, shuffle, repeat, and the volume between its two keys, in a row as
    // large as the width allows, never smaller than a finger.
    const int volume_w = ui::text_width(tessera::fill(plugin_->text("volume_all"), "n", "100").c_str(), Font::BODY_LARGE) + ui::px(8);
    round_ = std::max(ui::mm(7), std::min(key_, (width_ - volume_w - 8 * gap_) / 7));

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
    // What plays where: every room of the tile with what it plays; a tap takes that room over.
    const int playing_w = std::min(width_ / 4, ui::text_width(plugin_->text("everywhere"), Font::BODY_LARGE) + 2 * gap_ + ui::px(24));
    everywhere_key_ = button(width_ - library_w - gap_ - playing_w, 0, playing_w, key_, theme::RAISED);
    everywhere_text_ = ui::label(everywhere_key_, Font::BODY_LARGE, theme::INK);
    lv_obj_set_width(everywhere_text_, playing_w - 2 * gap_);
    lv_obj_set_style_text_align(everywhere_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(everywhere_text_, LV_ALIGN_CENTER, 0, 0);
    ui::set_text(everywhere_text_, plugin_->text("everywhere"));

    // Now playing: the cover of the tile's own player, what plays and who, and the keys.
    // The bar of the song over the keys: how far it is, at its left, and how long, at its right.
    const int bar_h = ui::line_height(Font::BODY);
    const int body_y = key_ + gap_, body_h = height_ - body_y - round_ - 2 * gap_ - bar_h;
    if (ui::pictures() && body_h > ui::mm(12)) {
      const int side = std::min(body_h, width_ * 2 / 5);
      cover_ = ui::picture(parent_);
      lv_obj_set_size(cover_, side, side);
      lv_obj_set_pos(cover_, 0, body_y + (body_h - side) / 2);
      cover_x_ = side + gap_ * 2;
      shifted_ = cover_x_;
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
    lv_label_set_long_mode(artist_, LV_LABEL_LONG_DOT);  // a long album ends in dots, on one line
    lv_obj_set_width(artist_, std::max(1, width_ - text_x));
    const int text_h = title_h + gap_ + artist_h;
    const int text_y = body_y + std::max(0, (body_h - text_h) / 2);
    lv_obj_set_pos(title_, text_x, text_y);
    lv_obj_set_pos(artist_, text_x, text_y + title_h + gap_);
    const int keys_y = height_ - round_;
    {
      const int time_w = ui::text_width("0:00:00", Font::BODY) + ui::px(4);
      const int bar_y = keys_y - gap_ - bar_h;
      time_at_ = ui::label(parent_, Font::BODY, theme::MUTED);
      lv_obj_set_size(time_at_, time_w, bar_h);
      lv_obj_set_pos(time_at_, 0, bar_y);
      time_all_ = ui::label(parent_, Font::BODY, theme::MUTED);
      lv_obj_set_size(time_all_, time_w, bar_h);
      lv_obj_set_style_text_align(time_all_, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(time_all_, width_ - time_w, bar_y);
      const int line = ui::px(6);
      bar_w_ = std::max(1, width_ - 2 * time_w - 2 * gap_);
      bar_ = lv_obj_create(parent_);
      lv_obj_remove_style_all(bar_);
      lv_obj_remove_flag(bar_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(bar_, bar_w_, line);
      lv_obj_set_pos(bar_, time_w + gap_, bar_y + (bar_h - line) / 2);
      lv_obj_set_style_radius(bar_, line / 2, 0);
      lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
      restyle(bar_, theme::TRACK);
      bar_fill_ = lv_obj_create(bar_);
      lv_obj_remove_style_all(bar_fill_);
      lv_obj_remove_flag(bar_fill_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(bar_fill_, 0, line);
      lv_obj_set_pos(bar_fill_, 0, 0);
      lv_obj_set_style_radius(bar_fill_, line / 2, 0);
      lv_obj_set_style_bg_opa(bar_fill_, LV_OPA_COVER, 0);
      restyle(bar_fill_, theme::ACCENT);
    }
    const uint32_t icons[KEYS_NOW] = {ICON_PREVIOUS, ICON_PLAY, ICON_NEXT, ICON_SHUFFLE, ICON_REPEAT, ICON_MINUS, 0, ICON_PLUS};
    int x = 0;
    for (int i = 0; i < KEYS_NOW; ++i) {
      if (i == 5) {
        // The volume stands between its two keys, at the right.
        x = width_ - (2 * round_ + volume_w + 2 * gap_);
      }
      if (i == 6) {
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
      if (i == 2) x += gap_;  // shuffle and repeat a little apart from the track keys
      lv_obj_align(key_icon_[i], LV_ALIGN_CENTER, 0, 0);
      x += round_ + gap_;
    }

    // The lists: as many rows as fit between the head and the pager.
    // A row and the pager are a finger's height, lower than the head's keys, so more rows fit a page.
    const int row_gap = ui::px(6);
    row_h_ = std::min(key_, std::max(ui::mm(8), ui::line_height(Font::BODY_LARGE) + ui::px(16)));
    rows_ = std::max(1, std::min(ROWS, (height_ - key_ - row_h_ - 2 * gap_ + row_gap) / (row_h_ + row_gap)));
    for (int i = 0; i < rows_; ++i) {
      Row &r = row_[i];
      r.y = key_ + gap_ + i * (row_h_ + row_gap);
      r.box = button(0, r.y, width_, row_h_, theme::CARD);
      r.icon = glyph(r.box, ICON_MUSIC, theme::MUTED);
      r.text = ui::label(r.box, Font::BODY_LARGE, theme::INK);
      r.side = ui::label(r.box, Font::BODY, theme::MUTED);
      r.mark = glyph(r.box, ICON_CHECK, theme::ACCENT);
    }
    const int pager_y = height_ - row_h_;
    prev_ = button(0, pager_y, key_ * 2, row_h_, theme::RAISED);
    prev_icon_ = glyph(prev_, ICON_LEFT, theme::INK);
    lv_obj_align(prev_icon_, LV_ALIGN_CENTER, 0, 0);
    next_ = button(width_ - key_ * 2, pager_y, key_ * 2, row_h_, theme::RAISED);
    next_icon_ = glyph(next_, ICON_RIGHT, theme::INK);
    lv_obj_align(next_icon_, LV_ALIGN_CENTER, 0, 0);
    page_ = ui::label(parent_, Font::BODY, theme::MUTED);
    lv_obj_set_size(page_, std::max(1, width_ - 4 * key_ - 2 * gap_), ui::line_height(Font::BODY));
    lv_obj_set_style_text_align(page_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(page_, key_ * 2 + gap_, pager_y + (row_h_ - ui::line_height(Font::BODY)) / 2);
    note_ = ui::text(parent_, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_size(note_, width_, ui::line_height(Font::BODY_LARGE) * 2);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, key_ + gap_ + ui::px(16));

    // The kinds: a grid of keys, each its icon and its word, laid out by paint_kinds for as many as it shows.
    for (int i = 0; i < KIND_KEYS; ++i) {
      kind_key_[i].box = button(0, 0, 1, 1, theme::RAISED);
      kind_key_[i].icon = glyph(kind_key_[i].box, ICON_MUSIC, theme::ACCENT);
      kind_key_[i].text = ui::label(kind_key_[i].box, Font::BODY_LARGE, theme::INK);
      lv_obj_set_style_text_align(kind_key_[i].text, LV_TEXT_ALIGN_CENTER, 0);
      hide(kind_key_[i].box, true);
    }
#if LV_USE_ROLLER
    // The library's items on a wheel, as the alarm clock's time: the chosen one in the middle, its neighbours above
    // and below; a swipe turns it. One row over it holds where it plays, the search key and the Play key, which plays
    // the one in the middle, so the wheel has the rest of the card. Its items live in PSRAM.
    shelf_ = new Shelf();
    const int wheel_y = key_ + gap_;
    const int wheel_h = height_ - wheel_y;
    if (shelf_ && wheel_h >= ui::mm(15)) {
      Font face = Font::BODY_LARGE;
      for (Font f : {Font::TITLE})
        if (5 * ui::line_height(f) + 4 * ui::px(10) <= wheel_h) face = f;
      wheel_ = lv_roller_create(parent_);
      lv_roller_set_options(wheel_, " ", LV_ROLLER_MODE_NORMAL);
      ui::set_font(wheel_, face);
      lv_obj_set_style_text_font(wheel_, lv_obj_get_style_text_font(wheel_, LV_PART_MAIN), LV_PART_SELECTED);
      lv_obj_set_style_text_align(wheel_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_line_space(wheel_, ui::px(10), 0);
      lv_obj_set_style_border_width(wheel_, 0, 0);
      lv_obj_set_style_radius(wheel_, ui::px(12), 0);
      lv_obj_set_style_bg_opa(wheel_, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_opa(wheel_, LV_OPA_COVER, LV_PART_SELECTED);
      lv_obj_set_size(wheel_, width_, wheel_h);
      lv_obj_set_pos(wheel_, 0, wheel_y);
      lv_obj_add_event_cb(wheel_, &MusicCard::wheel_turned, LV_EVENT_VALUE_CHANGED, this);
      style_wheel();
      const int lh = ui::line_height(Font::BODY_LARGE), icon_w = ui::line_height(Font::ICON_SMALL) + ui::px(4);
      const int play_tw = ui::text_width(plugin_->text("play"), Font::BODY_LARGE) + 2;
      play_w_ = icon_w + gap_ + play_tw + 3 * gap_;
      play_ = button(width_ - play_w_, 0, play_w_, key_, theme::ACCENT);
      play_icon_ = glyph(play_, ICON_PLAY, theme::ON_ACCENT);
      lv_obj_set_pos(play_icon_, (play_w_ - icon_w - gap_ - play_tw) / 2, (key_ - ui::line_height(Font::ICON_SMALL)) / 2);
      play_text_ = ui::label(play_, Font::BODY_LARGE, theme::ON_ACCENT);
      lv_obj_set_size(play_text_, play_tw, lh);
      lv_obj_set_pos(play_text_, (play_w_ - icon_w - gap_ - play_tw) / 2 + icon_w + gap_, (key_ - lh) / 2);
      ui::set_text(play_text_, plugin_->text("play"));
      find_w_ = ui::text_width(plugin_->text("search"), Font::BODY_LARGE) + 2 + 3 * gap_;
      find_ = button(width_ - play_w_ - gap_ - find_w_, 0, find_w_, key_, theme::RAISED);
      find_icon_ = ui::label(find_, Font::BODY_LARGE, theme::INK);
      ui::set_text(find_icon_, plugin_->text("search"));
      lv_obj_align(find_icon_, LV_ALIGN_CENTER, 0, 0);
      // Open: an artist's albums, an album's songs; beside Search, shown for an artist or an album in the middle.
      open_w_ = ui::text_width(plugin_->text("open"), Font::BODY_LARGE) + 2 + 3 * gap_;
      open_ = button(width_ - play_w_ - find_w_ - open_w_ - 2 * gap_, 0, open_w_, key_, theme::RAISED);
      open_text_ = ui::label(open_, Font::BODY_LARGE, theme::INK);
      ui::set_text(open_text_, plugin_->text("open"));
      lv_obj_align(open_text_, LV_ALIGN_CENTER, 0, 0);
      // What plays where, on a wheel of its own: every room with what it plays; Control takes the middle one over.
      rooms_wheel_ = lv_roller_create(parent_);
      lv_roller_set_options(rooms_wheel_, " ", LV_ROLLER_MODE_NORMAL);
      ui::set_font(rooms_wheel_, face);
      lv_obj_set_style_text_font(rooms_wheel_, lv_obj_get_style_text_font(rooms_wheel_, LV_PART_MAIN), LV_PART_SELECTED);
      lv_obj_set_style_text_align(rooms_wheel_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_line_space(rooms_wheel_, ui::px(10), 0);
      lv_obj_set_style_border_width(rooms_wheel_, 0, 0);
      lv_obj_set_style_radius(rooms_wheel_, ui::px(12), 0);
      lv_obj_set_style_bg_opa(rooms_wheel_, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_opa(rooms_wheel_, LV_OPA_COVER, LV_PART_SELECTED);
      lv_obj_set_size(rooms_wheel_, width_, wheel_h);
      lv_obj_set_pos(rooms_wheel_, 0, wheel_y);
      lv_obj_add_event_cb(rooms_wheel_, &MusicCard::rooms_turned, LV_EVENT_VALUE_CHANGED, this);
      take_w_ = ui::text_width(plugin_->text("take"), Font::BODY_LARGE) + 2 + 4 * gap_;
      take_ = button(width_ - take_w_, 0, take_w_, key_, theme::ACCENT);
      take_text_ = ui::label(take_, Font::BODY_LARGE, theme::ON_ACCENT);
      ui::set_text(take_text_, plugin_->text("take"));
      lv_obj_align(take_text_, LV_ALIGN_CENTER, 0, 0);
      // Move here: what plays moves to the room in the middle of the wheel.
      move_w_ = ui::text_width(plugin_->text("move_here"), Font::BODY_LARGE) + 2 + 4 * gap_;
      move_ = button(width_ - take_w_ - gap_ - move_w_, 0, move_w_, key_, theme::RAISED);
      move_text_ = ui::label(move_, Font::BODY_LARGE, theme::INK);
      ui::set_text(move_text_, plugin_->text("move_here"));
      lv_obj_align(move_text_, LV_ALIGN_CENTER, 0, 0);
      // The volume of the room in the middle of the wheel, under it: each room its own, also while several play together.
      const int volume_w = ui::text_width("100 %", Font::BODY_LARGE) + ui::px(8);
      const int row_y = height_ - key_;
      room_down_ = button(0, row_y, key_ * 2, key_, theme::RAISED);
      room_down_icon_ = glyph(room_down_, ICON_MINUS, theme::INK);
      lv_obj_align(room_down_icon_, LV_ALIGN_CENTER, 0, 0);
      room_up_ = button(width_ - key_ * 2, row_y, key_ * 2, key_, theme::RAISED);
      room_up_icon_ = glyph(room_up_, ICON_PLUS, theme::INK);
      lv_obj_align(room_up_icon_, LV_ALIGN_CENTER, 0, 0);
      room_volume_ = ui::label(parent_, Font::BODY_LARGE, theme::INK);
      lv_label_set_long_mode(room_volume_, LV_LABEL_LONG_DOT);
      lv_obj_set_size(room_volume_, std::max(volume_w, width_ - key_ * 4 - 2 * gap_), ui::line_height(Font::BODY_LARGE));
      lv_obj_set_style_text_align(room_volume_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(room_volume_, key_ * 2 + gap_, row_y + (key_ - ui::line_height(Font::BODY_LARGE)) / 2);
      style_wheel();
      hide(rooms_wheel_, true);
      hide(take_, true);
      hide(move_, true);
      for (lv_obj_t *o : {room_down_, room_up_, room_volume_}) hide(o, true);
      make_keyboard();
      hide(wheel_, true);
      hide(play_, true);
      hide(find_, true);
      hide(open_, true);
    }
#endif

    show(View::NOW);
  }

  // The tile's data comes to the card too: its players' states stay current while the tile is not made.
  void on_state(JsonObjectConst data) override {
    if (Players *tile = plugin_->players_at(tile_)) tile->read(data);
    paint();
  }
  void on_tick(uint32_t now) override {
    if (Players *tile = plugin_->players_at(tile_)) {
      tile->settle(now);
      tile->ask(now);
    }
    paint();
  }
  // A room without an image entity, or a player without a picture now: what plays takes the card's width.
  void on_picture(lv_obj_t *place, tessera::PictureState state) override {
    if (place != cover_) return;
    missing_ = state != tessera::PictureState::SHOWN;
    if (missing_) paint();
  }
  void on_theme() override {
    restyle(head_, theme::RAISED);
    restyle(library_, theme::RAISED);
    restyle(everywhere_key_, theme::RAISED);
    restyle(take_, theme::ACCENT);
    ui::set_color(take_text_, theme::ON_ACCENT);
    restyle(move_, theme::RAISED);
    ui::set_color(move_text_, theme::INK);
    ui::set_color(everywhere_text_, theme::INK);
    restyle(prev_, theme::RAISED);
    restyle(next_, theme::RAISED);
    for (int i = 0; i < KEYS_NOW; ++i) if (keys_[i]) restyle(keys_[i], i == 1 ? theme::ACCENT : theme::RAISED);
    for (lv_obj_t *k : {room_down_, room_up_}) if (k) restyle(k, theme::RAISED);
    restyle(bar_, theme::TRACK);
    restyle(bar_fill_, theme::ACCENT);
    for (int i = 0; i < rows_; ++i) restyle(row_[i].box, theme::CARD);
    for (int i = 0; i < KIND_KEYS; ++i) {
      restyle(kind_key_[i].box, theme::RAISED);
      ui::set_color(kind_key_[i].icon, theme::ACCENT);
      ui::set_color(kind_key_[i].text, theme::INK);
    }
    style_wheel();
    restyle(find_, theme::RAISED);
    restyle(open_, theme::RAISED);
    ui::set_color(open_text_, theme::INK);
    restyle(field_, theme::RAISED);
    restyle(go_, theme::ACCENT);
    ui::set_color(find_icon_, theme::INK);
    ui::set_color(go_text_, theme::ON_ACCENT);
    for (TypeKey &k : type_) {
      restyle(k.box, theme::RAISED);
      ui::set_color(k.text, theme::INK);
    }
    for (lv_obj_t *l : {head_icon_, head_text_, library_text_, title_, volume_}) if (l) ui::set_color(l, theme::INK);
    for (lv_obj_t *l : {head_more_, artist_, page_, note_}) if (l) ui::set_color(l, theme::MUTED);
    for (int i = 0; i < KEYS_NOW; ++i) if (key_icon_[i]) ui::set_color(key_icon_[i], i == 1 ? theme::ON_ACCENT : theme::INK);
    for (lv_obj_t *l : {time_at_, time_all_}) if (l) ui::set_color(l, theme::MUTED);
    for (lv_obj_t *l : {room_down_icon_, room_up_icon_, room_volume_}) if (l) ui::set_color(l, theme::INK);
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
      case View::ITEMS:
        // An album's songs go back to its artist's albums where it came from there, an artist's albums to the list
        // they were opened from; a search's results go back to the whole list, the list to the kinds.
        if (opened_ == 2) { opened_ = from_artist_ ? 1 : 0; show(View::ITEMS); return true; }
        if (opened_ == 1) { opened_ = 0; show(View::ITEMS); return true; }
        if (everywhere_) { view_ = View::SEARCH; paint(); return true; }
        if (query_[0]) { query_[0] = '\0'; show(View::ITEMS); return true; }
        show(View::KINDS);
        return true;
      case View::SEARCH:
        // From a search of all music back to the kinds; from a kind's search back to its list.
        if (everywhere_) { everywhere_ = false; query_[0] = '\0'; show(View::KINDS); return true; }
        view_ = View::ITEMS;
        paint();
        return true;
      case View::KINDS:
        if (favourites_) { favourites_ = false; kind_page_ = 0; show(View::KINDS); return true; }
        show(View::NOW);
        return true;
      case View::ROOMS: show(View::NOW); return true;
      case View::PLAYING: show(View::NOW); return true;
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
      if (inside(everywhere_key_, p)) { show(View::PLAYING); return; }
      if (inside(keys_[0], p)) tile->step_track(false);
      else if (inside(keys_[1], p)) tile->play_pause();
      else if (inside(keys_[2], p)) tile->step_track(true);
      else if (inside(keys_[3], p)) tile->toggle_shuffle();
      else if (inside(keys_[4], p)) tile->step_repeat();
      else if (inside(keys_[5], p)) tile->step_volume_keys(-5);
      else if (inside(keys_[7], p)) tile->step_volume_keys(5);
      // A tap on the volume switches the keys between this room and all its rooms.
      else if (inside(volume_, p)) tile->toggle_all_volume();
      paint();
      return;
    }
    if (view_ == View::KINDS)
      for (int i = 0; i < KIND_KEYS; ++i)
        if (inside(kind_key_[i].box, p)) { pick_kind(kind_entry_[i]); return; }
    if (view_ == View::ITEMS && wheel_) {
      if (inside(play_, p)) play_chosen(*tile);
      else if (inside(open_, p)) open_chosen();
      else if (inside(find_, p)) { view_ = View::SEARCH; paint(); }
      return;
    }
    if (view_ == View::SEARCH) {
      type_at(p);
      return;
    }
#if LV_USE_ROLLER
    if (view_ == View::PLAYING && rooms_wheel_) {
      const uint32_t n = lv_roller_get_selected(rooms_wheel_);
      if (static_cast<int>(n) >= entries_) return;
      const int slot = playing_slots_[n];
      if (inside(room_down_, p) || inside(room_up_, p)) {
        tile->step_volume(inside(room_up_, p) ? 5 : -5, slot);
        paint();
        return;
      }
      if (move_ && !lv_obj_has_flag(move_, LV_OBJ_FLAG_HIDDEN) && inside(move_, p)) {
        if (gone(tile->room(slot).state) || !tile->move_to(slot)) return;
        tile->focus(slot);
        show(View::NOW);
        return;
      }
      if (!inside(take_, p)) return;
      if (gone(tile->room(slot).state)) return;
      tile->focus(slot);
      show(View::NOW);
      return;
    }
#endif
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
    if (wheel_) {
      take(message);
      return;
    }
    count_ = 0;
    if (!(message["ok"] | false)) {
      failed_ = true;
    } else {
      for (JsonObjectConst item : message["result"]["items"].as<JsonArrayConst>()) {
        if (count_ >= ROWS) break;
        Item &it = items_[count_];
        put(it.name, item["name"] | "");
        const char *uri = item["uri"] | "";
        // An address cut at the app's 48 bytes plays by the item's name.
        it.playable = uri[0] != '\0';
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
  enum class View : uint8_t { NONE, NOW, ROOMS, KINDS, ITEMS, SEARCH, PLAYING };
  // The keyboard of a search: digits, three rows of letters, and a row of space and delete.
  static constexpr int KEYS = 10 + 26 + 2;
  struct TypeKey { lv_obj_t *box = nullptr, *text = nullptr; char ch = 0; };
  static constexpr int ROWS = 6;
  struct Row { lv_obj_t *box = nullptr, *icon = nullptr, *text = nullptr, *side = nullptr, *mark = nullptr; int y = 0; };
  struct Item { char name[49] = "", uri[49] = "", type[16] = "", side[49] = ""; bool playable = false; };
  // What the Open key opens: an artist (its albums) or an album (its songs).
  enum Opened : uint8_t { CLOSED = 0, ARTIST = 1, ALBUM = 2 };
  // The items of the wheel, as many as a person turns through: kept in PSRAM, made when the card opens.
  static constexpr int SHELF = 120;
  struct Shelf : public tessera::InPsram { Item items[SHELF]; };
  // How many the wheel asks for before a person turns it, so it opens with more than a page.
  static constexpr int FIRST_FILL = 18;
  // Favourites, every kind, and Search all.
  static constexpr int KIND_KEYS = KIND_COUNT + 2;
  struct KindKey { lv_obj_t *box = nullptr, *icon = nullptr, *text = nullptr; };

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
    lv_obj_set_pos(r.icon, pad, (row_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
    const int side_w = side.empty() ? 0 : std::min(ui::text_width(side, Font::BODY) + ui::px(4), width_ * 2 / 5);
    const int text_x = pad + icon_w + gap_;
    const int right = width_ - pad - mark_w - gap_;
    ui::set_text(r.side, side);
    lv_obj_set_size(r.side, std::max(1, side_w), ui::line_height(Font::BODY));
    lv_obj_set_pos(r.side, right - side_w, (row_h_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_size(r.text, std::max(1, right - side_w - gap_ - text_x), ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(r.text, text_x, (row_h_ - ui::line_height(Font::BODY_LARGE)) / 2);
    ui::set_text(r.text, words);
    ui::set_color(r.text, faded ? theme::SUBTLE : theme::INK);
    set_icon(r.mark, more ? ICON_RIGHT : ICON_CHECK);
    ui::set_color(r.mark, more ? theme::MUTED : theme::ACCENT);
    hide(r.mark, !ticked && !more);
    lv_obj_set_pos(r.mark, width_ - pad - mark_w, (row_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
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
    if (view == View::ROOMS || view == View::PLAYING) page_no_ = 0;
    if (view == View::PLAYING) start_playing_ = true;
    if (view == View::ITEMS) {
      total_ = 0;
      offset_ = 0;
      fill_wheel();
      ask();
    }
    paint();
  }

  // The head of a list: where the next item plays, so a tap in the library never surprises.
  void paint_head(Players &tile, bool now) {
    const int pad = gap_ + ui::px(4);
    hide(library_, !now);
    hide(everywhere_key_, !now);
    const bool row = view_ == View::ITEMS && wheel_;
    const bool taking = view_ == View::PLAYING && rooms_wheel_;
    // lv_obj_get_width is an int32_t: a long on the ESP32, an int on the bench's computer.
    const int head_w = std::max<int>(1, now ? width_ - static_cast<int>(lv_obj_get_width(library_)) -
                                                  static_cast<int>(lv_obj_get_width(everywhere_key_)) - 2 * gap_
                                       : row ? width_ - play_w_ - find_w_ - open_w_ - 3 * gap_
                                       : taking ? width_ - take_w_ - gap_ - (move_ ? move_w_ + gap_ : 0) : width_);
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
    if (row && opened_) {
      // An artist's albums, or an album's songs: the head names what is open.
      set_icon(head_icon_, KINDS[opened_ == 1 ? 3 : 2].icon);
      const std::string album = tessera::fill(plugin_->text("opened_album"), "album", open_album_);
      ui::set_text(head_text_, opened_ == 1 ? std::string(open_artist_)
                                            : tessera::fill(album.c_str(), "artist", open_artist_));
      return;
    }
    if (row && query_[0]) {
      // While a search shows its results, the head says what was searched in which kind.
      set_icon(head_icon_, everywhere_ ? ICON_ALL : KINDS[kind_].icon);
      const std::string searched = tessera::fill(plugin_->text("searched"), "q", query_);
      ui::set_text(head_text_, everywhere_ ? searched_all()
                                           : tessera::fill(searched.c_str(), "kind", plugin_->text(KINDS[kind_].word)));
      return;
    }
    ui::set_text(head_text_, now ? rooms : tessera::fill(plugin_->text("plays_in"), "rooms", rooms));
  }

  void paint() {
    Players *tile = plugin_->players_at(tile_);
    if (!parent_) return;
    const bool now = view_ == View::NOW;
    for (lv_obj_t *o : {cover_, title_, artist_, volume_, bar_, time_at_, time_all_}) hide(o, !now || !tile);
    for (int i = 0; i < KEYS_NOW; ++i) hide(keys_[i], !now || !tile);
    const bool list = !now;
    if (!list) for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
    hide(prev_, true);
    hide(next_, true);
    hide(page_, true);
    hide(note_, true);
    if (view_ != View::KINDS || !tile) for (int i = 0; i < KIND_KEYS; ++i) hide(kind_key_[i].box, true);
    if (view_ != View::ITEMS || !tile) {
      hide(wheel_, true);
      hide(play_, true);
      hide(find_, true);
      hide(open_, true);
    }
    if (view_ != View::PLAYING || !tile) {
      hide(rooms_wheel_, true);
      hide(take_, true);
      hide(move_, true);
      for (lv_obj_t *o : {room_down_, room_up_, room_volume_}) hide(o, true);
    }
    if (view_ != View::SEARCH || !tile) {
      hide(field_, true);
      hide(go_, true);
      for (TypeKey &k : type_) hide(k.box, true);
    }
    if (!tile) {
      hide(head_, true);
      hide(library_, true);
      hide(everywhere_key_, true);
      hide(note_, false);
      ui::set_text(note_, plugin_->text("choose_player"));
      return;
    }
    if (view_ == View::SEARCH) {
      hide(head_, true);
      hide(library_, true);
      hide(everywhere_key_, true);
      for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
      paint_search();
      return;
    }
    hide(head_, false);
    paint_head(*tile, now);
    if (now) {
      paint_now(*tile);
      return;
    }
    if (view_ == View::ROOMS) paint_rooms(*tile);
    else if (view_ == View::PLAYING) paint_playing(*tile);
    else if (view_ == View::KINDS) paint_kinds();
    else paint_items();
  }

  void paint_now(Players &tile) {
    const Room &lead = tile.room(tile.lead_slot());
    ui::set_text(title_, tile.now_playing());
    ui::set_color(title_, tile.noted() || gone(lead.state) ? theme::MUTED : theme::INK);
    ui::set_text(artist_, Players::byline(lead));
    const bool playing = !strcmp(lead.state, "playing");
    set_icon(key_icon_[1], playing ? ICON_PAUSE : ICON_PLAY);
    char words[16] = "";
    if (lead.volume >= 0) snprintf(words, sizeof(words), "%d", lead.volume);
    ui::set_text(volume_, lead.volume >= 0 ? tessera::fill(plugin_->text(tile.all_volume() ? "volume_all" : "volume"), "n", words)
                                           : std::string("-"));
    // Shuffle and repeat in the accent while on; repeat one with its own icon.
    ui::set_color(key_icon_[3], lead.shuffle == 1 ? theme::ACCENT : theme::MUTED);
    set_icon(key_icon_[4], !strcmp(lead.repeat, "one") ? ICON_REPEAT_ONE : ICON_REPEAT);
    ui::set_color(key_icon_[4], lead.repeat[0] && strcmp(lead.repeat, "off") ? theme::ACCENT : theme::MUTED);
    // The bar of the song: how far, how long; empty for a stream or nothing.
    const int at = Players::active(lead) ? position_now(lead, tessera::epoch()) : -1;
    ui::set_text(time_at_, at >= 0 ? clock_of(at) : std::string());
    ui::set_text(time_all_, at >= 0 ? clock_of(lead.duration) : std::string());
    const int filled = at >= 0 ? static_cast<int>(static_cast<int64_t>(bar_w_) * at / lead.duration) : 0;
    if (filled != bar_shown_) {
      bar_shown_ = filled;
      lv_obj_set_width(bar_fill_, filled);
    }
    // The cover of what plays in the room the keys act on: the tile's own player's, or another room's image entity.
    if (cover_) {
      const int slot = tile.lead_slot();
      const std::string address = Players::active(lead) ? cover_of(lead, slot == 0) : std::string();
      if (address != address_) {
        address_ = address;
        missing_ = false;
      }
      const bool own = !address.empty() && !missing_;
      hide(cover_, !own);
      if (own) ui::set_picture(cover_, address, tessera::Fit::COVER);
      // Without the cover what plays and who take the card's whole width.
      const int x = own ? cover_x_ : 0;
      if (x != shifted_) {
        shifted_ = x;
        for (lv_obj_t *o : {title_, artist_}) {
          lv_obj_set_x(o, x);
          lv_obj_set_width(o, std::max(1, width_ - x));
        }
      }
    }
  }

  // What plays where: every room of the tile, those that play first, each with what it plays and who; a tick on the
  // room the card holds now.
  void paint_playing(Players &tile) {
    entries_ = 0;
    int listed[SLOTS];
    for (int pass = 0; pass < 3; ++pass)
      for (int i = 0; i < SLOTS; ++i) {
        const Room &r = tile.room(i);
        if (!r.used()) continue;
        const int rank = !strcmp(r.state, "playing") ? 0 : !strcmp(r.state, "paused") ? 1 : 2;
        if (rank == pass) listed[entries_++] = i;
      }
    const int lead = tile.lead_slot();
#if LV_USE_ROLLER
    if (rooms_wheel_) {
      for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
      hide(rooms_wheel_, false);
      hide(take_, false);
      std::string lines;
      int at_lead = 0;
      for (int n = 0; n < entries_; ++n) {
        const int slot = listed[n];
        playing_slots_[n] = slot;
        if (slot == lead) at_lead = n;
        if (n) lines += '\n';
        lines += tile.room(slot).name;
        const std::string what = playing_words(tile.room(slot));
        if (!what.empty()) lines += std::string(" — ") + what;
      }
      // The wheel keeps its place while what plays changes; it starts on the room the card holds.
      if (lines != playing_lines_) {
        const uint32_t was = lv_roller_get_selected(rooms_wheel_);
        playing_lines_ = lines;
        lv_roller_set_options(rooms_wheel_, entries_ ? lines.c_str() : " ", LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(rooms_wheel_, 5);
        lv_obj_set_height(rooms_wheel_, wheel_h_() - key_ - gap_);
        lv_roller_set_selected(rooms_wheel_, std::min<uint32_t>(was, entries_ ? entries_ - 1 : 0), LV_ANIM_OFF);
      }
      if (start_playing_) {
        start_playing_ = false;
        lv_roller_set_selected(rooms_wheel_, at_lead, LV_ANIM_OFF);
      }
      // The middle room's volume between its keys: "Stereo 35 %".
      for (lv_obj_t *o : {room_down_, room_up_, room_volume_}) hide(o, entries_ == 0);
      const uint32_t middle = lv_roller_get_selected(rooms_wheel_);
      {
        const Room &from = tile.room(lead);
        const bool on = !strcmp(from.state, "playing") || !strcmp(from.state, "paused") || !strcmp(from.state, "buffering");
        const bool other = static_cast<int>(middle) < entries_ && playing_slots_[middle] != lead &&
                           !gone(tile.room(playing_slots_[middle]).state);
        hide(move_, !(plugin_->join_script()[0] && on && other));
      }
      if (static_cast<int>(middle) < entries_) {
        const Room &r = tile.room(playing_slots_[middle]);
        char words[16] = "";
        if (r.volume >= 0) snprintf(words, sizeof(words), "%d", r.volume);
        ui::set_text(room_volume_, r.name + std::string("  ") +
                                       (r.volume >= 0 ? tessera::fill(plugin_->text("volume"), "n", words) : std::string("-")));
      }
      return;
    }
#endif
    const int first = page_no_ * rows_;
    for (int i = 0; i < rows_; ++i) {
      const int at = first + i;
      if (at >= entries_) { hide(row_[i].box, true); continue; }
      const int slot = listed[at];
      row_entry_[i] = slot;
      const Room &r = tile.room(slot);
      const bool playing = !strcmp(r.state, "playing"), paused = !strcmp(r.state, "paused");
      const std::string what = playing_words(r);
      row(i, playing ? ICON_PLAY : paused ? ICON_PAUSE : ICON_SPEAKER, r.name, what, slot == lead, gone(r.state));
    }
    pager(entries_);
  }

  // What a room plays and who, or that nothing plays, or that Home Assistant cannot reach it.
  std::string playing_words(const Room &r) const {
    if (gone(r.state)) return plugin_->text("unavailable");
    const bool on = !strcmp(r.state, "playing") || !strcmp(r.state, "paused");
    if (on && r.title[0]) return r.artist[0] ? std::string(r.title) + " · " + r.artist : std::string(r.title);
    return plugin_->text("idle");
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
      const Players::Add add = tile.can_add(listed[at]);
      const bool cannot = add == Players::Add::NO;
      row(i, ICON_SPEAKER, r.name,
          gone(r.state)                 ? plugin_->text("unavailable")
          : cannot                      ? plugin_->text("cannot_join")
          : add == Players::Add::MOVE   ? plugin_->text("moves_here")
                                        : "",
          (picked >> listed[at]) & 1, gone(r.state) || cannot);
    }
    pager(entries_);
  }

  // The kinds as a grid of keys: Favourites first, then every kind; four across where they keep a finger's width.
  void paint_kinds() {
    for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
    const bool has_library = plugin_->players_at(tile_)->library()[0] != '\0';
    // Favourites (-1) first, then every kind, then Search all (-2); in the favourites, the kinds alone.
    int listed[KIND_KEYS], entries = 0;
    if (!favourites_) listed[entries++] = -1;
    for (int k = 0; k < KIND_COUNT; ++k) listed[entries++] = k;
    if (!favourites_ && wheel_) listed[entries++] = -2;
    if (!has_library) {
      for (int i = 0; i < KIND_KEYS; ++i) hide(kind_key_[i].box, true);
      hide(note_, false);
      ui::set_text(note_, plugin_->text("no_library"));
      return;
    }
    const int icon_w = ui::line_height(Font::ICON_SMALL) + ui::px(4), lh = ui::line_height(Font::BODY_LARGE);
    int widest = 0;
    for (int i = 0; i < entries; ++i) widest = std::max(widest, ui::text_width(kind_word(listed[i]), Font::BODY_LARGE));
    // The most keys across that keep every word whole and every key a finger's height, all on the card: with the icon
    // over the word where the key is tall enough, else beside it. Two across, smaller, when nothing else fits.
    // On a small card the keys take the room of the head too (Now playing shows where it plays).
    int cols = 2, kh = 0, kw = 0, top = 0;
    bool stacked = false, fits = false;
    for (int head : {key_ + gap_, 0}) {
      const int room = height_ - head;
      for (int n : {4, 3, 2}) {
        const int lines = (entries + n - 1) / n;
        const int h = std::min(key_ * 2, (room - (lines - 1) * gap_) / lines), w = (width_ - (n - 1) * gap_) / n;
        const bool over = h >= icon_w + lh + ui::px(12) && w >= widest + ui::px(12);
        const bool beside = h >= std::max(ui::mm(8), lh + ui::px(8)) && w >= icon_w + gap_ + widest + ui::px(16);
        cols = n, kh = h, kw = w, stacked = over, top = head;
        if ((fits = over || beside)) break;
      }
      if (fits) break;
    }
    hide(head_, top == 0);
    for (int i = 0; i < KIND_KEYS; ++i) {
      KindKey &k = kind_key_[i];
      if (i >= entries) { hide(k.box, true); continue; }
      const int kind = listed[i];
      kind_entry_[i] = kind;
      hide(k.box, false);
      lv_obj_set_pos(k.box, (i % cols) * (kw + gap_), top + (i / cols) * (kh + gap_));
      lv_obj_set_size(k.box, kw, kh);
      set_icon(k.icon, kind == -1 ? ICON_HEART : kind == -2 ? ICON_ALL : KINDS[kind].icon);
      ui::set_text(k.text, kind_word(kind));
      if (stacked) {
        lv_obj_set_width(k.icon, kw);
        lv_obj_set_pos(k.icon, 0, (kh - icon_w - lh) / 2);
        lv_obj_set_size(k.text, kw - ui::px(8), lh);
        lv_obj_set_pos(k.text, ui::px(4), (kh - icon_w - lh) / 2 + icon_w);
      } else {
        lv_obj_set_width(k.icon, icon_w);
        const int tw = std::min(kw - icon_w - gap_ - ui::px(8), ui::text_width(lv_label_get_text(k.text), Font::BODY_LARGE) + 2);
        const int x0 = std::max(ui::px(4), (kw - icon_w - gap_ - tw) / 2);
        lv_obj_set_pos(k.icon, x0, (kh - ui::line_height(Font::ICON_SMALL)) / 2);
        lv_obj_set_size(k.text, std::max(1, tw), lh);
        lv_obj_set_pos(k.text, x0 + icon_w + gap_, (kh - lh) / 2);
      }
    }
  }

  const char *kind_word(int kind) const {
    return plugin_->text(kind == -1 ? "favourites" : kind == -2 ? "search_all" : KINDS[kind].word);
  }

  void pick_kind(int kind) {
    query_[0] = '\0';
    opened_ = CLOSED;
    everywhere_ = kind == -2;
    if (everywhere_) { view_ = View::SEARCH; paint(); return; }
    if (kind < 0) { favourites_ = true; paint(); return; }
    kind_ = kind;
    offset_ = 0;
    show(View::ITEMS);
  }

  void paint_items() {
    if (wheel_) {
      for (int i = 0; i < rows_; ++i) hide(row_[i].box, true);
      const bool any = total_ > 0;
      hide(wheel_, !any);
      hide(play_, !any);
      hide(find_, false);
      if (!any) hide(open_, true);
      hide(note_, any);
      if (!any) ui::set_text(note_, plugin_->text(loading_ ? "loading" : failed_ ? "failed" : "empty"));
      if (any) paint_play();
      return;
    }
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
    if (view_ == View::PLAYING && rooms_wheel_) return;
    if (view_ == View::PLAYING) {
      const int slot = row_entry_[i];
      if (slot < 0 || gone(tile.room(slot).state)) return;
      tile.focus(slot);
      show(View::NOW);
      return;
    }
    if (view_ == View::ROOMS) {
      const int slot = row_entry_[i];
      if (slot < 0) {
        // All rooms: every room that can play in step with the one the keys act on.
        uint16_t all = 1u << tile.lead_slot();
        for (int s = 0; s < SLOTS; ++s)
          if (tile.room(s).used() && tile.can_add(s) == Players::Add::YES) all |= 1u << s;
        tile.choose(all);
        paint();
        return;
      }
      if (gone(tile.room(slot).state)) return;
      const Players::Add add = tile.can_add(slot);
      if (add == Players::Add::NO) {
        plugin_->explain(tile.room(slot).name, tile.room(tile.lead_slot()).name, false);
        return;
      }
      if (add == Players::Add::SWITCH) {
        // A Music Assistant group, or a room while nothing plays in one that cannot be grouped: this room instead.
        tile.choose(tile.chosen() | (1u << slot));
        if (!tile.room(slot).group || !tile.room(tile.lead_slot()).used()) tile.focus(slot);
        paint();
        return;
      }
      if (add == Players::Add::MOVE) {
        // What plays moves here (the join script's music_assistant.transfer_queue); this room is then the one in charge.
        // Sent as a move, whatever was ticked before: a room still ticked from earlier would otherwise send nothing.
        if (!tile.move_to(slot)) return;
        plugin_->explain(tile.room(slot).name, tile.room(tile.lead_slot()).name, true);
        tile.focus(slot);
        page_no_ = 0;
        show(View::NOW);
        return;
      }
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
      pick_kind(row_entry_[i]);
      return;
    }
    if (view_ == View::ITEMS && i < count_ && items_[i].playable) {
      tile.play(items_[i].uri, items_[i].type, items_[i].name, items_[i].side);
      show(View::NOW);
    }
  }

  // A page of the library for the wheel: its items after those it has, and the next page asked for while it opened
  // with fewer than FIRST_FILL; later pages when a person turns it near its end.
  void take(JsonObjectConst message) {
    loading_ = false;
    if (!(message["ok"] | false)) {
      failed_ = total_ == 0;
      paint();
      tessera::refresh();
      return;
    }
    int got = 0;
    // A page of the library comes as `items`, a search as the kind's own list (`artists`, `tracks`, ...); a search
    // of all music as several of those, the artists first.
    JsonArrayConst lists[4];
    int n = 0;
    lists[n] = message["result"][opened_ == ALBUM ? "children" : "items"].as<JsonArrayConst>();
    if (!lists[n].isNull()) ++n;
    else if (opened_ == ARTIST) lists[n++] = message["result"]["albums"].as<JsonArrayConst>();
    else if (!everywhere_) lists[n++] = message["result"][KINDS[kind_].found].as<JsonArrayConst>();
    else for (int k : EVERYTHING) lists[n++] = message["result"][KINDS[k].found].as<JsonArrayConst>();
    for (int l = 0; l < n; ++l)
    for (JsonObjectConst item : lists[l]) {
      if (total_ >= SHELF) break;
      Item &it = shelf_->items[total_];
      if (opened_ == ALBUM) {
        // A song of the album as Home Assistant browses it: "The Artist - The Song", its address, a track.
        const char *title = item["title"] | "";
        const size_t lead = strlen(open_artist_);
        if (lead && !strncmp(title, open_artist_, lead) && !strncmp(title + lead, " - ", 3)) title += lead + 3;
        put(it.name, title);
        put(it.uri, item["media_content_id"] | "");
        put(it.type, "track");
        put(it.side, open_artist_);
        it.playable = it.uri[0] != '\0' && (item["can_play"] | true);
        if (!it.name[0]) continue;
        ++total_;
        ++got;
        continue;
      }
      // An artist's albums: a search by its name also finds others', which stay out.
      if (opened_ == ARTIST && strcasecmp(item["artists"][0]["name"] | "", open_artist_)) continue;
      put(it.name, item["name"] | "");
      if (!it.name[0]) continue;
      // A name is one line of the wheel.
      for (char *ch = it.name; *ch; ++ch) if (*ch == '\n' || *ch == '\r') *ch = ' ';
      const char *uri = item["uri"] | "";
      // An address cut at the app's 48 bytes plays by the item's name.
      it.playable = uri[0] != '\0';
      put(it.uri, uri);
      put(it.type, item["media_type"] | KINDS[kind_].type);
      put(it.side, item["artists"][0]["name"] | "");
      ++total_;
      ++got;
    }
    // A search answers once, with as many as it found: no more to ask after it.
    more_ = !searching_ && got >= KINDS[kind_].page && total_ < SHELF;
    fill_wheel();
    if (more_ && total_ < FIRST_FILL) ask();
    paint();
    tessera::refresh();
  }

  // The wheel's lines: every item it has, the chosen one kept where it was.
  void fill_wheel() {
#if LV_USE_ROLLER
    if (!wheel_) return;
    const uint32_t chosen = total_ ? std::min<uint32_t>(lv_roller_get_selected(wheel_), total_ - 1) : 0;
    std::string lines;
    for (int i = 0; i < total_; ++i) {
      const Item &it = shelf_->items[i];
      if (i) lines += '\n';
      const int k = kind_of(it.type);
      const bool labels = everywhere_ && !opened_;
      if (labels && k >= 0) lines += tessera::fill(plugin_->text(ONE[k]), "name", it.name);
      else lines += it.name;
      if (labels && it.side[0] && (k == 2 || k == 4)) lines += std::string(" · ") + it.side;
    }
    lv_roller_set_options(wheel_, total_ ? lines.c_str() : " ", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(wheel_, 5);
    lv_obj_set_height(wheel_, wheel_h_());
    lv_roller_set_selected(wheel_, chosen, LV_ANIM_OFF);
#endif
  }
  int wheel_h_() const { return height_ - (key_ + gap_); }

  int chosen() const {
#if LV_USE_ROLLER
    if (wheel_ && total_) return std::min<int>(lv_roller_get_selected(wheel_), total_ - 1);
#endif
    return -1;
  }

  // The Play key: grey for an item that cannot play.
  void paint_play() {
    const int i = chosen();
    if (i < 0) return;
    const Item &it = shelf_->items[i];
    const bool can = it.playable;
    // Open for an artist (its albums, found by name) and an album whose address came whole (its songs).
    const int k = kind_of(it.type);
    hide(open_, !(k == 3 || (k == 2 && it.uri[0] && strlen(it.uri) < WHOLE)));
    restyle(play_, can ? theme::ACCENT : theme::RAISED);
    ui::set_color(play_icon_, can ? theme::ON_ACCENT : theme::SUBTLE);
    ui::set_color(play_text_, can ? theme::ON_ACCENT : theme::SUBTLE);
  }

  // The keyboard of a search, made once with the wheel: a field with what is typed and the search key over the keys.
  void make_keyboard() {
    field_ = button(0, 0, 1, 1, theme::RAISED);
    field_text_ = ui::label(field_, Font::BODY_LARGE, theme::INK);
    go_ = button(0, 0, 1, 1, theme::ACCENT);
    go_text_ = ui::label(go_, Font::BODY_LARGE, theme::ON_ACCENT);
    ui::set_text(go_text_, plugin_->text("search"));
    static const char *const LINES[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    int n = 0;
    for (const char *line : LINES)
      for (const char *ch = line; *ch; ++ch) type_[n++].ch = *ch;
    type_[n++].ch = '\b';
    type_[n++].ch = ' ';
    for (TypeKey &k : type_) {
      k.box = button(0, 0, 1, 1, theme::RAISED);
      k.text = ui::label(k.box, k.ch == '\b' ? Font::ICON_SMALL : Font::BODY_LARGE, theme::INK);
      lv_obj_set_style_text_align(k.text, LV_TEXT_ALIGN_CENTER, 0);
      const char word[2] = {k.ch, '\0'};
      ui::set_text(k.text, k.ch == '\b'  ? ui::icon(ICON_BACKSPACE)
                           : k.ch == ' ' ? std::string(plugin_->text("space"))
                                         : std::string(word));
      hide(k.box, true);
    }
    hide(field_, true);
    hide(go_, true);
  }

  // The search: what is typed and the search key in the top row, the keys in five rows under it, as large as the
  // card allows. The keys are laid out here, as the kinds' are.
  std::string searched_all() const { return tessera::fill(plugin_->text("searched_all"), "q", query_); }

  void paint_search() {
    const int lh = ui::line_height(Font::BODY_LARGE);
    const int go_tw = ui::text_width(plugin_->text("search"), Font::BODY_LARGE) + 2;
    const int go_w = go_tw + 4 * gap_;
    hide(field_, false);
    hide(go_, false);
    lv_obj_set_pos(go_, width_ - go_w, 0);
    lv_obj_set_size(go_, go_w, key_);
    lv_obj_set_size(go_text_, go_tw, lh);
    lv_obj_set_pos(go_text_, (go_w - go_tw) / 2, (key_ - lh) / 2);
    lv_obj_set_pos(field_, 0, 0);
    lv_obj_set_size(field_, width_ - go_w - gap_, key_);
    const int fh = ui::line_height(Font::BODY_LARGE);
    lv_obj_set_size(field_text_, std::max(1, width_ - go_w - 3 * gap_ - ui::px(8)), fh);
    lv_obj_set_pos(field_text_, gap_ + ui::px(4), (key_ - fh) / 2);
    // What is typed, with a bar where the next letter comes; the kind it searches while nothing is typed.
    ui::set_text(field_text_, query_[0] ? std::string(query_) + "|"
                                        : everywhere_ ? std::string(plugin_->text("search_all"))
                                        : tessera::fill(plugin_->text("search_in"), "kind", plugin_->text(KINDS[kind_].word)));
    ui::set_color(field_text_, query_[0] ? theme::INK : theme::MUTED);
    const int top = key_ + gap_, g = ui::px(6);
    const int kh = (height_ - top - 4 * g) / 5, kw = (width_ - 9 * g) / 10;
    int at = 0;
    for (int r = 0; r < 4; ++r) {
      const int count = r < 2 ? 10 : r == 2 ? 9 : 7;
      // Each row in the middle; the last row of letters keeps room for delete at its right.
      const int extra = r == 3 ? kw * 3 / 2 + g : 0;
      const int x0 = (width_ - count * kw - (count - 1) * g - extra) / 2;
      for (int i = 0; i < count; ++i) place(type_[at++], x0 + i * (kw + g), top + r * (kh + g), kw, kh);
      if (r == 3) place(type_[at++], x0 + count * (kw + g), top + r * (kh + g), kw * 3 / 2, kh);
    }
    // Space across the middle of the last row.
    place(type_[at], (width_ - kw * 5 - 4 * g) / 2, top + 4 * (kh + g), kw * 5 + 4 * g, kh);
  }

  void place(TypeKey &k, int x, int y, int w, int h) {
    hide(k.box, false);
    lv_obj_set_pos(k.box, x, y);
    lv_obj_set_size(k.box, std::max(1, w), std::max(1, h));
    const int lh = ui::line_height(k.ch == '\b' ? Font::ICON_SMALL : Font::BODY_LARGE);
    lv_obj_set_size(k.text, std::max(1, w), lh);
    lv_obj_set_pos(k.text, 0, (h - lh) / 2);
  }

  // A tap on the keyboard: a letter, a digit or a space at the end, delete the last, or search.
  void type_at(const tessera::Point &p) {
    if (inside(go_, p)) {
      if (everywhere_ && !query_[0]) return;
      opened_ = CLOSED;
      show(View::ITEMS);
      return;
    }
    for (const TypeKey &k : type_) {
      if (!inside(k.box, p)) continue;
      const size_t n = strlen(query_);
      if (k.ch == '\b') {
        if (n) query_[n - 1] = '\0';
      } else if (n + 1 < sizeof(query_) && !(k.ch == ' ' && (n == 0 || query_[n - 1] == ' '))) {
        // Letters as a name reads: a capital first and after a space, small ones after.
        char ch = k.ch;
        if (ch >= 'A' && ch <= 'Z' && n && query_[n - 1] != ' ') ch = static_cast<char>(ch - 'A' + 'a');
        query_[n] = ch;
        query_[n + 1] = '\0';
      }
      paint();
      return;
    }
  }

  void open_chosen() {
    const int i = chosen();
    if (i < 0) return;
    const Item &it = shelf_->items[i];
    const int k = kind_of(it.type);
    if (k == 3) {
      put(open_artist_, it.name);
      opened_ = ARTIST;
    } else if (k == 2) {
      from_artist_ = opened_ == ARTIST;
      put(open_album_, it.name);
      if (it.side[0]) put(open_artist_, it.side);
      put(open_uri_, it.uri);
      opened_ = ALBUM;
    } else {
      return;
    }
    show(View::ITEMS);
  }

  void play_chosen(Players &tile) {
    const int i = chosen();
    if (i < 0 || !shelf_->items[i].playable) return;
    tile.play(shelf_->items[i].uri, shelf_->items[i].type, shelf_->items[i].name, shelf_->items[i].side);
    show(View::NOW);
  }

  // The wheel's colours, as the alarm clock's: the middle line in the accent.
  void style_wheel() {
    if (rooms_wheel_) {
      lv_obj_set_style_bg_color(rooms_wheel_, ui::color(theme::RAISED), 0);
      lv_obj_set_style_text_color(rooms_wheel_, ui::color(theme::MUTED), 0);
      lv_obj_set_style_bg_color(rooms_wheel_, ui::tone_color(tessera::Tone::ACCENT, theme::INK), LV_PART_SELECTED);
      lv_obj_set_style_text_color(rooms_wheel_, ui::color(theme::ON_ACCENT), LV_PART_SELECTED);
    }
    if (!wheel_) return;
    lv_obj_set_style_bg_color(wheel_, ui::color(theme::RAISED), 0);
    lv_obj_set_style_text_color(wheel_, ui::color(theme::MUTED), 0);
    lv_obj_set_style_bg_color(wheel_, ui::tone_color(tessera::Tone::ACCENT, theme::INK), LV_PART_SELECTED);
    lv_obj_set_style_text_color(wheel_, ui::color(theme::ON_ACCENT), LV_PART_SELECTED);
  }

#if LV_USE_ROLLER
  // A turn of the wheel: the Play key follows it, and near the end the next page is asked for.
  // The rooms' wheel turned: the volume under it is the new middle room's.
  static void rooms_turned(lv_event_t *e) {
    auto *self = static_cast<MusicCard *>(lv_event_get_user_data(e));
    if (self->view_ == View::PLAYING) self->paint();
  }
  static void wheel_turned(lv_event_t *e) {
    auto *self = static_cast<MusicCard *>(lv_event_get_user_data(e));
    if (!self->wheel_ || self->view_ != View::ITEMS) return;
    if (self->more_ && !self->loading_ && self->chosen() >= self->total_ - 4) self->ask();
    self->paint_play();
  }
#endif

  // A page of the library, through the app: music_assistant.get_library of the tile's Music Assistant entry.
  void ask() {
    Players *tile = plugin_->players_at(tile_);
    if (!wheel_) count_ = 0;
    failed_ = false;
    more_ = false;
    if (!tile || !tile->library()[0]) { failed_ = true; return; }
    // What was typed, without a space at its end.
    std::string q = query_;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    searching_ = wheel_ && !q.empty();
    JsonDocument request;
    JsonObject data = request["data"].to<JsonObject>();
    if (opened_ == ALBUM) {
      // An album's songs, as Home Assistant browses them on the tile's own Music Assistant player.
      searching_ = true;
      request["ask"] = "media_player/browse_media";
      data["entity_id"] = tile->room(0).id;
      data["media_content_id"] = open_uri_;
      data["media_content_type"] = "album";
      asked_ = tessera::send(plugin_, request.as<JsonObjectConst>());
      loading_ = asked_ != 0;
      failed_ = asked_ == 0;
      return;
    }
    data["config_entry_id"] = tile->library();
    if (opened_ == ARTIST) {
      // An artist's albums: Music Assistant's search by the artist's name, in the library and its providers.
      searching_ = true;
      request["ask"] = "call_service:music_assistant.search";
      data["name"] = open_artist_;
      data["artist"] = open_artist_;
      data["media_type"].to<JsonArray>().add("album");
      data["limit"] = 12;
      data["library_only"] = false;
    } else if (searching_) {
      // A search asks Music Assistant's providers too (Spotify, TuneIn, ...), not only the library: the best matches
      // of the kind, as many as fit an answer whole.
      request["ask"] = "call_service:music_assistant.search";
      data["name"] = q;
      JsonArray types = data["media_type"].to<JsonArray>();
      if (everywhere_) for (int k : EVERYTHING) types.add(KINDS[k].type);
      else types.add(KINDS[kind_].type);
      // A few of each kind of all music; a kind alone, more.
      data["limit"] = everywhere_ ? 3 : KINDS[kind_].page * 2;
      data["library_only"] = false;
    } else {
      request["ask"] = "call_service:music_assistant.get_library";
      data["media_type"] = KINDS[kind_].type;
      if (favourites_) data["favorite"] = true;
      data["limit"] = wheel_ ? KINDS[kind_].page : page_size();
      data["offset"] = wheel_ ? total_ : offset_;
      data["order_by"] = "sort_name";
    }
    asked_ = tessera::send(plugin_, request.as<JsonObjectConst>());
    loading_ = asked_ != 0;
    failed_ = asked_ == 0;
  }

  MusicRooms *plugin_;
  lv_obj_t *parent_{};
  int row_h_ = 0;
  int width_ = 0, height_ = 0, tile_ = -1, gap_ = 0, key_ = 0, round_ = 0, cover_x_ = 0, rows_ = 0, shifted_ = 0;
  lv_obj_t *head_{}, *head_icon_{}, *head_text_{}, *head_more_{}, *library_{}, *library_text_{};
  static constexpr int KEYS_NOW = 8;
  lv_obj_t *cover_{}, *title_{}, *artist_{}, *volume_{}, *keys_[KEYS_NOW] = {}, *key_icon_[KEYS_NOW] = {};
  lv_obj_t *bar_{}, *bar_fill_{}, *time_at_{}, *time_all_{};
  int bar_w_ = 1, bar_shown_ = -1;
  std::string address_;
  bool missing_ = false;
  // The volume of the room in the middle of Playing's wheel.
  lv_obj_t *room_down_{}, *room_down_icon_{}, *room_up_{}, *room_up_icon_{}, *room_volume_{};
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
  KindKey kind_key_[KIND_KEYS];
  int kind_entry_[KIND_KEYS] = {};
  Shelf *shelf_ = nullptr;
  lv_obj_t *wheel_{}, *play_{}, *play_icon_{}, *play_text_{}, *find_{}, *find_icon_{};
  lv_obj_t *field_{}, *field_text_{}, *go_{}, *go_text_{};
  TypeKey type_[KEYS];
  char query_[33] = "";
  bool searching_ = false, everywhere_ = false;
  int total_ = 0, play_w_ = 0, find_w_ = 0, open_w_ = 0;
  lv_obj_t *open_{}, *open_text_{}, *everywhere_key_{}, *everywhere_text_{};
  lv_obj_t *rooms_wheel_{}, *take_{}, *take_text_{}, *move_{}, *move_text_{};
  int take_w_ = 0, move_w_ = 0, playing_slots_[SLOTS] = {};
  bool start_playing_ = false;
  std::string playing_lines_;
  uint8_t opened_ = CLOSED;
  bool from_artist_ = false;
  char open_artist_[49] = "", open_album_[49] = "", open_uri_[49] = "";
};

// ---- A popup: a room that cannot play together with what plays ----
class CannotJoin : public tessera::Popup {
 public:
  CannotJoin(MusicRooms *plugin, const char *room, const char *lead, bool moved) : plugin_(plugin), moved_(moved) {
    put(room_, room);
    put(lead_, lead);
  }
  void open(const tessera::PopupContext &c) override {
    const int gap = ui::px(10);
    icon_ = ui::label(c.parent, Font::ICON, theme::MUTED);
    ui::set_text(icon_, ui::icon(ICON_SPEAKER));
    lv_obj_set_pos(icon_, 0, 0);
    title_ = ui::text(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, std::max(1, c.width - c.key - gap - c.corner));
    lv_obj_set_pos(title_, c.key + gap, 0);
    ui::set_text(title_, tessera::fill(plugin_->text(moved_ ? "moved_title" : "cannot_join_title"), "room", room_));
    body_ = ui::text(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_width(body_, c.width);
    lv_obj_set_pos(body_, 0, std::max(c.key, ui::line_height(Font::TITLE) * 2) + gap);
    ui::set_text(body_, tessera::fill(tessera::fill(plugin_->text(moved_ ? "moved_body" : "cannot_join_body"), "room", room_).c_str(), "lead", lead_));
  }
  void on_tap() override { tessera::close_popup(plugin_); }

 private:
  MusicRooms *plugin_;
  bool moved_;
  char room_[49] = "", lead_[49] = "";
  lv_obj_t *icon_{}, *title_{}, *body_{};
};

void MusicRooms::explain(const char *room, const char *lead, bool moved) {
  std::string r = room, l = lead;
  tessera::show_popup(this, [this, r, l, moved]() { return new CannotJoin(this, r.c_str(), l.c_str(), moved); });
}

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
  // A room's queue first (Players::ask), else the card's library.
  for (Entry &e : entries_)
    if (e.players && e.players->take(message)) return;
  if (card_) card_->answer(message);
}

}  // namespace esphome::music_rooms
