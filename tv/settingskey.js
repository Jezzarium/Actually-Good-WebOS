// Remote Settings (gear) key -> Control Center (com.lgtvtweaks.control).
//
// Loaded by surface-manager through the com.webos.surfacemanager.keyFilters
// config, inserted just before LG's handleHotkeys (appLaunch.js), so earlier
// filters such as the triple-press AV reset still see the key. Mirrors LG's
// own Settings case: act on release, leave the key to YouTube, and stay out of
// the way during first-use setup. Pressing it again hides the panel: the app
// closes itself when launched while open.
// Any error falls through to LG's handler, so a bug cannot swallow the key.

var quickSettingsAppId = "com.lgtvtweaks.control";
var quickSettingsYouTubeIds = ["youtube.leanback.v4", "youtube.leanback.ytv.v1", "youtube.leanback.kids.v4"];

function handleQuickSettingsKey(key, pressed, autoRepeat, deviceId) {
    if (key !== WebOS.Key_webOS_Settings && key !== WebOS.Key_webOS_LocalSettings)
        return KeyPolicy.NextPolicy;

    try {
        for (var i = 0; i < quickSettingsYouTubeIds.length; i++) {
            if (global.hasActiveFocus(quickSettingsYouTubeIds[i]))
                return KeyPolicy.NextPolicy;
        }
        if (!access.keyAction.launchApp || (bootManager.bootFirstUse && !systemPropertyService.powerOnlyMode))
            return KeyPolicy.NextPolicy;

        // The app stays loaded and hides itself rather than closing, so it
        // reopens instantly.
        if (!pressed && !autoRepeat)
            applicationManager.launch(quickSettingsAppId);
        return KeyPolicy.Accepted;
    } catch (e) {
        console.warn("[lgtv-tweaks] Settings key handler failed, falling back to LG: " + e);
        return KeyPolicy.NextPolicy;
    }
}
