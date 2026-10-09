import QtQuick
import QtQuick.Controls

// Geometry and scheduling are independent of the image URL and cache identity.
QtObject {
    id: root
    required property GridView view
    required property var controller
    readonly property var sceneWindow: view ? view.Window.window : null
    readonly property bool activeViewport: view && view.visible && sceneWindow &&
                                           sceneWindow.visible && sceneWindow.visibility !== Window.Minimized
    onSceneWindowChanged: { dirty = true; if (sceneWindow && activeViewport) sceneWindow.update() }
    onActiveViewportChanged: {
        dirty = true
        if (!activeViewport) report()
        else if (sceneWindow) sceneWindow.update()
    }
    property var registeredController: null
    property string owner: ""
    onControllerChanged: {
        if (registeredController && registeredController.setThumbnailViewport !== undefined && owner.length > 0)
            registeredController.setThumbnailViewport(owner, [], false)
        registeredController = controller
        owner = controller && controller.registerThumbnailViewport !== undefined
                ? controller.registerThumbnailViewport() : "preview"
        dirty = true
    }
    property string anchorPath: ""
    property real anchorOffset: 0
    property bool anchorPending: false
    property Connections modelEvents: Connections {
        target: root.view.model
        ignoreUnknownSignals: true
        function onRowsAboutToBeInserted() {
            if (root.view.moving || root.scrollBarPressed || root.anchorPending) return
            // Index arithmetic can lag several insertion batches until the next
            // GridView polish. Anchor the actual visible delegate's file instead.
            const children = root.view.contentItem.children
            let anchor = null
            for (const item of children) {
                if (item.path === undefined || item.thumbnailDemand === undefined || !item.visible ||
                    item.y + item.height <= root.view.contentY || item.y >= root.view.contentY + root.view.height ||
                    item.x + item.width <= root.view.contentX || item.x >= root.view.contentX + root.view.width) continue
                if (!anchor || item.y < anchor.y || (item.y === anchor.y && item.x < anchor.x)) anchor = item
            }
            if (!anchor) return
            root.anchorPath = anchor.path
            root.anchorOffset = root.view.contentY - anchor.y
            root.anchorPending = true
        }
        function onRowsInserted() {
            root.dirty = true
        }
        function onModelReset() {
            root.anchorPending = false; root.dirty = true
        }
    }
    property real lastY: 0
    property double lastTime: 0
    property int direction: 1
    property real lastSpeed: 0
    property bool fastScrolling: false
    readonly property bool scrollBarPressed: view.ScrollBar.vertical !== null && view.ScrollBar.vertical.pressed
    property Connections scrollBarEvents: Connections {
        target: root.view.ScrollBar.vertical
        function onPressedChanged() { root.dirty = true }
    }
    property bool dirty: true
    onDirtyChanged: { if (dirty && activeViewport && sceneWindow) sceneWindow.update() }
    property Connections frameEvents: Connections {
        target: root.sceneWindow
        function onAfterAnimating() {
            root.restoreAnchor()
            if (root.activeViewport && (root.dirty || root.view.moving ||
                                       root.scrollBarPressed || root.fastScrolling)) root.report()
        }
    }
    property Connections events: Connections {
        target: root.view
        ignoreUnknownSignals: true
        function onContentYChanged() { root.dirty = true }
        function onCountChanged() { root.dirty = true }
        function onWidthChanged() { root.dirty = true }
        function onHeightChanged() { root.dirty = true }
        function onMovingChanged() { root.dirty = true }
        function onVisibleChanged() { root.dirty = true }
    }
    property Connections childrenEvents: Connections {
        target: root.view.contentItem
        function onChildrenChanged() { root.dirty = true }
    }
    function restoreAnchor() {
        if (!anchorPending) return
        anchorPending = false
        if (!activeViewport || view.moving || scrollBarPressed ||
            !controller || controller.thumbnailIndexForPath === undefined) return
        const index = controller.thumbnailIndexForPath(anchorPath)
        if (index < 0) return
        // Flush the frame's model changes once, then read real delegate geometry.
        // GridView origins can shift when sorted insertions precede the viewport.
        view.forceLayout()
        const item = view.itemAtIndex(index)
        const columns = Math.max(1, Math.floor(view.width / view.cellWidth))
        const itemY = item && item.path === anchorPath ? item.y
                     : view.originY + Math.floor(index / columns) * view.cellHeight
        view.contentY = Math.max(view.originY,
            Math.min(itemY + anchorOffset, view.originY + Math.max(0, view.contentHeight - view.height)))
    }
    function report() {
        if (!view || !controller || owner.length === 0) return
        dirty = false
        const now = Date.now()
        const delta = view.contentY - lastY
        if (Math.abs(delta) > 0.5) direction = delta > 0 ? 1 : -1
        const moving = activeViewport && (view.moving || scrollBarPressed)
        const speed = !activeViewport ? 0 : Math.abs(delta) > 0.5 && lastTime > 0
                      ? Math.abs(delta) * 1000 / Math.max(1, now - lastTime)
                      : moving ? lastSpeed : 0
        lastSpeed = speed
        // Scrollbar jumps and programmatic positioning need cancellation even when
        // Flickable.moving is false; clear the transient jump state on the next tick.
        const fast = activeViewport && (speed >= view.height * 2 || Math.abs(delta) >= view.height)
        fastScrolling = fast
        lastY = view.contentY
        lastTime = now
        const top = view.contentY
        const bottom = top + view.height
        const ahead = fast ? 0.5 : 1
        const behind = fast ? 0 : 0.5
        const low = top - view.height * (direction > 0 ? behind : ahead)
        const high = bottom + view.height * (direction > 0 ? ahead : behind)
        const children = view.contentItem.children
        const candidates = []
        let visibleCount = 0
        for (let i = 0; i < children.length; ++i) {
            const item = children[i]
            if (item.thumbnailDemand === undefined) continue
            const intersects = item.y + item.height > top && item.y < bottom
                               && item.x + item.width > view.contentX
                               && item.x < view.contentX + view.width
            const near = item.y + item.height > low && item.y < high
            const allowed = activeViewport && item.visible && !item.isDirectory && (intersects || near)
            if (allowed) {
                if (intersects) ++visibleCount
                const distance = Math.abs(item.y + item.height / 2 - (top + bottom) / 2)
                const aheadOfView = direction > 0 ? item.y >= bottom : item.y + item.height <= top
                candidates.push({ item: item, path: item.path, visible: intersects,
                                  priority: (intersects ? 80 : aheadOfView ? 35 : 20)
                                            - Math.min(15, Math.floor(distance / Math.max(1, view.height) * 10)) })
            } else item.thumbnailDemand = false
        }

        candidates.sort(function(a, b) { return b.priority - a.priority })
        const limit = Math.max(visibleCount, Math.min(128, visibleCount * 3))
        const entries = []
        for (let i = 0; i < candidates.length; ++i) {
            if (i < limit) entries.push({ path: candidates[i].path, priority: candidates[i].priority })
            else candidates[i].item.thumbnailDemand = false
        }
        // Publish priority before Images create their asynchronous responses.
        if (controller.setThumbnailViewport !== undefined) controller.setThumbnailViewport(owner, entries, fast)
        for (let i = 0; i < Math.min(limit, candidates.length); ++i)
            candidates[i].item.thumbnailDemand = true
        if (fast && !moving && sceneWindow) sceneWindow.update()
    }
    Component.onDestruction: { if (registeredController && registeredController.setThumbnailViewport !== undefined && owner.length > 0) registeredController.setThumbnailViewport(owner, [], false) }
}
