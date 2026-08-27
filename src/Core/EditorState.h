#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Core/FormatPresets.h"

namespace ihv::core {

// Position/scale/rotation of the photo relative to the format guide.
// Ported from the `guide` global (line 322). Rotation is deliberately not
// derivable from dot-dragging (see solveGuideFromDots in EditorRenderer) —
// it's driven only by the rotation slider.
struct Guide {
    double x = 0;
    double y = 0;
    double scale = 1;
    double rotation = 0;
};

// Cyan/Magenta/Yellow shift for one luminance zone, -50..50. Ported from
// ccData (line 324).
struct CcZone {
    int c = 0;
    int m = 0;
    int y = 0;
};

struct ColorCorrection {
    CcZone shadows;
    CcZone mid;
    CcZone high;
};

// All editing state, ported 1:1 from the flat global variables in
// VisaHater — Фото на документы.html (lines 321-327). Plain struct, mutated
// imperatively by UI handlers followed by an explicit repaint — same
// pattern as iHateCards' AppState.
struct EditorState {
    // Format parameters (the "Формат"/"Параметры формата" panel).
    double widthMm = 35, heightMm = 45, topMarginMm = 5;
    double headPct = 75.6;    // % of height the head occupies — the source of truth
    double headSizeMm = 34;   // derived from headPct
    double botMarginMm = 6;   // derived from topMargin/headSize
    int dpi = 300;
    std::string formatKey = "passport_rf";
    bool lockVertical = true;

    Guide guide;
    std::string bgColorHex = "#ffffff";
    ColorCorrection cc;

    int brightness = 0;    // -100..100
    int contrast = 0;      // -100..100
    int gammaPercent = 100; // 20..300, ratio = gammaPercent/100
    int saturationPercent = 100;  // 0..300
    bool blackAndWhite = false;
    bool ovalOverlay = false;
    bool cornerOverlay = false;

    // Print composer (line 253-262).
    int printDpi = 300;
    double printMarginMm = 3;
    double printGapMm = 2;
    int printPhotoCount = 6;  // 2/4/6/8
    bool printBorder = false;
};

namespace EditorEngine {

// Ported from recomputeDerivedParams() (line 346): headSize/botMargin
// follow height/topMargin/headPct so the four numbers can't drift into a
// geometrically inconsistent guide.
inline void recomputeDerivedParams(EditorState& s) {
    s.headSizeMm = std::round(s.heightMm * s.headPct / 100.0 * 10) / 10;
    s.botMarginMm = std::max(0.0, std::round((s.heightMm - s.topMarginMm - s.headSizeMm) * 10) / 10);
}

// Ported from onFormatChange() (line 353).
inline void applyFormat(EditorState& s, const std::string& key) {
    s.formatKey = key;
    const FormatPreset& f = FormatPresets::byKey(key);
    s.widthMm = f.widthMm;
    s.heightMm = f.heightMm;
    s.topMarginMm = f.topMarginMm;
    s.headPct = std::round(f.headSizeMm / f.heightMm * 1000) / 10;
    recomputeDerivedParams(s);
    s.ovalOverlay = f.oval;
    s.cornerOverlay = f.corner;
    s.guide = Guide{};
}

// Ported from onParamChange() (line 357) — width/height edited directly.
inline void onWidthOrHeightChange(EditorState& s) { recomputeDerivedParams(s); }

// Ported from onTopMarginChange() (line 358).
inline void onTopMarginChange(EditorState& s) {
    s.botMarginMm = std::max(0.0, std::round((s.heightMm - s.topMarginMm - s.headSizeMm) * 10) / 10);
}

// Ported from onHeadPctChange() (line 363).
inline void onHeadPctChange(EditorState& s) { recomputeDerivedParams(s); }

// Ported from onHeadSizeChange() (line 364).
inline void onHeadSizeChange(EditorState& s) {
    s.headPct = std::round(s.headSizeMm / s.heightMm * 1000) / 10;
    s.botMarginMm = std::max(0.0, std::round((s.heightMm - s.topMarginMm - s.headSizeMm) * 10) / 10);
}

// Ported from onBotMarginChange() (line 370).
inline void onBotMarginChange(EditorState& s) {
    s.headSizeMm = std::max(0.0, std::round((s.heightMm - s.topMarginMm - s.botMarginMm) * 10) / 10);
    s.headPct = std::round(s.headSizeMm / s.heightMm * 1000) / 10;
}

}  // namespace EditorEngine

}  // namespace ihv::core
