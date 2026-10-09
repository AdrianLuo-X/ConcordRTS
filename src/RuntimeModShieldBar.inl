    const auto* shieldType = state.rules.findTechnoModExtension(object.type);
    if (shieldType && !shieldType->shieldType.empty() && object.modExtensions.shieldCapacity > 0) {
        const auto& shield = object.modExtensions;
        const int width = object.kind == WorldObjectKind::Infantry ? 18 : 36;
        const int center = (projectedObject.selectionBounds.left + projectedObject.selectionBounds.right) / 2;
        const int top = projectedObject.selectionBounds.top - 15;
        const RECT frame{center - width / 2, top, center + width / 2, top + 4};
        const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        const COLORREF oldColor = SetDCBrushColor(dc, RGB(18, 30, 42));
        FillRect(dc, &frame, brush);
        SetDCBrushColor(dc, shield.shieldOnline ? RGB(96, 208, 255) : RGB(112, 120, 128));
        FrameRect(dc, &frame, brush);
        const int fill = static_cast<int>(static_cast<long long>(width - 2) * shield.shieldHealth / shield.shieldCapacity);
        const RECT inner{frame.left + 1, top + 1, frame.left + 1 + fill, top + 3};
        FillRect(dc, &inner, brush);
        SetDCBrushColor(dc, oldColor);
    }
