// tvOS-style app transitions (Transitions.qml beside this file), loaded into
// surface-manager (LG's compositor) as a key filter.
//
// tv/settings-key puts this file at the front of the
// com.webos.surfacemanager.keyFilters list, so the handler sees the Home and
// Back keys before LG's handlers act on them: the app's picture has to be
// taken while it is still on screen. It never takes a key.
//
// surface-manager loads every file in the list through a loader object and
// calls init() whenever it changes the list (adding a file, at boot), so init()
// replaces the layer it finds from a previous load. The layer belongs to this
// load's loader: when surface-manager drops the loader (the list changed, or
// uninstall took this file out of it), the layer stops and leaves LG's views
// as they were. Any error leaves the keys and LG's own cut untouched.

var transitionsQml = "file:///var/lib/lgtv-tweaks/Transitions.qml";
var transitions = null;

function init() {
    try {
        for (var i = views.children.length - 1; i >= 0; i--) {
            var old = views.children[i];
            if (old.objectName !== "lgtvTransitions") continue;
            try {
                old.release();  // now: destroy() waits, and the new layer lowers LG's views
            } catch (e) {
                // A layer that already stopped as its loader went.
            }
            old.destroy();
        }
        // What the layer uses of surface-manager: a firmware update may change it.
        if (!views.fullscreen || !views.fullscreen.surfaceOpened || views.fullscreen.currentItem === undefined ||
            !views.starfishImageSplash) {
            console.warn("[lgtv-tweaks] transitions: surface-manager isn't laid out as expected; off");
            return;
        }
        // A new URL each time: the engine caches components by URL, and this
        // runs again after the file is updated.
        var component = Qt.createComponent(transitionsQml + "?" + Date.now());
        if (component.status !== Component.Ready) {
            console.warn("[lgtv-tweaks] transitions: " + component.errorString());
            return;
        }
        // root: the loader that included this file (StarfishKeyFilterLoader.qml).
        var owner = typeof root !== "undefined" && root.filename !== undefined ? root : null;
        transitions = component.createObject(views, {"owner": owner});
    } catch (e) {
        console.warn("[lgtv-tweaks] transitions: " + e);
    }
}

function handleTransitionKeys(key, pressed, autoRepeat, deviceId) {
    try {
        if (transitions && pressed && !autoRepeat) {
            switch (key) {
            case Qt.Key_Super_L:  // Home on the remote
            case Qt.Key_Meta:     // Home from LG's phone remote (networkinput)
            case WebOS.Key_webOS_Back:
            case Qt.Key_Escape:
            case Qt.Key_Return:  // OK: apps ask "exit the app?" and quit on OK
            case Qt.Key_Enter:
                transitions.leaving();
                break;
            }
        }
    } catch (e) {
        console.warn("[lgtv-tweaks] transitions: " + e);
    }
    return KeyPolicy.NextPolicy;
}
