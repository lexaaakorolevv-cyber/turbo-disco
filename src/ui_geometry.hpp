#pragma once
#include <algorithm>

namespace rf {
struct PanelGeometry {
    float width, height, buttonWidth, buttonHeight, titleY, statusY, statusHeight;
    float leftX, rightX, firstRowY, rowSpacing;
};
inline PanelGeometry panelGeometry(float viewportWidth, float viewportHeight) {
    float width = std::min(420.f, viewportWidth - 40.f);
    float height = std::min(296.f, viewportHeight - 32.f);
    float buttonHeight = std::min(30.f, height * .11f);
    float rowSpacing = buttonHeight + 10.f;
    float first = height - 64.f;
    float statusTop = first - 2 * rowSpacing - buttonHeight / 2.f - 10.f;
    float statusHeight = std::max(24.f, statusTop - 30.f);
    return {width, height, (width - 40.f) / 2.f, buttonHeight, height - 25.f,
        30.f + statusHeight / 2.f, statusHeight, width / 4.f, width * .75f, first, rowSpacing};
}
}
