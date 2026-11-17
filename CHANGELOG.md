# Changelog

## 0.13.10

- Whole House means all its rooms: unticking one of its speakers unticks Whole House too, and the other speakers go on
  playing as a group of their own. Tick Whole House again to bring every room back.
- While Whole House plays, any room that is not one of its speakers says "Moves here": a tap moves the music there alone.
- Only Cast speakers group: AirPlay speakers and DLNA players (a TV) say "Moves here" and take the music alone, as Music
  Assistant cannot keep them in step with Cast.
- While Whole House plays, it and its speakers are ticked in green on Rooms, other rooms in the accent colour.
- The volume's rooms (tap the %) are every room playing the same song, also before the screen has learned the group.
- Tapping Whole House while music plays elsewhere no longer shows the "can't play in step" popup meant for a screen's
  own speaker.

## 0.13.9

- Tapping Whole House while music plays in another room moves the music to Whole House (all its rooms), instead of
  only choosing it for the next song.
- Untick Whole House while it plays: the music goes on in the speakers that were playing in it, as a group of their
  own. Tick Whole House again and every room of it comes back.
- On Rooms, tap "Plays in Kitchen + 2" at the top: a popup lists every room it plays in.

## 0.13.8

- Rooms shows the real group: a speaker that plays in the same group as the room in charge (a Cast group, joined
  here, on the tablet or in the Google Home app) is ticked, and unticked it leaves. Unticking the room in charge keeps
  the others chosen, so "Moves here" stays on the rooms that cannot join.
- The volume of one room shows its name over its level in smaller letters, so more of the name fits.

## 0.13.7

- Volume: tap the % between the volume keys to go through all the rooms it plays in ("All 30 %"), then each room on
  its own ("Stereo 34 %"), then all again. The keys change what is shown; all of them each from its own level.
- While Whole House plays, a room that cannot join it (a screen's own speaker) says "Moves here": a tap moves the
  music there alone, as with other groups.
- From Whole House down to one room: on Rooms, tap Whole House to untick it, then tap the one room to play in. The
  music moves there alone. Tap Whole House again to keep it as it was.

## 0.13.6

- A speaker that plays in Whole House (or another Music Assistant group) now stands for the group: the card says
  "Plays in Whole House", Rooms ticks Whole House and its playing speakers, and the keys act on the group.
- Other rooms' songs show the artist without the album.

## 0.13.5

- **All rooms** is gone from Rooms: it overlapped Whole House. For every speaker at once, play on a Music Assistant
  group such as Whole House.
- **Whole House stands alone**: ticking it unticks the other rooms (it already holds its speakers), and ticking a
  speaker unticks it. Playing on Whole House no longer says "Did not start" because another ticked room stayed idle.

## 0.13.4

- While Whole House (a Music Assistant group) plays and the keys act on it, Rooms ticks the speakers that play in it:
  a tap takes one out, or puts one back in. Needs the updated join script.

## 0.13.3

- Unticking a room goes through the join script, which knows the real lead of the group: unticking the lead leaves
  the music playing in the rooms still ticked (it stayed on the lead). Needs the updated join script.

## 0.13.2

- A tap on a room that says "Moves here" always moves the music, also when that room was ticked before (it
  showed the popup and moved nothing).

## 0.13.1

- **Move here** in Playing, beside Control: what plays (the room you control, its group, or Whole House) moves to the
  room in the middle of the wheel, alone, and the keys then act on it. From Whole House to the Google Home in one tap.
  Needs the updated join script (`scripts/music_rooms_join.yaml`).
- "Did not start" no longer stays on Now playing when the room starts late: it goes as soon as the room plays, and
  waits 30 seconds instead of 15.

## 0.13.0

- **Switching between a screen's speaker and another room works**: a room that cannot play in step with the one that
  plays (a screen's own speaker, on either side) says "Moves here", and a tap moves the music to it, with a popup that
  says why. The keys then act on that room. Needs the updated join script (`scripts/music_rooms_join.yaml`).
- Without the join script such a room is greyed ("Can't join"), and a tap says why.
- A tap on a room while nothing plays in a speaker that cannot be grouped switches to that room. **All rooms** ticks
  only the rooms that can play together.
- Needs Tessera's plugin API 0.18 (popups).

## 0.12.5

- With the join script, every room ticked while music plays goes to Home Assistant, which knows the groups: a Cast
  room joins (also to Whole House's music), a room already inside the playing Music Assistant group stays as it is,
  and a room that cannot join (a screen's own speaker) gets the same song started on it, not in exact step.

## 0.12.4

- **Whole House (a Music Assistant group) ticked while music plays** now takes the music over (Music Assistant's
  transfer of the queue) and the card holds it, instead of waiting for the next item. Needs the join script, updated
  in `scripts/music_rooms_join.yaml`.

## 0.12.3

- **This room or all rooms**: a tap on the volume between − and + switches the keys between the room they act on and
  all the rooms it plays in ("All 35 %"), each stepped from its own level so their balance stays.

## 0.12.2

- Rooms join a group through its lead, also when the room the screen controls is a member of another room's group
  (Music Assistant ignored those joins). Needs the join script (`scripts/music_rooms_join.yaml`) and
  `join_script: script.music_rooms_join` under `music_rooms:`.

## 0.12.1

- A room unticked while music plays now always leaves the group, also when it was joined from another screen, the
  dashboard, or before the screen started again.
- Fewer questions to Music Assistant (the room the keys act on every 10 seconds at most, another every 30), so a
  screen that plays music on its own speaker keeps more of its Wi-Fi for the music.

## 0.12.0

- **The song's bar** on Now playing: how far it is and how long, moving while it plays.
- **Shuffle and repeat** keys beside next (repeat all, one, off).
- **Each room's own volume** under Playing's wheel, for the room in the middle, also while several play together.
- **Covers of every room**, not only the tile's own player: each player's cover through an image entity in Home
  Assistant (see Set up, step 6).
- **Spotify playlists and artists play exactly** with the play script (Set up, step 7), not by a name another one may
  share.
- Rooms now send only their state, name and volume; what plays there comes from Music Assistant's queue. Eleven rooms
  with long names fit the tile's data again, so a room no longer loses its details (and its "can join") to the limit.

## 0.11.0

- **The album** beside the artist, under what plays: on the tile and on Now playing ("The Reytons · What's Rock and
  Roll?"). A long one ends in dots.

## 0.10.0

- **Eleven rooms** beside the tile's own player. The Music Assistant entry gave its place on the tile: it is asked once
  when the plugin is added, for every tile of every screen (a tile that named its own keeps it).
- The cover on more tiles: a 3 x 2 tile of a square screen shows it too, at most two fifths of the tile wide.
- An idle player shows Nothing playing alone: no longer the artist and cover of what it played last.

## 0.9.0

- **Playing**, beside Library on Now playing: every room of the tile with what it plays and who, the rooms that
  play first. A tap on a room takes it over: Now playing, its keys and the next item are that room's, while every
  other room goes on with its own music (another Spotify account, the radio). Tick rooms under the rooms key to add
  speakers to it.

## 0.8.0

- For screens of 4 inches or more only (see Screens in the README): the CYD and the 3.5" boards are too small.

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
