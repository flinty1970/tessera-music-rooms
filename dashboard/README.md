# Music rooms dashboard

A Home Assistant dashboard page in the look of the Music rooms screens: Now playing (cover, song, artist and album, the
song's bar, the keys), every room with what it plays, its volume and Add/Remove, the rooms that play together marked
with one colour, and the library and search of Music Assistant (also Spotify) that plays in the room the page controls.

## Set up

1. Install `button-card` from HACS (the page is built of it).
2. Add `tessera-music-library.js` as a dashboard resource (Settings, Dashboards, ⋮, Resources, Add resource,
   JavaScript module), or copy it to `/config/www/` and add `/local/tessera-music-library.js`.
3. Add a dropdown helper `input_select.music_dashboard_room` whose options are your Music Assistant players' entity
   ids: the room the page controls.
4. Add the script in `music_dashboard_toggle_room.yaml` with the id `music_dashboard_toggle_room` (Add and Remove).
5. Build the page of: one `custom:button-card` Now playing card and its keys, a row per room (the room, Add/Remove,
   volume down and up), and the library:

   ```yaml
   type: custom:tessera-music-library
   entry: <your Music Assistant config entry id>
   room: input_select.music_dashboard_room
   ```

   The button-card templates (`tn_music_now`, `tn_music_key`, `tn_music_room`, `tn_music_join`, `tn_music_down`,
   `tn_music_up`) live under `button_card_templates` in the dashboard's raw configuration.

## Using it

- Tap a room to control it: Now playing, its keys and the library then act on that room.
- **Add** (green) joins a room to the controlled room's group, **Remove** (red) takes it out; the controlled room shows
  **Controlling** (blue). The lead of a group has Remove too: the music then goes on in the rooms left.
- While Whole House (a Music Assistant group) plays, its rooms say Remove and others Add; the dashboard treats Whole
  House as the room in charge even when you control one of its rooms.
- A room that cannot play in step with the controlled room (a screen's own speaker, on either side) says **Move here**
  (amber): a tap moves the music to it (`music_assistant.transfer_queue`) and the dashboard then controls it.
- Rooms playing together share a coloured stripe at their left, and say "With <the lead>".
- In the library, a tap on a tile plays it; › opens an artist (their albums) or an album (its songs).
