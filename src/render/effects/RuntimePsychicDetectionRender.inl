void drawRuntimePsychicDetection(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderStats& stats, const SpecialMechanismRenderSnapshot& snapshot) {
    if (dc == nullptr || !snapshot.hasBeamEndpoints
        || (!context.visionObserverOwner.empty()
            && toLowerAscii(snapshot.owner) != toLowerAscii(context.visionObserverOwner))) return;
    const POINT from = screenPoint(projectCellXY(snapshot.sourceX, snapshot.sourceY,
        snapshot.sourceLevel, camera), viewport, camera);
    const POINT to = screenPoint(projectCellXY(snapshot.targetX, snapshot.targetY,
        snapshot.targetLevel, camera), viewport, camera);
    const double dx = static_cast<double>(to.x) - from.x;
    const double dy = static_cast<double>(to.y) - from.y;
    const double distance = std::hypot(dx, dy);
    if (distance < 1.0) return;
    const int saved = SaveDC(dc);
    if (saved == 0) return;
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    SetBkMode(dc, TRANSPARENT);
    HPEN pen = CreatePen(PS_DOT, 1, snapshot.phase == "Attack"
        ? RGB(255,80,80) : RGB(180,100,255));
    if (pen != nullptr) {
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, from.x, from.y, nullptr);
        LineTo(dc, to.x, to.y);
        const double ux = dx / distance, uy = dy / distance;
        for (int side : {-1, 1}) {
            MoveToEx(dc, to.x, to.y, nullptr);
            LineTo(dc, to.x - static_cast<int>(ux * 8 - side * uy * 4),
                to.y - static_cast<int>(uy * 8 + side * ux * 4));
        }
        SelectObject(dc, previous);
        DeleteObject(pen);
        recordRuntimeSpecialMechanismRenderIntent(stats, snapshot, 0);
    }
    RestoreDC(dc, saved);
}
