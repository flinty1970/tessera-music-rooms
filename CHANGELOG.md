# Changelog

## 0.8.0

- **Open** an artist to see their albums, and an album to see its songs, beside Play and Search; Play plays the one
  in the middle (the whole artist, the whole album, or one song). Back goes up a step.
- **Ten rooms** beside the tile's own player. Rooms at once gave its place to the tenth (a tile has at most twelve
  settings): every tile now plays in the rooms you tick, one or several; a tap on All rooms ticks them all.
- **What plays leads**: Next, Play/Pause and the volume act on the room that plays, and a room ticked joins it, whatever
  their places in the list. Before, the first room of the list led, so Next could start that room's own old queue.
- Playing in another room, the tile and Now playing show what plays across their whole width instead of beside an
  empty frame: the Tessera app shows a tile only its own player's cover.

## 0.7.0

- **A room ticked while music plays joins it at once**, in step (a Cast, a Google display, the stereo), and one
  unticked leaves. Before, the ticks waited for the next item.
- A room that cannot play in step with the others (a screen's own speaker) gets the item on its own, where before it
  stayed silent.

## 0.6.0

- **Search finds music outside your library**: Spotify and Music Assistant's other providers, not only what you
  added. An artist you never added (a band you just heard of) now comes up.
- **Search all** in the Library's grid looks for artists, songs, albums and playlists at once; the wheel says which
  each one is ("Artist: …", "Song: … · the artist").
- An item whose address is too long for the Tessera app (a Spotify artist's, a playlist's) plays by its name through
  Music Assistant, which finds it, instead of showing grey.

## 0.5.0

- **Search**: a key beside Play opens a keyboard; type part of a name and the wheel shows only those artists, songs,
  albums or playlists of your Music Assistant library.
- The wheel has more room: where it plays, Search and Play now share one row at the top.
- The wheel also on small screens such as the CYD.

## 0.4.0

- The library's items turn on a wheel, as the alarm clock's time does: swipe through them, and the large Play key
  under the wheel plays the one in the middle (with its artist, where it has one). More load as you reach the end.
- The kinds (Favourites, Playlists, Radio, ...) are a grid of large keys, all on one page.
- A small screen without room for the wheel keeps the list, which no longer runs into the page keys.

## 0.3.1

- Fixed: on a screen with many pages, the tile showed "Choose a player" although its player was set.

## 0.3.0
- **Nine rooms** beside the tile's own player, and Music Assistant **groups** as rooms: a group plays on all its
  speakers in sync. Ticked with other rooms, a group gets the item on its own beside them.
- **Open library on** is now called **Source**.
- **Default player** and **Play when idle** are gone, to make room for nine rooms: the tile's own player is where it
  plays until you pick another. A tile that had them set loses those two settings.

## 0.2.0
- **Default player**: the player picked when the screen starts.
- **Open library on**: the Library key opens all kinds, the favourites or one kind, such as Radio, first.
- **Play when idle**: the play key starts a playlist or station you name on a player that has nothing to play.
- Six rooms beside the tile's own player instead of eight: a tile has room for twelve settings. A tile that had a
  Room 7 or Room 8 loses them; set them again as one of the first six.

## 0.1.0
- First version: a Music Assistant player's tile with the rooms to play in (one, several or all), the Music Assistant
  library a page at a time, and Now playing with play and pause, skip and volume.
