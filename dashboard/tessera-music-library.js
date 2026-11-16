// Tessera music library: a Home Assistant dashboard card in the look of the Music rooms screens.
// Kinds, a search box and tiles of Music Assistant's library and its providers (Spotify and the rest); a tap plays the
// item in the room the page controls (an input_select of media players), an artist or an album opens with ›.
// It also lays the cover of what plays there, blurred, behind the whole page.
//
//   type: custom:tessera-music-library
//   entry: <Music Assistant config entry id>
//   room: input_select.music_dashboard_room
//   backdrop: false   (true lays the cover, washed, behind the page)
const KINDS = [
  { id: 'favourites', word: 'Favourites', icon: 'mdi:heart', type: null },
  { id: 'playlist', word: 'Playlists', icon: 'mdi:playlist-music', type: 'playlist', list: 'playlists' },
  { id: 'artist', word: 'Artists', icon: 'mdi:account-music', type: 'artist', list: 'artists' },
  { id: 'album', word: 'Albums', icon: 'mdi:album', type: 'album', list: 'albums' },
  { id: 'track', word: 'Tracks', icon: 'mdi:music-note', type: 'track', list: 'tracks' },
  { id: 'radio', word: 'Radio', icon: 'mdi:radio', type: 'radio', list: 'radio' },
];
const ACCENT = '#0B96D6';

class TesseraMusicLibrary extends HTMLElement {
  setConfig(config) {
    if (!config.entry) throw new Error('entry: the Music Assistant config entry id');
    this._config = { room: 'input_select.music_dashboard_room', backdrop: false, ...config };
    this._kind = 'playlist';
    this._query = '';
    this._items = [];
    this._title = '';
    this._busy = false;
    if (!this.shadowRoot) this.attachShadow({ mode: 'open' });
    this._build();
  }

  set hass(hass) {
    const first = !this._hass;
    this._hass = hass;
    if (first) this._load();
    if (this._config.backdrop) this._paintBackdrop();
  }

  getCardSize() { return 8; }
  getGridOptions() { return { columns: 'full', rows: 'auto' }; }

  connectedCallback() { if (this._hass && this._config.backdrop) this._paintBackdrop(); }
  disconnectedCallback() { this._setBackdrop(null); }

  // ---- What plays: the room the page controls and its cover ----
  _room() { return this._hass && this._hass.states[this._config.room] ? this._hass.states[this._config.room].state : ''; }

  _paintBackdrop() {
    const p = this._hass.states[this._room()];
    const on = p && ['playing', 'paused', 'buffering'].includes(p.state) && p.attributes.media_title;
    this._setBackdrop(on ? p.attributes.entity_picture : null);
  }

  // The view's own background element, found through the shadow roots, takes the cover; the theme's comes back when
  // nothing plays or the page closes.
  _setBackdrop(picture) {
    if (picture === this._picture && (!picture || (this._back && this._back.isConnected))) return;
    this._picture = picture;
    if (!this._back || !this._back.isConnected) {
      if (this._back) this._back.style.cssText = this._was || '';
      this._back = this._seek(document.querySelector('home-assistant'), 'HUI-VIEW-BACKGROUND', 0);
      this._was = this._back ? this._back.style.cssText : '';
    }
    const back = this._back;
    if (!back) return;
    if (picture) {
      // The cover only tints the page: the theme's own background over it, four fifths strong, keeps every word as
      // readable as on any other page.
      const wash = 'color-mix(in srgb, var(--primary-background-color, #fafafa) 80%, transparent)';
      back.style.cssText = this._was + `;background:linear-gradient(${wash}, ${wash}), center / cover no-repeat url("${picture}") !important;` +
        'filter:blur(36px) saturate(1.4);transform:scale(1.15);transition:background .8s';
    } else {
      back.style.cssText = this._was;
    }
  }

  // An element by its tag, through the shadow roots of the page.
  _seek(node, tag, depth) {
    if (!node || depth > 14) return null;
    if (node.tagName === tag) return node;
    for (const k of [...(node.shadowRoot ? node.shadowRoot.children : []), ...node.children]) {
      const found = this._seek(k, tag, depth + 1);
      if (found) return found;
    }
    return null;
  }

  // ---- The library ----
  async _call(service, data) {
    const r = await this._hass.callWS({ type: 'call_service', domain: 'music_assistant', service,
      service_data: { config_entry_id: this._config.entry, ...data }, return_response: true });
    return r.response || {};
  }

  async _load() {
    const kind = KINDS.find((k) => k.id === this._kind) || KINDS[0];
    this._busy = true; this._error = ''; this._render();
    try {
      if (this._open) {
        if (this._open.type === 'artist') {
          const r = await this._call('search', { name: this._open.name, media_type: ['album'], limit: 24, library_only: false });
          this._items = (r.albums || []).filter((a) => (a.artists || []).some((x) => (x.name || '').toLowerCase() === this._open.name.toLowerCase()));
          if (!this._items.length) this._items = r.albums || [];
        } else {
          const r = await this._hass.callWS({ type: 'media_player/browse_media', entity_id: this._room(),
            media_content_id: this._open.uri, media_content_type: 'album' });
          this._items = (r.children || []).map((c) => ({ media_type: 'track', uri: c.media_content_id, name: c.title.replace(/^.* - /, ''),
            artists: [{ name: this._open.artist || '' }], image: c.thumbnail || this._open.image }));
        }
        this._title = this._open.name;
      } else if (this._query) {
        const types = kind.type ? [kind.type] : ['artist', 'track', 'album', 'playlist'];
        const r = await this._call('search', { name: this._query, media_type: types, limit: kind.type ? 30 : 8, library_only: false });
        this._items = kind.type ? (r[kind.list] || []) : types.flatMap((t) => r[KINDS.find((k) => k.type === t).list] || []);
        this._title = kind.type ? `“${this._query}” in ${kind.word.toLowerCase()}` : `“${this._query}”`;
      } else {
        const r = await this._call('get_library', kind.type
          ? { media_type: kind.type, limit: 40, order_by: 'sort_name' }
          : { media_type: 'playlist', favorite: true, limit: 40, order_by: 'sort_name' });
        this._items = r.items || [];
        if (!kind.type) {
          for (const t of ['album', 'track', 'radio', 'artist']) {
            const more = await this._call('get_library', { media_type: t, favorite: true, limit: 20, order_by: 'sort_name' });
            this._items = this._items.concat(more.items || []);
          }
        }
        this._title = kind.word;
      }
    } catch (e) {
      this._items = []; this._error = (e && e.message) || 'Music Assistant did not answer';
    }
    this._busy = false; this._render();
  }

  _play(item) {
    const room = this._room();
    if (!room || !item.uri) return;
    this._hass.callService('media_player', 'play_media', { entity_id: room, media_content_id: item.uri,
      media_content_type: item.media_type || 'music' });
    this._toast(`Playing ${item.name}`);
  }

  _toast(words) {
    const t = this.shadowRoot.getElementById('toast');
    t.textContent = words; t.style.opacity = '1';
    clearTimeout(this._toastTimer);
    this._toastTimer = setTimeout(() => { t.style.opacity = '0'; }, 2200);
  }

  // ---- Drawing ----
  _build() {
    this.shadowRoot.innerHTML = `
      <style>
        :host { display: block; position: relative; z-index: 1; }
        ha-card { padding: 14px; border-radius: 16px; }
        .row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
        .kind, .go, .back { border: 0; border-radius: 12px; height: 44px; padding: 0 14px; font: inherit; font-size: 15px;
          background: rgba(127,127,127,.14); color: var(--primary-text-color); display: flex; align-items: center; gap: 8px; cursor: pointer; }
        .kind.on { background: ${ACCENT}; color: white; }
        .go { background: ${ACCENT}; color: white; }
        input { flex: 1; min-width: 180px; height: 44px; border-radius: 12px; border: 0; padding: 0 14px; font: inherit; font-size: 16px;
          background: rgba(127,127,127,.14); color: var(--primary-text-color); outline: none; }
        .head { display: flex; align-items: center; gap: 10px; margin: 14px 2px 10px; font-size: 18px; font-weight: 600; }
        .head .muted { font-weight: 400; opacity: .6; font-size: 14px; }
        .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 12px; }
        .tile { position: relative; border-radius: 14px; background: rgba(127,127,127,.12); overflow: hidden; cursor: pointer; }
        .tile .pic { aspect-ratio: 1; background: rgba(127,127,127,.18) center/cover no-repeat; display: flex; align-items: center;
          justify-content: center; }
        .tile .pic ha-icon { --mdc-icon-size: 48px; opacity: .35; }
        .tile .name { padding: 8px 10px 2px; font-size: 14px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .tile .side { padding: 0 10px 10px; font-size: 12px; opacity: .65; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .tile .open { position: absolute; top: 8px; right: 8px; width: 36px; height: 36px; border-radius: 50%; border: 0;
          background: rgba(0,0,0,.55); color: white; display: flex; align-items: center; justify-content: center; cursor: pointer; }
        .tile .play { position: absolute; left: 8px; top: 8px; width: 36px; height: 36px; border-radius: 50%; background: ${ACCENT};
          color: white; display: flex; align-items: center; justify-content: center; opacity: .92; }
        .note { padding: 24px; text-align: center; opacity: .6; }
        #toast { position: absolute; left: 50%; bottom: 18px; transform: translateX(-50%); background: rgba(0,0,0,.75); color: white;
          padding: 10px 18px; border-radius: 12px; opacity: 0; transition: opacity .3s; pointer-events: none; }
      </style>
      <ha-card>
        <div class="row" id="kinds"></div>
        <div class="row" style="margin-top:10px">
          <input id="q" placeholder="Search your library and Spotify" enterkeyhint="search">
          <button class="go" id="go"><ha-icon icon="mdi:magnify"></ha-icon>Search</button>
        </div>
        <div class="head" id="head"></div>
        <div id="body"></div>
        <div id="toast"></div>
      </ha-card>`;
    const kinds = this.shadowRoot.getElementById('kinds');
    for (const k of KINDS) {
      const b = document.createElement('button');
      b.className = 'kind'; b.dataset.kind = k.id;
      b.innerHTML = `<ha-icon icon="${k.icon}"></ha-icon>${k.word}`;
      b.onclick = () => { this._kind = k.id; this._open = null; this._load(); };
      kinds.appendChild(b);
    }
    const q = this.shadowRoot.getElementById('q');
    const search = () => { this._query = q.value.trim(); this._open = null; this._load(); };
    this.shadowRoot.getElementById('go').onclick = search;
    q.onkeydown = (e) => { if (e.key === 'Enter') search(); };
  }

  _render() {
    if (!this.shadowRoot || !this._config) return;
    for (const b of this.shadowRoot.querySelectorAll('.kind')) b.classList.toggle('on', b.dataset.kind === this._kind);
    const head = this.shadowRoot.getElementById('head');
    const room = this._hass && this._hass.states[this._room()];
    const where = room ? room.attributes.friendly_name : '';
    head.innerHTML = '';
    if (this._open || this._query) {
      const back = document.createElement('button');
      back.className = 'back'; back.innerHTML = '<ha-icon icon="mdi:arrow-left"></ha-icon>';
      back.onclick = () => {
        if (this._open) this._open = this._open.from || null; else { this._query = ''; this.shadowRoot.getElementById('q').value = ''; }
        this._load();
      };
      head.appendChild(back);
    }
    const t = document.createElement('span'); t.textContent = this._title || ''; head.appendChild(t);
    const m = document.createElement('span'); m.className = 'muted'; m.textContent = where ? `plays in ${where}` : ''; head.appendChild(m);

    const body = this.shadowRoot.getElementById('body');
    if (this._busy) { body.innerHTML = '<div class="note">Loading…</div>'; return; }
    if (this._error) { body.innerHTML = `<div class="note">${this._error}</div>`; return; }
    if (!this._items.length) { body.innerHTML = '<div class="note">Nothing here</div>'; return; }
    const grid = document.createElement('div'); grid.className = 'grid';
    for (const item of this._items) {
      const tile = document.createElement('div'); tile.className = 'tile';
      const kind = KINDS.find((k) => k.type === item.media_type) || KINDS[4];
      const pic = item.image || (item.metadata && item.metadata.images && item.metadata.images[0] && item.metadata.images[0].path) || '';
      const artist = (item.artists || []).map((a) => a.name).filter(Boolean).join(', ');
      tile.innerHTML = `<div class="pic" style="${pic ? `background-image:url('${pic}')` : ''}">${pic ? '' : `<ha-icon icon="${kind.icon}"></ha-icon>`}</div>
        <div class="play"><ha-icon icon="mdi:play"></ha-icon></div>
        <div class="name"></div><div class="side"></div>`;
      tile.querySelector('.name').textContent = item.name || '';
      tile.querySelector('.side').textContent = [this._kind === 'favourites' ? kind.word.replace(/s$/, '') : '', artist].filter(Boolean).join(' · ');
      tile.onclick = () => this._play(item);
      if (item.media_type === 'artist' || item.media_type === 'album') {
        const open = document.createElement('button'); open.className = 'open';
        open.innerHTML = '<ha-icon icon="mdi:chevron-right"></ha-icon>';
        open.onclick = (e) => {
          e.stopPropagation();
          this._open = { type: item.media_type, name: item.name, uri: item.uri, image: pic, artist, from: this._open };
          this._load();
        };
        tile.appendChild(open);
      }
      grid.appendChild(tile);
    }
    body.innerHTML = ''; body.appendChild(grid);
  }
}

customElements.define('tessera-music-library', TesseraMusicLibrary);
window.customCards = window.customCards || [];
window.customCards.push({ type: 'tessera-music-library', name: 'Tessera music library',
  description: 'Music Assistant library and search in the look of the Tessera Music rooms screens.' });
