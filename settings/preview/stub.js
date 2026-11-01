/*
 * Desktop stand-in for webOS's PalmServiceBridge, for checking layout and
 * navigation in a normal browser. Not packaged.
 *
 * It mimics the TV closely enough to exercise the app's routing: methods that
 * a developer-mode app may not call directly are refused unless they arrive
 * through the Homebrew Channel exec service, as on the TV.
 */
(function () {
  'use strict';

  const settings = {
    picture: {
      backlight: 35, pictureMode: 'expert2', eyeComfortMode: 'off', energySaving: 'off', contrast: '85',
      brightness: '50', color: '50', sharpness: '10', colorTemperature: '-40', peakBrightness: 'off',
      hdrDynamicToneMapping: 'on', gamma: 'high2', dynamicContrast: 'off', superResolution: 'off',
      noiseReduction: 'off', realCinema: 'on', motionEyeCare: 'off', aspectRatio: '16x9',
      screenShift: 'on', logoLuminanceAdjust: 'light',
    },
    aiPicture: { ai_Brightness: 'off' },
    sound: { soundMode: 'standard', soundOutput: 'tv_speaker', autoVolume: 'off' },
    option: {
      quickStartMode: 'on', screenSaverTimer: '3', standByLight: 'on', highContrast: 'off',
      menuTransparency: 'on', audioGuidance: 'off', screenOff: 'off',
    },
  };
  // Allowed values as getSystemSettingDesc resolves them on the OLED65C5PUA (SDR input).
  const lists = {
    pictureMode: ['personalized', 'vivid', 'normal', 'eco', 'cinema', 'sports', 'game', 'filmMaker', 'expert1', 'expert2'],
    energySaving: ['auto', 'off', 'min', 'med', 'max'],
    soundMode: ['personalized', 'aiSoundPlus', 'standard', 'movie', 'news', 'sports', 'music', 'game'],
    // Every output the firmware knows, as the TV's description lists them.
    soundOutput: ['tv_speaker', 'tv_speakerbar', 'external_optical', 'external_arc', 'bt_soundbar', 'wisa_speaker',
      'mobile_phone', 'lineout', 'headphone', 'bt_audio', 'tv_speaker_external_arc', 'usb_speaker', 'wow_cast'],
    peakBrightness: ['off', 'low', 'medium', 'high'],
    hdrDynamicToneMapping: ['on', 'off', 'HGIG'],
    gamma: ['high2', 'mediumHavingLevel'],
    dynamicContrast: ['off', 'low', 'medium', 'high'],
    superResolution: ['off', 'low', 'medium', 'high'],
    noiseReduction: ['off', 'low', 'medium', 'high', 'auto'],
    truMotionMode: ['off', 'cinemaClear', 'natural', 'smooth', 'user'],
    aspectRatio: ['16x9', 'original', '4x3', 'zoom', 'cinema_zoom'],
    logoLuminanceAdjust: ['off', 'light', 'strong'],
    screenSaverTimer: ['3', '10', '20', '30'],
  };
  const ranges = { sharpness: [0, 50], colorTemperature: [-50, 50] };
  const hiddenInSdr = { hdrDynamicToneMapping: true };
  const inactive = { realCinema: true };
  const privateMethods = /tvReservationAgent\/timer\/sleep|tvpower\/power\/turnOffScreen/;
  let sleepEnds = 0;

  function handle(uri, p) {
    const method = uri.split('/').pop();
    if (uri.indexOf('settingsservice') > 0) {
      const cat = settings[p.category] || {};
      if (method === 'getSystemSettings') {
        const out = {};
        p.keys.forEach((k) => { if (k in cat) out[k] = cat[k]; });
        return { returnValue: true, category: p.category, settings: out };
      }
      if (method === 'getSystemSettingDesc') {
        return {
          returnValue: true,
          results: p.keys.map((k) => ({
            category: p.category, key: k,
            ui: { visible: !hiddenInSdr[k], active: !inactive[k] },
            values: lists[k] ? { arrayExt: lists[k].map((v) => ({ value: v, active: true, visible: true })) }
              : ranges[k] ? { range: { min: ranges[k][0], max: ranges[k][1], interval: 1 } }
                : typeof cat[k] === 'number' || /^-?\d+$/.test(cat[k]) ? { range: { min: 0, max: 100, interval: 1 } }
                  : { arrayExt: ['on', 'off'].map((v) => ({ value: v, active: true, visible: true })) },
          })),
        };
      }
      if (method === 'setSystemSettings') {
        Object.assign(settings[p.category] = settings[p.category] || {}, p.settings);
        console.log('[stub] set', p.category, JSON.stringify(p.settings));
        return { returnValue: true };
      }
    }
    if (method === 'getRemainTime') {
      return { returnValue: true, sleepTimeSecond: Math.max(0, Math.round((sleepEnds - Date.now()) / 1000)) };
    }
    if (uri.indexOf('timer/sleep/add') > 0) {
      sleepEnds = Number(p.sleepTimeMinute) ? Date.now() + Number(p.sleepTimeMinute) * 60000 : 0;
      return { returnValue: true };
    }
    console.log('[stub]', uri, JSON.stringify(p));
    return { returnValue: true };
  }

  // Parses `luna-send -n 1 <uri> '<json>' </dev/null` as the app builds it.
  function exec(command) {
    const m = /^luna-send -n 1 (\S+) '(.*)' <\/dev\/null$/.exec(command);
    if (!m) return { returnValue: false, errorText: 'stub: unsupported command' };
    const res = handle(m[1], JSON.parse(m[2].replace(/'\\''/g, "'")));
    return { returnValue: true, stdoutString: JSON.stringify(res) + '\n' };
  }

  window.PalmServiceBridge = function () {};
  window.PalmServiceBridge.prototype.call = function (uri, json) {
    const p = JSON.parse(json);
    const res = uri.indexOf('hbchannel.service/exec') > 0 ? exec(p.command)
      : privateMethods.test(uri) ? { returnValue: false, errorText: 'Denied method call "' + uri + '" (stub)' }
        : handle(uri, p);
    setTimeout(() => this.onservicecallback(JSON.stringify(res)), 25 + Math.random() * 40);
  };
})();
