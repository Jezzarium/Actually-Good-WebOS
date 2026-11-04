// tvOS-style app transitions, drawn inside surface-manager (LG's compositor).
// transitions.js (a key filter) loads this as a layer over the app in front
// and LG's splash, and under everything else: the Control Center, the volume,
// notifications and LG's popups stay on top.
//
// Opening: just before Home (com.lgtvtweaks.home) launches an app from a
// tile, it sets the window property lgtvLaunch on its surface: where the tile
// is and how it looks. The tile grows from there to full screen, holds like a
// splash until the app has drawn, then fades away to show it.
//
// Going Home: when the Home or Back key goes down in that app, its picture is
// captured on the GPU (LG hides the app at once when Home comes back, so it
// must be taken first). When Home comes back the picture shrinks into the
// tile and crossfades into it as it lands. Video is on a plane of its own that
// the capture can't see, and an app may also go without a key; then the tile
// itself shrinks from full screen instead.
//
// Apps not opened from a tile, and switches that don't involve Home, keep
// LG's cut.

import QtQuick 2.12

Item {
    id: root
    objectName: "lgtvTransitions"
    anchors.fill: parent
    // Above LG's views lowered to -1 below, under all the others (at 0).
    z: -0.5

    // transitions.js's loader. surface-manager destroys it when the key filter
    // list changes (and makes a new one, which replaces this layer) or no
    // longer has us in it: this layer then stops and gives LG's views back.
    // (It stays, empty, until surface-manager restarts: destroying it from
    // here, as the loader goes, never completes.)
    property QtObject owner
    onOwnerChanged: if (!owner) release()

    readonly property string homeId: "com.lgtvtweaks.home"
    // LG's app view and the views LG stacks just above it (the splash,
    // spinners...): they go under this layer, keeping their order.
    readonly property var below: [views.fullscreen, views.starfishHCAP, views.starfishFloating,
        views.starfishInfiniteLoadingSpinner, views.starfishSpinner, views.starfishBootPopup, views.starfishImageSplash]
    property bool released: false

    property Item home: null      // Home's surface
    property var tile: null       // its lgtvLaunch, parsed
    property int launches: -1     // tile.n when last read: a new n is a new launch
    property string front: ""     // the last app to open that isn't Home
    property string opening: ""   // the app the tile is zooming into
    property bool expanded: false
    property bool drawn: false
    property double snapped: 0    // when the snapshot was taken (ms since the epoch); 0: none
    property string snappedApp: ""
    property bool useSnapshot: false

    // Motion as in Apple's interfaces: springs, set by a damping ratio and a
    // response (the period of the undamped spring, in seconds), as Apple
    // describes them ("Designing Fluid Interfaces", WWDC 2018). They start
    // softly, move fast, and take a long, gentle time to settle, where a
    // fixed easing curve reads as mechanical. Closing lands with a hint of
    // bounce, as an app does going back into its icon on iOS.
    readonly property real openDamping: 0.9
    readonly property real openResponse: 0.5
    readonly property real closeDamping: 0.85
    readonly property real closeResponse: 0.55

    // How far a spring from rest has gone toward its target (0..1, past 1 on
    // overshoot), after time seconds.
    function spring(time, damping, response) {
        var w = 2 * Math.PI / response, decay = damping * w;
        var wd = w * Math.sqrt(1 - damping * damping);
        return 1 - Math.exp(-decay * time) * (Math.cos(wd * time) + decay / wd * Math.sin(wd * time));
    }
    // When it is within a few pixels of the target over the screen's width.
    function settleTime(damping, response) {
        var w = 2 * Math.PI / response;
        return Math.log(1 / (0.003 * Math.sqrt(1 - damping * damping))) / (damping * w);
    }

    // Seconds into the open or close spring.
    property real elapsed: 0
    property bool closing: false
    // 0: the tile where Home draws it, 1: full screen.
    readonly property real t: closing ? 1 - spring(elapsed, closeDamping, closeResponse)
                                      : spring(elapsed, openDamping, openResponse)

    Component.onCompleted: {
        for (var i = 0; i < below.length; i++) {
            if (below[i]) below[i].z = -1;
        }
        // Loaded (again) while an app is in front: Home is hidden, not gone.
        var item = views.fullscreen.currentItem;
        if (item && item.appId !== homeId) front = item.appId;
        var surface = compositor.surfaceForApplication ? compositor.surfaceForApplication(homeId) : null;
        if (surface) watchHome(surface);
    }
    Component.onDestruction: release()

    // Stops for good and puts LG's views back as they were. Before a new layer
    // replaces this one, and when it goes.
    function release() {
        if (released) return;
        released = true;
        reset();
        for (var i = 0; i < below.length; i++) {
            if (below[i]) below[i].z = 0;
        }
    }

    Connections {
        target: views.fullscreen
        enabled: !root.released
        onSurfaceOpened: root.surfaceOpened(surfaceItem)
    }
    Connections {
        target: root.home
        enabled: !root.released
        ignoreUnknownSignals: true
        onWindowPropertiesChanged: root.readTile(true)
    }
    // The opening app's next frame: it has drawn.
    Connections {
        id: redraws
        target: null
        ignoreUnknownSignals: true
        onRedraw: root.appDrawn()
    }

    function watchHome(item) {
        if (home === item) return;
        // A new surface: Home started again and counts its launches from 0.
        home = item;
        launches = -1;
        readTile(false);
    }

    function readTile(launch) {
        var json = home && home.windowProperties ? home.windowProperties.lgtvLaunch : undefined;
        var parsed = null;
        try {
            parsed = json ? JSON.parse(json) : null;
        } catch (e) {
            console.warn("[lgtv-tweaks] transitions: bad lgtvLaunch: " + json);
        }
        tile = parsed;
        if (!parsed || parsed.n === launches) return;
        launches = parsed.n;
        if (launch && views.fullscreen.currentItem === home) open(parsed.id);
    }

    function surfaceOpened(item) {
        if (!item) return;
        if (item.appId === homeId) {
            watchHome(item);
            var from = front;
            front = "";
            reset();
            if (from && tile && from === tile.id) close(from);
            return;
        }
        front = item.appId;
        if (item.appId === opening) {
            redraws.target = item.surface;
            drawWait.restart();
        } else if (opening) {
            reveal();  // something else came up instead
        }
    }

    // The Home or Back key went down. If Home comes back next, it shrinks
    // this picture of the app.
    function leaving() {
        if (released) return;
        var item = views.fullscreen.currentItem;
        if (!item || !tile || item.appId !== tile.id || playingVideo(item)) return;
        snapshot.sourceItem = item;
        snapshot.scheduleUpdate();  // drawn into its texture with the next frame
        snapped = Date.now();
        snappedApp = item.appId;
    }

    // Whether LG has a video window for the app: it is on its own plane, and
    // a snapshot would show a black hole there.
    function playingVideo(item) {
        for (var p = item.parent; p; p = p.parent) {
            if (p.foregroundVideoWindowMgr === undefined) continue;
            var videos = p.foregroundVideoWindowMgr.getForegroundVideoWindow();
            for (var i = 0; i < videos.length; i++) {
                if (videos[i].appId === item.appId) return true;
            }
            return false;
        }
        return false;
    }

    // Mostly on screen: Home may have scrolled it away.
    function onScreen(r) {
        return r && r.x > -r.w / 2 && r.y > -r.h / 2 && r.x + r.w / 2 < width && r.y + r.h / 2 < height;
    }

    function open(appId) {
        reset();
        if (!onScreen(tile)) return;
        opening = appId;
        homeShot.sourceItem = home;
        homeShot.scheduleUpdate();
        homeShot.visible = true;
        closing = false;
        elapsed = 0;
        card.opacity = 1;
        card.visible = true;
        expand.start();
        openTimeout.restart();
        watchdog.restart();
    }

    function appDrawn() {
        redraws.target = null;
        drawWait.stop();
        drawn = true;
        if (expanded) reveal();
    }

    function reveal() {
        if (!card.visible || fade.running) return;
        opening = "";
        expand.stop();
        openTimeout.stop();
        drawWait.stop();
        redraws.target = null;
        fade.start();
    }

    function close(appId) {
        useSnapshot = snappedApp === appId && Date.now() - snapped < 1500;
        snapped = 0;
        snappedApp = "";
        if (!onScreen(tile)) {
            reset();
            return;
        }
        closing = true;
        elapsed = 0;
        card.visible = true;
        // The tile's art, unlike the picture, isn't what was on screen: it
        // fades in as it starts to shrink.
        card.opacity = useSnapshot ? 1 : 0;
        if (!useSnapshot) appear.start();
        shrink.start();
        watchdog.restart();
    }

    function reset() {
        opening = "";
        expanded = drawn = false;
        expand.stop();
        fade.stop();
        shrink.stop();
        appear.stop();
        land.stop();
        openTimeout.stop();
        watchdog.stop();
        drawWait.stop();
        redraws.target = null;
        card.visible = false;
        homeShot.visible = false;
        homeShot.sourceItem = null;
        useSnapshot = false;
        // Let the app's surface go, unless a snapshot is waiting for Home.
        if (!snapped) snapshot.sourceItem = null;
    }

    // The springs run on their own clock: elapsed goes up steadily, t follows
    // the curve.
    NumberAnimation {
        id: expand
        target: root
        property: "elapsed"
        from: 0
        to: settleTime(openDamping, openResponse)
        duration: to * 1000
        onFinished: {
            root.expanded = true;
            homeShot.visible = false;
            if (root.drawn) root.reveal();
        }
    }
    NumberAnimation {
        id: fade
        target: card
        property: "opacity"
        to: 0
        duration: 250
        easing.type: Easing.InOutQuad
        onFinished: root.reset()
    }
    NumberAnimation {
        id: shrink
        target: root
        property: "elapsed"
        from: 0
        to: settleTime(closeDamping, closeResponse)
        duration: to * 1000
        onFinished: land.start()
    }
    NumberAnimation {
        id: appear
        target: card
        property: "opacity"
        to: 1
        duration: 150
        easing.type: Easing.OutQuad
    }
    // On the tile, which looks the same underneath.
    NumberAnimation {
        id: land
        target: card
        property: "opacity"
        to: 0
        duration: 150
        easing.type: Easing.InOutQuad
        onFinished: root.reset()
    }
    // After its surface opens, the app's first new frame, or this long.
    Timer {
        id: drawWait
        interval: 100
        onTriggered: root.appDrawn()
    }
    // An app slow to open (or that never does) is shown, or Home, by then.
    Timer {
        id: openTimeout
        interval: 4000
        onTriggered: root.reveal()
    }
    // Whatever happens, the layer never stays on screen.
    Timer {
        id: watchdog
        interval: 5000
        onTriggered: root.reset()
    }

    // Home as it was when the tile was chosen: LG takes Home away as soon as
    // the app's window opens, and the tile grows over this instead.
    // Home's window has alpha where it blends; LG composes it over black.
    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: homeShot.visible
    }
    ShaderEffectSource {
        id: homeShot
        anchors.fill: parent
        visible: false
        live: false
    }

    // The app's picture. It isn't shown (the card draws it), but it must stay
    // visible, at no opacity, for its texture to be drawn.
    ShaderEffectSource {
        id: snapshot
        width: root.width
        height: root.height
        opacity: 0
        live: false
        mipmap: true
    }

    Image {
        id: icon
        visible: false
        asynchronous: true
        mipmap: true
        sourceSize.height: 320
        source: root.tile && root.tile.icon ? "file://" + root.tile.icon : ""
    }

    // The tile (its colours and icon, rounded), or the app's picture, at any
    // size between the tile and the screen.
    ShaderEffect {
        id: card
        visible: false
        x: root.tile ? root.tile.x * (1 - root.t) : 0
        y: root.tile ? root.tile.y * (1 - root.t) : 0
        width: root.tile ? root.tile.w + (root.width - root.tile.w) * root.t : root.width
        height: root.tile ? root.tile.h + (root.height - root.tile.h) * root.t : root.height

        // The icon grows less than the tile: 2.5 times at full screen.
        readonly property real iconScale: 1 + 1.5 * root.t
        readonly property real iconW: root.tile ? root.tile.iw * iconScale : 0
        readonly property real iconH: root.tile ? root.tile.ih * iconScale : 0

        property size cardSize: Qt.size(width, height)
        property real radius: root.tile ? root.tile.r * Math.max(0, 1 - root.t) : 0
        property color topColor: root.tile ? root.tile.top : "black"
        property color bottomColor: root.tile ? root.tile.bottom : "black"
        property variant iconTexture: icon
        property rect iconRect: Qt.rect((width - iconW) / 2, (height - iconH) / 2, iconW, iconH)
        property real iconRadius: root.tile ? root.tile.ir * iconScale : 0
        property real iconShown: icon.status === Image.Ready && iconW > 0 ? 1 : 0
        property variant snapTexture: snapshot
        // Cropped to the card's shape rather than squeezed.
        property point snapScale: width / height < root.width / root.height
                                  ? Qt.point(width / height * root.height / root.width, 1)
                                  : Qt.point(1, height / width * root.width / root.height)
        // The picture until the last stretch, then the tile.
        property real snapMix: root.useSnapshot ? Math.max(0, Math.min(1, (root.t - 0.01) / 0.15)) : 0

        Behavior on iconShown { NumberAnimation { duration: 150 } }

        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform lowp float qt_Opacity;
            uniform highp vec2 cardSize;
            uniform highp float radius;
            uniform lowp vec4 topColor;
            uniform lowp vec4 bottomColor;
            uniform sampler2D iconTexture;
            uniform highp vec4 iconRect;
            uniform highp float iconRadius;
            uniform lowp float iconShown;
            uniform sampler2D snapTexture;
            uniform highp vec2 snapScale;
            uniform lowp float snapMix;

            // Signed distance from p to a rounded rectangle centred on 0.
            highp float box(highp vec2 p, highp vec2 halfSize, highp float r) {
                highp vec2 q = abs(p) - halfSize + vec2(r);
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }

            void main() {
                highp vec2 p = qt_TexCoord0 * cardSize;
                lowp float inside = clamp(0.5 - box(p - cardSize * 0.5, cardSize * 0.5, radius), 0.0, 1.0);
                lowp vec4 c = mix(topColor, bottomColor, qt_TexCoord0.y);
                highp vec2 ip = (p - iconRect.xy) / iconRect.zw;
                if (iconShown > 0.0 && ip.x >= 0.0 && ip.y >= 0.0 && ip.x <= 1.0 && ip.y <= 1.0) {
                    lowp float a = clamp(0.5 - box(p - iconRect.xy - iconRect.zw * 0.5, iconRect.zw * 0.5, iconRadius), 0.0, 1.0);
                    lowp vec4 i = texture2D(iconTexture, ip) * a * iconShown;
                    c = i + c * (1.0 - i.a);
                }
                if (snapMix > 0.0) {
                    // Apps are opaque; where video punched through, black.
                    lowp vec4 s = vec4(texture2D(snapTexture, 0.5 + (qt_TexCoord0 - 0.5) * snapScale).rgb, 1.0);
                    c = mix(c, s, snapMix);
                }
                gl_FragColor = c * inside * qt_Opacity;
            }"
    }
}
