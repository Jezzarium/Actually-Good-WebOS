/*
 * Quick Settings for LG webOS TVs.
 *
 * A small, dependency-free replacement for LG's settings panel. The sheet is
 * drawn at once from the values cached on the last run, with Brightness
 * focused, and the live values are read from the TV right after.
 *
 * Runtime: Chromium 120 (webOS 10 WebAppMgr). Luna services are reached via
 * PalmServiceBridge. A developer-mode app only holds the "public" ACL group,
 * so methods that need more go through Homebrew Channel's root exec service
 * (see luna()).
 */
(function () {
  'use strict';

  /* ------------------------------------------------------------------ *
   * Luna service access
   * ------------------------------------------------------------------ */

  const SETTINGS = 'luna://com.webos.settingsservice/';
  const SLEEP_TIMER = 'luna://com.webos.service.tvReservationAgent/timer/sleep/';
  const TURN_OFF_SCREEN = 'luna://com.webos.service.tvpower/power/turnOffScreen';
  const LAUNCH = 'luna://com.webos.applicationManager/launch';
  const EXEC = 'luna://org.webosbrew.hbchannel.service/exec';

  // Bridges must stay referenced until they answer, or GC drops the callback.
  const pending = new Set();

  function bridgeCall(uri, params, timeoutMs) {
    return new Promise((resolve, reject) => {
      const bridge = new PalmServiceBridge();
      pending.add(bridge);
      const finish = (settle, arg) => {
        if (!pending.delete(bridge)) return;
        clearTimeout(timer);
        settle(arg);
      };
      const timer = setTimeout(() => finish(reject, new Error('Timed out: ' + uri)), timeoutMs || 5000);
      bridge.onservicecallback = (text) => {
        let res;
        try { res = JSON.parse(text); } catch (e) { finish(reject, e); return; }
        if (res.returnValue === false) finish(reject, new Error(res.errorText || uri + ' failed'));
        else finish(resolve, res);
      };
      bridge.call(uri, JSON.stringify(params || {}));
    });
  }

  // Runs `luna-send` as root through Homebrew Channel. stdin must be redirected
  // or luna-send prints nothing. Only the JSON payload is variable, and it is
  // single-quoted for the shell.
  function execCall(uri, params) {
    const json = JSON.stringify(params || {}).replace(/'/g, "'\\''");
    const command = 'luna-send -n 1 ' + uri + " '" + json + "' </dev/null";
    return bridgeCall(EXEC, { command }, 8000).then((r) => {
      const line = (r.stdoutString || '').split('\n').find((l) => l.charAt(0) === '{');
      if (!line) throw new Error('No reply from ' + uri);
      const res = JSON.parse(line);
      if (res.returnValue === false) throw new Error(res.errorText || uri + ' failed');
      return res;
    });
  }

  // URIs to call through exec first. Seeded with methods whose ACL excludes
  // "public" on webOS 10 (checked with ls-monitor -i); afterwards it learns
  // from whichever route actually answered and remembers that across runs.
  const ROUTE_KEY = 'qs.execFirst';
  const execFirst = new Set(readStore(ROUTE_KEY) || [
    SLEEP_TIMER + 'add',
    SLEEP_TIMER + 'getRemainTime',
    TURN_OFF_SCREEN,
  ]);

  function luna(uri, params) {
    const viaExec = execFirst.has(uri);
    const first = viaExec ? execCall : bridgeCall;
    const second = viaExec ? bridgeCall : execCall;
    return first(uri, params).catch((err) =>
      second(uri, params).then((res) => {
        if (viaExec) execFirst.delete(uri); else execFirst.add(uri);
        writeStore(ROUTE_KEY, Array.from(execFirst));
        return res;
      }, () => { throw err; }));
  }

  function readStore(key) {
    try { return JSON.parse(localStorage.getItem(key)); } catch (e) { return null; }
  }
  function writeStore(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); } catch (e) { /* storage full or blocked */ }
  }

  /* ------------------------------------------------------------------ *
   * Display names (LG's own wording, shortened where it would not fit)
   * ------------------------------------------------------------------ */

  const PICTURE_MODES = {
    vivid: 'Vivid', normal: 'Standard', standard: 'Standard', external: 'Standard', eco: 'Eco',
    cinema: 'Cinema', cinemaBright: 'Cinema Home', sports: 'Sports', game: 'Game Optimizer',
    photo: 'Photo', filmMaker: 'Filmmaker Mode', personalized: 'Personalized',
    expert1: 'Expert (Bright)', expert2: 'Expert (Dark)', effect: 'HDR Effect',
  };
  const SOUND_MODES = {
    aiSoundPlus: 'AI Sound Pro', aiSound: 'AI Sound', standard: 'Standard', news: 'Clear Voice',
    music: 'Music', movie: 'Cinema', sports: 'Sports', game: 'Game Optimizer',
    personalized: 'Personalized', precision: 'Precision Sound',
  };
  const SOUND_OUTPUTS = {
    tv_speaker: 'TV Speaker', external_arc: 'HDMI ARC', external_optical: 'Optical',
    bt_soundbar: 'Bluetooth', bt_audio: 'Bluetooth', headphone: 'Headphones', lineout: 'Audio Out',
    usb_speaker: 'USB Audio', mobile_phone: 'Phone', wisa_speaker: 'WiSA Speakers',
    wow_cast: 'WOWCAST', flex_connector: 'FlexConnect', tv_speaker_external_arc: 'ARC + TV Speaker',
    tv_speaker_bluetooth: 'Bluetooth + TV Speaker', tv_speaker_headphone: 'Headphones + TV Speaker',
    tv_external_speaker: 'Optical + TV Speaker', tv_speaker_wow_cast: 'WOWCAST + TV Speaker',
  };
  const ENERGY = { auto: 'Auto', off: 'Off', min: 'Minimum', med: 'Medium', max: 'Maximum' };
  const TRUMOTION = {
    off: 'Off', cinemaClear: 'Cinematic Movement', natural: 'Natural', smooth: 'Smooth Movement',
    user: 'User Selection',
  };
  const GAMMA = { low: 'Low', medium: 'Medium', mediumHavingLevel: 'Medium', high1: 'High 1', high2: 'High 2' };
  const ASPECT = {
    '16x9': '16:9', '4x3': '4:3', '21x9': '21:9', '14x9': '14:9', original: 'Original',
    full_wide: 'Full Wide', zoom: 'Zoom', zoom2: 'Zoom 2', cinema_zoom: 'Cinema Zoom', caption_zoom: 'Caption Zoom',
  };
  const TONE_MAPPING = { on: 'On', off: 'Off', HGIG: 'HGIG' };

  // "cinema_zoom" -> "Cinema zoom", "mediumHigh" -> "Medium high"
  function humanize(v) {
    const s = String(v).replace(/_/g, ' ').replace(/([a-z])([A-Z0-9])/g, '$1 $2').toLowerCase();
    return s.charAt(0).toUpperCase() + s.slice(1);
  }
  const fromMap = (map) => (v) => map[v] || humanize(v);
  const minutes = (v) => (v === 'off' ? 'Off' : v + ' min');

  /* ------------------------------------------------------------------ *
   * Settings catalogue
   *
   * cat/key:  settingsservice category and key (key defaults to the id).
   *           Picture keys take their input/pictureMode dimensions from what
   *           is playing when no dimension is passed; that is how LG's own
   *           settings app reads and writes them.
   * kind:     range | choice | toggle
   * load/save: custom access for items that are not plain settings keys.
   * ------------------------------------------------------------------ */

  // From description.json (time/sleepTimer); the reservation agent has no list call.
  const SLEEP_OPTIONS = ['off', '10', '20', '30', '60', '90', '120', '180', '240'];

  const ITEMS = {
    // "Brightness" in LG's menu on OLED is OLED Pixel Brightness, i.e. `backlight`;
    // the `brightness` key is labelled "Black Level".
    backlight: { cat: 'picture', label: 'Brightness', kind: 'range', icon: 'sun' },
    pictureMode: {
      cat: 'picture', label: 'Picture mode', kind: 'choice', icon: 'picture',
      format: (v) => PICTURE_MODES[v.replace(/^(dolbyHdr|hdr)(.)/, (m, p, c) => c.toLowerCase())] || humanize(v),
    },
    eyeComfortMode: { cat: 'picture', label: 'Eye comfort', kind: 'toggle', icon: 'eye' },
    energySaving: { cat: 'picture', label: 'Energy saving', kind: 'choice', icon: 'leaf', format: fromMap(ENERGY) },
    contrast: { cat: 'picture', label: 'Contrast', kind: 'range' },
    brightness: { cat: 'picture', label: 'Black level', kind: 'range' },
    color: { cat: 'picture', label: 'Color', kind: 'range' },
    sharpness: { cat: 'picture', label: 'Sharpness', kind: 'range' },
    colorTemperature: {
      cat: 'picture', label: 'Color temperature', kind: 'range',
      format: (v) => (v < 0 ? 'W' + -v : v > 0 ? 'C' + v : '0'),
    },
    peakBrightness: { cat: 'picture', label: 'Peak brightness', kind: 'choice' },
    hdrDynamicToneMapping: { cat: 'picture', label: 'Dynamic tone mapping', kind: 'choice', format: fromMap(TONE_MAPPING) },
    gamma: { cat: 'picture', label: 'Gamma', kind: 'choice', format: fromMap(GAMMA) },
    dynamicContrast: { cat: 'picture', label: 'Dynamic contrast', kind: 'choice' },
    superResolution: { cat: 'picture', label: 'Super resolution', kind: 'choice' },
    noiseReduction: { cat: 'picture', label: 'Noise reduction', kind: 'choice' },
    truMotionMode: { cat: 'picture', label: 'TruMotion', kind: 'choice', format: fromMap(TRUMOTION) },
    realCinema: { cat: 'picture', label: 'Real Cinema', kind: 'toggle' },
    motionEyeCare: { cat: 'picture', label: 'Motion eye care', kind: 'toggle' },
    ai_Brightness: { cat: 'aiPicture', label: 'AI Brightness', kind: 'toggle' },
    aspectRatio: { cat: 'picture', label: 'Aspect ratio', kind: 'choice', format: fromMap(ASPECT) },
    screenShift: { cat: 'picture', label: 'Screen shift', kind: 'toggle' },
    logoLuminanceAdjust: { cat: 'picture', label: 'Logo luminance', kind: 'choice' },

    soundMode: { cat: 'sound', label: 'Sound mode', kind: 'choice', icon: 'sound', format: fromMap(SOUND_MODES) },
    soundOutput: {
      cat: 'sound', label: 'Sound out', kind: 'choice', icon: 'speaker', format: fromMap(SOUND_OUTPUTS),
      // The description lists every output the firmware knows (combined and
      // vendor-specific ones too). Offer the main wired ones, as LG's own list
      // does; Bluetooth needs device pairing, which stays in LG's settings.
      only: ['tv_speaker', 'external_arc', 'external_optical'],
    },
    autoVolume: { cat: 'sound', label: 'Auto volume', kind: 'toggle' },

    sleepTimer: {
      cat: 'timer', label: 'Sleep timer', kind: 'choice', icon: 'moon', options: SLEEP_OPTIONS,
      format: (v, st) => (st.remain ? st.remain + ' min left' : minutes(v)),
      // LG's settings app shows the smallest option that covers the time left.
      load: () => luna(SLEEP_TIMER + 'getRemainTime', {}).then((r) => {
        const remain = Math.ceil((Number(r.sleepTimeSecond) || 0) / 60);
        const opts = SLEEP_OPTIONS.slice(1);
        return { remain, value: remain ? opts.find((o) => Number(o) >= remain) || opts[opts.length - 1] : 'off' };
      }),
      save: (v) => luna(SLEEP_TIMER + 'add', { sleepTimeMinute: v === 'off' ? '0' : v })
        .then(() => patch('sleepTimer', { remain: v === 'off' ? 0 : Number(v) })),
      optionFormat: minutes,
    },
    quickStartMode: { cat: 'option', label: 'Quick Start+', kind: 'toggle' },
    screenSaverTimer: { cat: 'option', label: 'Screen saver after', kind: 'choice', format: minutes },
    standByLight: { cat: 'option', label: 'Standby light', kind: 'toggle' },

    highContrast: { cat: 'option', label: 'High contrast', kind: 'toggle' },
    greyscale: { cat: 'option', label: 'Grayscale', kind: 'toggle' },
    menuTransparency: { cat: 'option', label: 'Menu transparency', kind: 'toggle' },
    audioGuidance: { cat: 'option', label: 'Audio guidance', kind: 'toggle' },
  };

  // Writes in one group can change other keys in it (a new picture mode brings
  // its own brightness, a new output its own sound mode), so a successful
  // choice/toggle write re-reads the group.
  const GROUP = { picture: 'picture', aiPicture: 'picture', sound: 'sound', option: 'option', timer: 'timer' };

  /* ------------------------------------------------------------------ *
   * State
   *
   * state[id] = { value, options, min, max, step, missing, hidden, disabled, remain }
   * Seeded from the last run so the first frame already shows real numbers.
   * ------------------------------------------------------------------ */

  const CACHE_KEY = 'qs.cache';
  const state = readStore(CACHE_KEY) || {};
  const touchedAt = {}; // id -> time of the last local change; stale reads must not undo it
  let saveTimer = 0;

  function st(id) {
    return state[id] || (state[id] = {});
  }

  function patch(id, changes) {
    Object.assign(st(id), changes);
    if (!saveTimer) saveTimer = setTimeout(() => { saveTimer = 0; writeStore(CACHE_KEY, state); }, 800);
    view.update(id);
  }

  // Applies a value read from the TV unless the user changed it moments ago.
  function received(id, changes) {
    if (Date.now() - (touchedAt[id] || 0) < 1500) delete changes.value;
    patch(id, changes);
  }

  function isVisible(id) {
    const s = st(id);
    return !s.missing && !s.hidden;
  }

  // Reads values and allowed values (getSystemSettingDesc resolves the
  // per-model, per-signal lists that description.json only holds in raw form).
  function load(ids) {
    const byCat = {};
    ids.forEach((id) => {
      const item = ITEMS[id];
      if (item.load) {
        item.load().then((r) => received(id, Object.assign({ missing: false }, r)), () => {});
      } else {
        (byCat[item.cat] = byCat[item.cat] || []).push(id);
      }
    });
    Object.keys(byCat).forEach((cat) => {
      const catIds = byCat[cat];
      const keys = catIds.map((id) => ITEMS[id].key || id);
      luna(SETTINGS + 'getSystemSettings', { category: cat, keys }).then((r) => {
        const values = r.settings || {};
        catIds.forEach((id, i) => {
          const v = values[keys[i]];
          received(id, v === undefined ? { missing: true } : { value: v, missing: false });
        });
      }, () => {});
      luna(SETTINGS + 'getSystemSettingDesc', { category: cat, keys }).then((r) => {
        (r.results || []).forEach((d) => {
          const id = catIds[keys.indexOf(d.key)];
          if (!id) return;
          const ui = d.ui || {};
          const values = d.values || {};
          const changes = { hidden: ui.visible === false, disabled: ui.active === false };
          if (values.range) {
            changes.min = values.range.min;
            changes.max = values.range.max;
            changes.step = values.range.interval || 1;
          } else if (values.arrayExt && !ITEMS[id].load) {
            const offered = values.arrayExt.filter((o) => o.active && o.visible).map((o) => o.value);
            // `only` narrows the list and sets its order.
            const only = ITEMS[id].only;
            changes.options = only ? only.filter((v) => offered.indexOf(v) >= 0) : offered;
          }
          patch(id, changes);
        });
      }, () => {});
    });
  }

  function reloadGroup(group) {
    load(Object.keys(state).filter((id) => ITEMS[id] && GROUP[ITEMS[id].cat] === group));
  }

  /* ------------------------------------------------------------------ *
   * Writes
   *
   * The screen updates on every step; the TV gets the newest value at most
   * once per WRITE_INTERVAL ms, one request at a time, and always the final
   * value. Holding Right on a slider therefore costs a handful of calls.
   * ------------------------------------------------------------------ */

  const WRITE_INTERVAL = 120;
  const writers = {};

  function save(id, value) {
    const item = ITEMS[id];
    if (item.save) return item.save(value);
    const settings = {};
    settings[item.key || id] = value;
    return luna(SETTINGS + 'setSystemSettings', { category: item.cat, settings });
  }

  function writerFor(id) {
    if (writers[id]) return writers[id];
    let latest;
    let queued = false;
    let busy = false;
    let lastSent = 0;
    let timer = 0;
    const pump = () => {
      if (busy || !queued) return;
      const wait = lastSent + WRITE_INTERVAL - Date.now();
      if (wait > 0) {
        if (!timer) timer = setTimeout(() => { timer = 0; pump(); }, wait);
        return;
      }
      const value = latest;
      queued = false;
      busy = true;
      lastSent = Date.now();
      save(id, value).then(() => {
        if (ITEMS[id].kind !== 'range') reloadGroup(GROUP[ITEMS[id].cat]);
      }, (err) => {
        toast("Couldn't change " + ITEMS[id].label + ': ' + err.message);
        delete touchedAt[id];
        load([id]);
      }).then(() => { busy = false; pump(); });
    };
    writers[id] = (value) => { latest = value; queued = true; pump(); };
    return writers[id];
  }

  function setValue(id, value) {
    touchedAt[id] = Date.now();
    patch(id, { value });
    writerFor(id)(value);
  }

  // Range values go back with the type they were read with (the service
  // returns `backlight` as a number but `contrast` as a string).
  function setRange(id, n) {
    const s = st(id);
    setValue(id, typeof s.value === 'string' ? String(n) : n);
  }

  /* ------------------------------------------------------------------ *
   * Actions
   * ------------------------------------------------------------------ */

  // The page stays loaded between opens: the TV preloads it at boot and
  // closing only hides the window, so the next Settings press shows it in
  // tens of milliseconds instead of a ~0.8 s cold start.
  const system = window.webOSSystem || window.PalmSystem;
  const canHide = !!(system && typeof system.hide === 'function');
  if (canHide && typeof system.setKeepAlive === 'function') system.setKeepAlive(true);

  function closeApp() {
    if (!canHide) { window.close(); return; }
    system.hide();
    reset(); // the next open starts on Brightness with fresh values
  }

  function launchApp(id, params) {
    luna(LAUNCH, params ? { id, params } : { id }).then(closeApp, (err) => toast("Couldn't open " + id + ': ' + err.message));
  }

  // Lumen's proven route first; LG's own app instead writes option/screenOff.
  function screenOff() {
    luna(TURN_OFF_SCREEN, {})
      .catch(() => luna(SETTINGS + 'setSystemSettings', { category: 'option', settings: { screenOff: 'on' } }))
      .then(closeApp, (err) => toast("Couldn't turn the screen off: " + err.message));
  }

  /* ------------------------------------------------------------------ *
   * Icons (24px stroke glyphs)
   * ------------------------------------------------------------------ */

  const ICONS = {
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    picture: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
    eye: '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    sound: '<path d="M4 9v6h4l5 4V5L8 9H4z"/><path d="M16.5 8.5a5 5 0 0 1 0 7M19 6a8.5 8.5 0 0 1 0 12"/>',
    speaker: '<rect x="6" y="2" width="12" height="20" rx="2"/><circle cx="12" cy="14" r="4"/><path d="M12 6h.01"/>',
    moon: '<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/>',
    leaf: '<path d="M5 19c0-9 6-14 15-14 0 9-5 15-14 15"/><path d="M5 19c3-4 6-6.5 10-8.5"/>',
    power: '<path d="M12 3v8"/><path d="M6.3 6.8a8 8 0 1 0 11.4 0"/>',
    sliders: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    chev: '<path d="M9 5l7 7-7 7"/>',
    check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  };

  function icon(name, cls) {
    return '<svg viewBox="0 0 24 24"' + (cls ? ' class="' + cls + '"' : '') + '>' + ICONS[name] + '</svg>';
  }

  /* ------------------------------------------------------------------ *
   * Cells
   *
   * A cell is { el, id?, update?(), activate?(), adjust?(dir, accel) }.
   * ------------------------------------------------------------------ */

  function make(tag, cls, html) {
    const el = document.createElement(tag);
    el.className = cls;
    if (html) el.innerHTML = html;
    return el;
  }

  function valueText(id) {
    const item = ITEMS[id];
    const s = st(id);
    if (s.value === undefined) return '—';
    if (item.kind === 'toggle') return s.value === 'on' ? 'On' : 'Off';
    if (item.format) return item.format(s.value, s);
    return humanize(s.value);
  }

  // compact: list-row sized, without icon (used inside sections).
  function sliderCell(id, compact) {
    const item = ITEMS[id];
    const el = make('div', compact ? 'cell slider compact' : 'cell slider',
      '<div class="fill"></div>' + (item.icon && !compact ? icon(item.icon) : '') +
      '<span class="label">' + item.label + '</span><span class="value"></span>');
    const fill = el.firstChild;
    const value = el.lastChild;
    const bounds = () => {
      const s = st(id);
      return { min: s.min !== undefined ? s.min : 0, max: s.max !== undefined ? s.max : 100, step: s.step || 1 };
    };
    return {
      el, id,
      update() {
        const b = bounds();
        const n = Number(st(id).value);
        value.textContent = isNaN(n) ? '—' : item.format ? item.format(n) : String(n);
        fill.style.transform = 'scaleX(' + (isNaN(n) ? 0 : (n - b.min) / (b.max - b.min)) + ')';
        el.classList.toggle('off', !!st(id).disabled);
      },
      adjust(dir, accel) {
        const n = Number(st(id).value);
        if (isNaN(n) || st(id).disabled) return;
        const b = bounds();
        const next = Math.min(b.max, Math.max(b.min, n + dir * b.step * accel));
        if (next !== n) setRange(id, next);
      },
      pointAt(fraction) {
        const b = bounds();
        setRange(id, Math.round(b.min + fraction * (b.max - b.min)));
      },
    };
  }

  // Shared behaviour of tiles and rows that show a setting.
  function settingCell(id, el, valueEl, switchEl) {
    const item = ITEMS[id];
    return {
      el, id,
      update() {
        if (switchEl) switchEl.classList.toggle('on', st(id).value === 'on');
        else valueEl.textContent = valueText(id);
        el.classList.toggle('off', !!st(id).disabled);
      },
      activate() {
        if (st(id).disabled) return;
        if (item.kind === 'toggle') setValue(id, st(id).value === 'on' ? 'off' : 'on');
        else push(pickerView(id));
      },
    };
  }

  function tileCell(id) {
    const item = ITEMS[id];
    const el = make('div', 'cell tile',
      icon(item.icon) + '<div class="text"><div class="label">' + item.label + '</div><div class="value"></div></div>');
    return settingCell(id, el, el.querySelector('.value'));
  }

  function settingRow(id) {
    const item = ITEMS[id];
    const toggle = item.kind === 'toggle';
    const el = make('div', 'cell row',
      '<span class="label">' + item.label + '</span>' +
      (toggle ? '<span class="switch"></span>' : '<span class="value"></span>' + icon('chev', 'chev')));
    return settingCell(id, el, el.querySelector('.value'), el.querySelector('.switch'));
  }

  function actionRow(label, iconName, activate, chevron) {
    const el = make('div', 'cell row',
      (iconName ? icon(iconName) : '') + '<span class="label">' + label + '</span>' + (chevron ? icon('chev', 'chev') : ''));
    return { el, activate };
  }

  /* ------------------------------------------------------------------ *
   * Views
   *
   * A view is { title, subtitle, ids, rows(), shape?(), focus?() }. rows()
   * returns arrays of cells, each inner array one line of focus navigation.
   * shape() describes what rows() depends on; when it changes after a read
   * the view is rebuilt, otherwise cells are patched in place.
   * ------------------------------------------------------------------ */

  const QUICK_IDS = ['backlight', 'pictureMode', 'eyeComfortMode', 'soundMode', 'soundOutput', 'sleepTimer', 'energySaving'];

  function quickView() {
    return {
      title: 'Settings',
      subtitle: '',
      ids: QUICK_IDS,
      rows: () => [
        [sliderCell('backlight')],
        [tileCell('pictureMode'), tileCell('eyeComfortMode')],
        [tileCell('soundMode'), tileCell('soundOutput')],
        [tileCell('sleepTimer'), tileCell('energySaving')],
        [actionRow('Screen off', 'power', screenOff)],
        [actionRow('More settings', 'sliders', () => push(moreView()), true)],
        [actionRow('All LG settings', 'gear', () => launchApp('com.palm.app.settings'), true)],
      ],
    };
  }

  const SECTIONS = [
    {
      title: 'Picture',
      ids: ['pictureMode', 'backlight', 'contrast', 'brightness', 'color', 'sharpness', 'colorTemperature',
        'peakBrightness', 'hdrDynamicToneMapping', 'gamma', 'dynamicContrast', 'superResolution',
        'noiseReduction', 'truMotionMode', 'realCinema', 'motionEyeCare', 'eyeComfortMode', 'ai_Brightness', 'aspectRatio'],
      lgTarget: 'picture',
    },
    { title: 'Sound', ids: ['soundMode', 'soundOutput', 'autoVolume'], lgTarget: 'sound' },
    { title: 'OLED care & power', ids: ['energySaving', 'screenShift', 'logoLuminanceAdjust', 'quickStartMode', 'standByLight'] },
    { title: 'General', ids: ['sleepTimer', 'screenSaverTimer'] },
    { title: 'Accessibility', ids: ['highContrast', 'greyscale', 'menuTransparency', 'audioGuidance'] },
  ];

  function moreView() {
    return {
      title: 'More settings',
      subtitle: 'Settings',
      ids: [],
      rows: () => SECTIONS.map((section) => [actionRow(section.title, null, () => push(sectionView(section)), true)]),
    };
  }

  function sectionView(section) {
    return {
      title: section.title,
      subtitle: 'More settings',
      ids: section.ids,
      shape: () => section.ids.filter(isVisible).join(),
      rows: () => {
        const rows = section.ids.filter(isVisible).map((id) =>
          [ITEMS[id].kind === 'range' ? sliderCell(id, true) : settingRow(id)]);
        if (section.lgTarget) {
          rows.push([actionRow('Open in LG settings', 'gear',
            () => launchApp('com.palm.app.settings', { target: section.lgTarget }), true)]);
        }
        return rows;
      },
    };
  }

  function pickerView(id) {
    const item = ITEMS[id];
    const label = item.optionFormat || item.format || humanize;
    return {
      title: item.label,
      subtitle: '',
      ids: [id],
      focus: () => st(id).value,
      shape: () => String(st(id).value) + ':' + (st(id).options || []).join(),
      rows: () => {
        const s = st(id);
        let options = s.options && s.options.length ? s.options : item.options || [s.value];
        // The current value may be one the list leaves out (e.g. a Bluetooth output).
        if (s.value !== undefined && options.map(String).indexOf(String(s.value)) < 0) options = [s.value].concat(options);
        return options.filter((o) => o !== undefined).map((option) => {
          const selected = String(option) === String(s.value);
          const el = make('div', 'cell row compact' + (selected ? ' selected' : ''),
            '<span class="label">' + label(option, {}) + '</span>' + (selected ? icon('check', 'check') : ''));
          return [{ el, key: String(option), activate: () => { setValue(id, option); pop(); } }];
        });
      },
    };
  }

  /* ------------------------------------------------------------------ *
   * Rendering and focus
   * ------------------------------------------------------------------ */

  const panel = make('div', '', '<header><h1></h1><p></p></header><div class="scroller"><div class="list"></div></div>');
  panel.id = 'panel';
  const titleEl = panel.querySelector('h1');
  const subtitleEl = panel.querySelector('p');
  const scroller = panel.querySelector('.scroller');
  let list = panel.querySelector('.list');
  const toastEl = make('div', '');
  toastEl.id = 'toast';
  document.body.appendChild(panel);
  document.body.appendChild(toastEl);

  const stack = [];
  const view = {
    current: null,
    rows: [],
    pos: { r: 0, c: 0 },
    shape: '',
    update(id) {
      const v = this.current;
      if (!v || v.ids.indexOf(id) < 0) return;
      if (v.shape && v.shape() !== this.shape) render(focusedKey(), false);
      else this.rows.forEach((row) => row.forEach((cell) => { if (cell.id === id && cell.update) cell.update(); }));
    },
  };

  function keyOf(cell) {
    return cell.key || cell.id || cell.el.textContent;
  }

  function focusedKey() {
    const cell = focusedCell();
    return cell ? keyOf(cell) : null;
  }

  function focusedCell() {
    const row = view.rows[view.pos.r];
    return row ? row[view.pos.c] : null;
  }

  // animate: slide the list in (navigation) or swap it silently (data refresh).
  function render(focusKey, animate) {
    const v = view.current;
    titleEl.textContent = v.title;
    subtitleEl.textContent = v.subtitle;
    view.shape = v.shape ? v.shape() : '';
    view.rows = v.rows();
    const fresh = make('div', animate ? 'list enter' : 'list');
    view.rows.forEach((row) => {
      if (row.length === 1) fresh.appendChild(row[0].el);
      else {
        const grid = make('div', 'grid');
        row.forEach((cell) => grid.appendChild(cell.el));
        fresh.appendChild(grid);
      }
      row.forEach((cell) => { if (cell.update) cell.update(); });
    });
    scroller.replaceChild(fresh, list);
    list = fresh;

    let r = 0;
    let c = 0;
    view.rows.forEach((row, ri) => row.forEach((cell, ci) => {
      if (focusKey !== null && keyOf(cell) === focusKey) { r = ri; c = ci; }
    }));
    view.pos = { r: -1, c: 0 };
    focusAt(r, c);
  }

  function focusAt(r, c) {
    if (!view.rows.length) return;
    r = Math.max(0, Math.min(view.rows.length - 1, r));
    c = Math.max(0, Math.min(view.rows[r].length - 1, c));
    const prev = focusedCell();
    if (prev) prev.el.classList.remove('f');
    view.pos = { r, c };
    const cell = focusedCell();
    cell.el.classList.add('f');
    scrollIntoView(cell.el);
  }

  // Room below the last cell for its focus scale-up.
  const SCROLL_END_ROOM = 12;

  // Keeps the focused cell inside the scroller with some context around it.
  // offsetTop is relative to the positioned .scroller and ignores the
  // list's own transform. The content height is measured to the bottom of
  // the last cell, not the list's trailing padding and margins, so a view
  // whose cells fit never moves.
  function scrollIntoView(el) {
    const viewH = scroller.clientHeight;
    const last = list.lastElementChild;
    const contentH = last ? last.offsetTop + last.offsetHeight + SCROLL_END_ROOM : 0;
    let offset = Math.min(0, viewH / 2 - el.offsetTop - el.offsetHeight / 2);
    offset = Math.max(offset, Math.min(0, viewH - contentH));
    list.style.transform = 'translateY(' + offset + 'px)';
  }

  function show(v, focusKey) {
    view.current = v;
    render(focusKey, true);
    if (v.ids.length) load(v.ids);
  }

  function push(v) {
    stack.push({ view: view.current, focus: focusedKey() });
    show(v, v.focus ? String(v.focus()) : null);
  }

  function pop() {
    const prev = stack.pop();
    if (!prev) { closeApp(); return; }
    view.current = prev.view;
    render(prev.focus, true);
  }

  function reset() {
    stack.length = 0;
    show(quickView(), 'backlight');
  }

  let toastTimer = 0;
  function toast(message) {
    toastEl.textContent = message;
    toastEl.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove('show'), 3000);
  }

  /* ------------------------------------------------------------------ *
   * Input
   * ------------------------------------------------------------------ */

  const KEY = { LEFT: 37, UP: 38, RIGHT: 39, DOWN: 40, ENTER: 13, BACK: 461, ESC: 27, BACKSPACE: 8 };

  // Remote auto-repeat arrives as a stream of keydowns. The longer a slider
  // key is held, the bigger the step, so 0..100 takes about two seconds.
  let held = { key: 0, count: 0, last: 0 };
  function acceleration(keyCode) {
    const now = Date.now();
    held = keyCode === held.key && now - held.last < 250
      ? { key: keyCode, count: held.count + 1, last: now }
      : { key: keyCode, count: 0, last: now };
    return held.count < 6 ? 1 : held.count < 16 ? 2 : 5;
  }

  document.addEventListener('keydown', (e) => {
    const cell = focusedCell();
    const { r, c } = view.pos;
    switch (e.keyCode) {
      case KEY.LEFT:
      case KEY.RIGHT: {
        const dir = e.keyCode === KEY.RIGHT ? 1 : -1;
        if (cell && cell.adjust) cell.adjust(dir, acceleration(e.keyCode));
        else focusAt(r, c + dir);
        break;
      }
      case KEY.UP: focusAt(r - 1, c); break;
      case KEY.DOWN: focusAt(r + 1, c); break;
      case KEY.ENTER: if (cell && cell.activate) cell.activate(); break;
      case KEY.BACK: case KEY.ESC: case KEY.BACKSPACE: pop(); break;
      default: return;
    }
    e.preventDefault();
  });
  document.addEventListener('keyup', () => { held.key = 0; });

  // Magic Remote pointer: hover focuses, click activates or sets a slider.
  function cellAt(target) {
    for (let r = 0; r < view.rows.length; r++) {
      for (let c = 0; c < view.rows[r].length; c++) {
        if (view.rows[r][c].el.contains(target)) return { r, c, cell: view.rows[r][c] };
      }
    }
    return null;
  }
  panel.addEventListener('mouseover', (e) => {
    const hit = cellAt(e.target);
    if (hit && (hit.r !== view.pos.r || hit.c !== view.pos.c)) focusAt(hit.r, hit.c);
  });
  panel.addEventListener('click', (e) => {
    const hit = cellAt(e.target);
    if (!hit) return;
    if (hit.cell.pointAt) {
      const box = hit.cell.el.getBoundingClientRect();
      hit.cell.pointAt(Math.min(1, Math.max(0, (e.clientX - box.left) / box.width)));
    } else if (hit.cell.activate) {
      hit.cell.activate();
    }
  });

  // With handlesRelaunch, every launch after the first lands here instead of
  // reloading the page. The Settings key handler relaunches with
  // {close: true} when the panel already has focus, to toggle it shut.
  document.addEventListener('webOSRelaunch', (e) => {
    let params = e.detail;
    if (!params) { try { params = JSON.parse(system.launchParams); } catch (err) { params = {}; } }
    if (params && params.close) closeApp();
    else reset();
  });

  reset();
})();
