# Music rooms

A tile for a Music Assistant player that lets you choose the room or rooms to play in, browse the Music Assistant
library and play what you tap there, with Now playing, play and pause, skip and volume on the same tile.

## Screens

Music rooms needs a screen of **4 inches or more**: the Guition 4" square (ESP32-S3-4848S040) and the Waveshare 4"
squares, every 4.3" board (Waveshare 4.3 and 4.3C, Guition JC4880P443), and every 5", 7", 8" and 10.1" board. The CYD
(2.8"), the 3.5" boards (Waveshare 3.5, Guition JC3248W535, WT32-SC01 Plus), the narrow Hosyond 4" and the Freenove
FNK0104S are too small for its wheel, keyboard and keys, and the Tessera app does not offer it for them.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Music rooms** tile from the library's Plugins group, and choose its player: a Music Assistant
   player. It is the room the tile plays in until you pick another.
3. Under **Room 1** to **Room 11**, choose the other Music Assistant players the tile may play in: speakers, or Music
   Assistant groups (a group is one player that plays on all its speakers in sync).
4. The **Music Assistant entry** is asked once, when you add the plugin: the ID of your Music Assistant integration. In Home Assistant open
   Settings, Devices & services, Music Assistant, and open its entry: the ID is the last part of the page's address.
5. Optional: under **Source**, what the Library key opens first: all kinds, Favourites, or one kind such as Radio.
6. Optional, for the covers of every room (the tile's own player's shows without this): in Home Assistant add a
   Template image helper for each Music Assistant player (Settings, Devices & services, Helpers, Create helper,
   Template, Image), named `Music cover <player>` so its entity is `image.music_cover_<the player's object id>`
   (`image.music_cover_stereo_3` for `media_player.stereo_3`), with the URL template
   `{{ state_attr('media_player.stereo_3', 'entity_picture') }}` and Verify SSL off.
7. Optional, so Spotify playlists and artists play exactly: add the script in `scripts/music_rooms_play.yaml` to Home
   Assistant with the id `music_rooms_play`, and give the screen `play_script: script.music_rooms_play` under
   `music_rooms:` in its ESPHome configuration. Without it such an item plays by its name.
8. Recommended, so a room joins a group whatever room of it the tile controls: add the script in
   `scripts/music_rooms_join.yaml` with the id `music_rooms_join`, and give the screen
   `join_script: script.music_rooms_join` under `music_rooms:`. Without it a room ticked while the controlled room is a
   member of another room's group is not joined (Music Assistant ignores a join to a member).

## Home Assistant scripts

Two optional scripts in `scripts/` make the screens do what the Tessera app alone cannot. Add each in Home Assistant
(Settings, Automations & scenes, Scripts, Add script, ⋮, Edit in YAML, paste the file), with the id in the file name,
then name it under `music_rooms:` in the screen's ESPHome configuration:

```yaml
music_rooms:
  library: <your Music Assistant config entry id>
  play_script: script.music_rooms_play
  join_script: script.music_rooms_join
```

| Script | What it does | Without it |
|---|---|---|
| `music_rooms_play` (`scripts/music_rooms_play.yaml`) | Plays a Spotify playlist or artist exactly: the app gives a plugin 48 characters of a text, so the screen sends the start of the address, the name and the kind, and the script plays the one search result whose address starts so. | Such an item plays by its name, which another item may share. |
| `music_rooms_join` (`scripts/music_rooms_join.yaml`) | Joins the rooms you tick to the lead of the group the controlled room is in (the first of its `group_members`): Music Assistant ignores a join to a room that is a member of another room's group, and the screen cannot see the groups. A Music Assistant group player (Whole House) ticked while music plays takes the music over, and so does a room that cannot play in step with the one that plays (`music_assistant.transfer_queue`). | A room ticked while the controlled room is a member of another room's group is not joined, and a Music Assistant group (Whole House) ticked while music plays waits for the next item. |

Both run in Home Assistant with the screen's own permission to perform actions (`script.turn_on`). A screen must be
allowed to perform Home Assistant actions (Settings, Devices & services, ESPHome, the screen, Configure).

## Dashboard

`dashboard/` holds a Home Assistant dashboard page in the same look: Now playing, every room with Add/Remove and its
volume, the groups in colour, and the library and search. See `dashboard/README.md`.

## On the screen

- A tap on the tile opens its card on **Now playing**: the rooms, what plays there, the artist and the album, the
  song's bar (how far, how long), shuffle and repeat, the cover of the tile's
  own player, play and pause, previous, next and the volume. A tap on the volume number switches its keys between
  the room they act on and all the rooms it plays in ("All 35 %"), each from its own level.
- **Playing**, beside Library, lists every room of the tile with what it plays, those that play first. A tap on a room
  takes it over: Now playing and its keys are that room's and the next item plays there, while the other rooms go on
  with their own music. **Move here**, beside it, moves what plays (the room you control, its group, or Whole House) to
  the room in the middle, alone: from Whole House to the Google Home in one tap (with the join script). Under the
  wheel, the volume of the room in the middle, each room its own. Each room keeps its own queue in Music Assistant, so one room can play one Spotify account,
  another the other account and a third the radio.
- The rooms key at the top opens **Rooms**: a tap adds a room or takes it out. For every speaker at once, use a Music
  Assistant group such as Whole House. A room Home Assistant cannot reach is marked Unavailable.
- Only rooms that can play in step are grouped. While a room plays, a room that cannot play in step with it (a
  screen's own speaker, on either side) says **Moves here**: a tap moves the music to it (with the join script,
  `music_assistant.transfer_queue`) and a popup says why it moved instead of joining; the keys then act on that room.
  From the Office screen's speaker to the Stereo and back, say. Without the join script such a room is grey,
  **Can't join**. A Music Assistant group (Whole House) can always take the music over. While Whole House plays and
  the keys act on it, Rooms ticks its speakers that play: a tap takes one out of it or puts one back in.
- **Library** shows Favourites, Playlists, Radio, Albums, Artists, Tracks and Podcasts as large keys. A kind's items
  turn on a wheel, like the alarm clock's time: swipe it, and **Play** at the top plays the one in the middle, in the
  rooms shown beside it. More come as you near the end. Now playing then says Starting and then what plays, or Did
  not start.
- **Search** beside Play opens a keyboard: type part of a name and tap Search, and the wheel shows the best matches
  of that kind, from your library and from Music Assistant's providers such as Spotify. **Search all**, the last key
  of the Library's grid, looks for artists, songs, albums and playlists at once.
- **Open**, beside Search, opens the artist in the middle of the wheel (their albums) or the album (its songs, the
  first eight or so: the Tessera app keeps an answer to about 3 KB). Play plays the whole album too.
- The card's Back key goes up one step: from a search to the whole list, from the items to the kinds, from the kinds
  to Now playing, then closes.

## How it plays

- With Several, a room ticked while the first room plays joins it at once, in step, and a room unticked leaves; a
  room that cannot be joined (a screen's own speaker) gets the music moved to it instead. With nothing playing, the ticks wait
  for the next item. Picking a room never stops what plays.
- In one room: Home Assistant's `media_player.play_media` on that player, with the item's own Music Assistant address
  and kind, also while another room plays something else.
- In several rooms: the other rooms that can play in step (Home Assistant's grouping: a Cast, a Sonos) join the first
  one first (`media_player.join`), which then plays the item for all of them; a room that cannot (a screen's own
  speaker, a Music Assistant group) gets the item on its own. Rooms this tile put together before and that are no longer ticked leave the group first
  (`media_player.unjoin`), so it plays in exactly the rooms ticked.
- The keys of Now playing act on the first room, which leads a group.

## The group it follows

One template sensor in Home Assistant tells the screens what plays and which speakers can join or leave it. It finds
your speaker groups by itself: every Music Assistant player of type group (a group made in the Google Home app shows
up as one), and as speakers every player those groups list. Make a group in the Google Home app, and once Music
Assistant shows it the screens follow it, with no change here and no new build.

With it, a group that is not one of a tile's rooms (Living + Stereo, Everywhere) still shows on the screen: its
speakers get the music note, and the card's keys act on the group. And Add and Remove appear only where what then
plays is a group (or one speaker). Make it in Settings, Devices & services, Helpers, Template, Sensor, with this
state, as it is:

```
{%- set G = states.media_player | selectattr('attributes.mass_player_type', 'defined')
    | selectattr('attributes.mass_player_type', 'eq', 'group') | selectattr('attributes.group_members', 'defined')
    | rejectattr('state', 'in', ['unavailable', 'unknown']) | map(attribute='entity_id') | list -%}
{%- set busy = ['playing', 'paused', 'buffering'] -%}
{%- set ns = namespace(lead='', members=[], sets=[], add=[], rm=[], S=[]) -%}
{%- for g in G -%}{%- for m in state_attr(g, 'group_members') or [] -%}
  {%- if m not in ns.S -%}{%- set ns.S = ns.S + [m] -%}{%- endif -%}{%- endfor -%}{%- endfor -%}
{%- for g in G -%}{%- if not ns.lead and states(g) in busy -%}
  {%- set ns.lead = g -%}{%- set ns.members = state_attr(g, 'group_members') or [] -%}{%- endif -%}{%- endfor -%}
{%- for s in ns.S -%}{%- if not ns.lead and states(s) in busy -%}
  {%- set ns.lead = s -%}{%- set ns.members = [s] -%}{%- endif -%}{%- endfor -%}
{%- if not ns.lead -%}none{%- else -%}
{%- for g in G -%}{%- set ns.sets = ns.sets + [(state_attr(g, 'group_members') or []) | sort | join(',')] -%}{%- endfor -%}
{%- for s in ns.S -%}{%- set t = s.split('.')[1][:6] -%}
  {%- if s in ns.members -%}{%- set rest = ns.members | reject('eq', s) | list -%}
    {%- if rest | length == 1 or (rest | sort | join(',')) in ns.sets -%}{%- set ns.rm = ns.rm + [t] -%}{%- endif -%}
  {%- elif states(s) not in ['unavailable', 'unknown'] -%}
    {%- if ((ns.members + [s]) | sort | join(',')) in ns.sets -%}{%- set ns.add = ns.add + [t] -%}{%- endif -%}
  {%- endif -%}{%- endfor -%}
{{ ns.lead }}|{{ state_attr(ns.lead, 'active_queue') }}|{{ states(ns.lead) }}|{{ state_attr(ns.lead, 'friendly_name') }}|{{ ns.add | join(',') }}|{{ ns.rm | join(',') }}
{%- endif -%}
```

Then pick that sensor as Group it follows in each Music tile. A speaker is named by the first six letters of its
object id, so keep those different among your grouped speakers. Rooms you want as rows on a screen are still chosen
per tile in the Tessera app, without a new build; for a group's cover, make its image entity as for every room.

## Good to know

- The library comes from Music Assistant's own `music_assistant.get_library`, four to six items at a time; a search
  from `music_assistant.search`, the library and the providers together (a dozen of one kind, or three of each).
- The Tessera app gives a plugin at most 48 characters of a text, and a Spotify artist's or playlist's address is
  longer: with the play script (Set up, step 7) Home Assistant finds the one item whose address starts so; without it
  such an item plays by its name (and artist) through `music_assistant.play_media`.
- A tile's data is about 2.6 KB for its own player, ten rooms and the group it follows together, so a room sends only its state, name
  and volume: what plays there, how far and shuffle and repeat come from its Music Assistant queue
  (`music_assistant.get_queue`), asked by the screen while the room plays.
- An item whose Music Assistant address is longer than 48 characters shows faded and plays nothing: the Tessera app
  keeps no longer text for a plugin.
- The cover of the tile's own player always shows; another room's shows through its image entity (Set up, step 6).
- Up to ten rooms beside the tile's own player. The tile's own player is where it plays until you pick another.
- A Music Assistant group ticked together with other rooms is not joined to them: each gets the item on its own, so
  they play the same thing but not exactly in step. A group ticked alone plays in sync as always.
- A Spotify account that allows one stream at a time cannot play the same thing on two rooms apart: the second stops
  the first. That is why a room that cannot group gets the music moved to it, not a copy.
