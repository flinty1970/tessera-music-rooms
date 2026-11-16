# Music rooms

A tile for a Music Assistant player that lets you choose the room or rooms to play in, browse the Music Assistant
library and play what you tap there, with Now playing, play and pause, skip and volume on the same tile.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Music rooms** tile from the library's Plugins group, and choose its player: a Music Assistant
   player. It is the room the tile plays in until you pick another.
3. Under **Room 1** to **Room 9**, choose the other Music Assistant players the tile may play in: speakers, or Music
   Assistant groups (a group is one player that plays on all its speakers in sync).
4. Under **Rooms at once**, choose **One** (the next item plays in the room you pick) or **Several** (tick rooms, or
   All rooms, and it plays in all of them together).
5. Under **Music Assistant entry**, fill in the ID of your Music Assistant integration. In Home Assistant open
   Settings, Devices & services, Music Assistant, and open its entry: the ID is the last part of the page's address.
6. Optional: under **Source**, what the Library key opens first: all kinds, Favourites, or one kind such as Radio.

## On the screen

- A tap on the tile opens its card on **Now playing**: the rooms, what plays there and who, the cover of the tile's
  own player, play and pause, previous, next and the volume.
- The rooms key at the top opens **Rooms**. With One, a tap on a room makes it the room and goes back. With Several,
  a tap adds a room or takes it out, and **All rooms** picks them all. A room Home Assistant cannot reach is marked
  Unavailable.
- **Library** shows Favourites, Playlists, Radio, Albums, Artists, Tracks and Podcasts as large keys. A kind's items
  turn on a wheel, like the alarm clock's time: swipe it, and **Play** at the top plays the one in the middle, in the
  rooms shown beside it. More come as you near the end. Now playing then says Starting and then what plays, or Did
  not start.
- **Search** beside Play opens a keyboard: type part of a name and tap Search, and the wheel shows only the items of
  that kind whose name holds it (artists in Artists, songs in Tracks).
- The card's Back key goes up one step: from a search to the whole list, from the items to the kinds, from the kinds
  to Now playing, then closes.

## How it plays

- Picking a room never moves or stops what already plays anywhere. Only a tap on an item plays.
- In one room: Home Assistant's `media_player.play_media` on that player, with the item's own Music Assistant address
  and kind, also while another room plays something else.
- In several rooms: the other rooms join the first one first (`media_player.join`), which then plays the item for all
  of them. Rooms this tile put together before and that are no longer ticked leave the group first
  (`media_player.unjoin`), so it plays in exactly the rooms ticked.
- The keys of Now playing act on the first room, which leads a group.

## Good to know

- The library comes from Music Assistant's own `music_assistant.get_library`, four to six items at a time, and a
  search uses its `search` field. It finds what is in your Music Assistant library, not everything a provider such as
  Spotify has: add an artist or a song to the library in Music Assistant first.
- An item whose Music Assistant address is longer than 48 characters shows faded and plays nothing: the Tessera app
  keeps no longer text for a plugin.
- The cover on Now playing is the tile's own player's. Another room shows what plays and who, without a cover.
- Up to nine rooms beside the tile's own player. The tile's own player is where it plays until you pick another.
- A Music Assistant group ticked together with other rooms is not joined to them: each gets the item on its own, so
  they play the same thing but not exactly in step. A group ticked alone plays in sync as always.
