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
3. Under **Room 1** to **Room 10**, choose the other Music Assistant players the tile may play in: speakers, or Music
   Assistant groups (a group is one player that plays on all its speakers in sync).
4. Under **Music Assistant entry**, fill in the ID of your Music Assistant integration. In Home Assistant open
   Settings, Devices & services, Music Assistant, and open its entry: the ID is the last part of the page's address.
5. Optional: under **Source**, what the Library key opens first: all kinds, Favourites, or one kind such as Radio.

## On the screen

- A tap on the tile opens its card on **Now playing**: the rooms, what plays there and who, the cover of the tile's
  own player, play and pause, previous, next and the volume.
- **Playing**, beside Library, lists every room of the tile with what it plays, those that play first. A tap on a room
  takes it over: Now playing and its keys are that room's and the next item plays there, while the other rooms go on
  with their own music. Each room keeps its own queue in Music Assistant, so one room can play one Spotify account,
  another the other account and a third the radio.
- The rooms key at the top opens **Rooms**: a tap adds a room or takes it out, and **All rooms** picks them all. A room Home Assistant cannot reach is marked
  Unavailable.
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
  room that cannot be joined (a screen's own speaker) waits for the next item. With nothing playing, the ticks wait
  for the next item. Picking a room never stops what plays.
- In one room: Home Assistant's `media_player.play_media` on that player, with the item's own Music Assistant address
  and kind, also while another room plays something else.
- In several rooms: the other rooms that can play in step (Home Assistant's grouping: a Cast, a Sonos) join the first
  one first (`media_player.join`), which then plays the item for all of them; a room that cannot (a screen's own
  speaker, a Music Assistant group) gets the item on its own. Rooms this tile put together before and that are no longer ticked leave the group first
  (`media_player.unjoin`), so it plays in exactly the rooms ticked.
- The keys of Now playing act on the first room, which leads a group.

## Good to know

- The library comes from Music Assistant's own `music_assistant.get_library`, four to six items at a time; a search
  from `music_assistant.search`, the library and the providers together (a dozen of one kind, or three of each).
- The Tessera app gives a plugin at most 48 characters of a text, and a Spotify artist's or playlist's address is
  longer: such an item plays by its name (and artist) through `music_assistant.play_media`, which finds it again.
- An item whose Music Assistant address is longer than 48 characters shows faded and plays nothing: the Tessera app
  keeps no longer text for a plugin.
- The cover on Now playing is the tile's own player's. Another room shows what plays and who, without a cover.
- Up to ten rooms beside the tile's own player. The tile's own player is where it plays until you pick another.
- A Music Assistant group ticked together with other rooms is not joined to them: each gets the item on its own, so
  they play the same thing but not exactly in step. A group ticked alone plays in sync as always.
