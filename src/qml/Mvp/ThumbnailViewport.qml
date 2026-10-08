import QtQuick

// Geometry and scheduling are independent of the image URL and cache identity.
QtObject {
    id: root
    required property GridView view
    required property var controller
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
            if (root.view.moving || root.view.benchmarkMoving || root.anchorPending) return
            const index = root.view.indexAt(root.view.contentX + 1, root.view.contentY + 1)
            const item = root.view.itemAtIndex(index)
            if (!item || item.path === undefined) return
            root.anchorPath = item.path
            root.anchorOffset = root.view.contentY - item.y
            root.anchorPending = true
        }
        function onRowsInserted() {
            root.dirty = true
            if (!root.anchorPending) return
            Qt.callLater(function() {
                if (!root.controller || !root.anchorPending || root.controller.thumbnailIndexForPath === undefined) return
                root.anchorPending = false
                const index = root.controller.thumbnailIndexForPath(root.anchorPath)
                if (index < 0) return
                const columns = Math.max(1, Math.floor(root.view.width / root.view.cellWidth))
                const y = Math.floor(index / columns) * root.view.cellHeight + root.anchorOffset
                root.view.contentY = Math.max(root.view.originY,
                    Math.min(y, root.view.originY + Math.max(0, root.view.contentHeight - root.view.height)))
            })
        }
        function onModelReset() { root.anchorPending = false; root.dirty = true }
    }
    property real lastY: 0
    property double lastTime: 0
    property int direction: 1
    property bool dirty: true
    property Timer ticker: Timer {
        interval: 16
        repeat: true
        running: root.view.visible
        onTriggered: { if (root.dirty || root.view.moving || root.view.benchmarkMoving) root.report() }
    }
    property Connections events: Connections {
        target: root.view
        function onContentYChanged() { root.dirty = true }
        function onCountChanged() { root.dirty = true }
        function onWidthChanged() { root.dirty = true }
        function onHeightChanged() { root.dirty = true }
        function onMovingChanged() { root.dirty = true; if (!root.view.moving) root.report() }
        function onVisibleChanged() { root.dirty = true; root.report() }
    }
    property Connections childrenEvents: Connections {
        target: root.view.contentItem
        function onChildrenChanged() { root.dirty = true }
    }
    function report() {
        if (!view || !controller || owner.length === 0) return
        dirty = false
        const now = Date.now()
        const delta = view.contentY - lastY
        if (Math.abs(delta) > 0.5) direction = delta > 0 ? 1 : -1
        const speed = lastTime > 0 ? Math.abs(delta) * 1000 / Math.max(1, now - lastTime) : 0
        const fast = (view.moving || view.benchmarkMoving) && speed >= view.height * 2
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
            const allowed = view.visible && item.visible && !item.isDirectory && (intersects || near)
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
    }
    Component.onDestruction: { if (registeredController && registeredController.setThumbnailViewport !== undefined && owner.length > 0) registeredController.setThumbnailViewport(owner, [], false) }
}
