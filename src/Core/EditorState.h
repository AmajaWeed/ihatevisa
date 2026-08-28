#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Core/FormatPresets.h"

namespace ihv::core {

// Position/scale/rotation of the photo relative to the format guide.
// Ported from the `guide` global (line 322). Rotation is derived from
// dragging the crown/chin dots off the vertical (see
// EditorRenderer::solveGuideFromDots) rather than a separate slider.
struct Guide {
    double x = 0;
    double y = 0;
    double scale = 1;
    double rotation = 0;
};

enum class CornerPosition { TopLeft, TopRight, BottomLeft, BottomRight };

// Camera-Raw-style tone adjustments, all -100..100 (0 = no change) unless
// noted. Replaces the earlier simple brightness/contrast/gamma/saturation
// + 3-way CMY corrector with the White Balance / Tone / Presence / Detail
// grouping used by Adobe Camera Raw and Lightroom's Basic panel.
struct RawAdjustments {
    // White balance
    int temperature = 0;  // warm (+, more red/less blue) <-> cool (-)
    int tint = 0;          // magenta (+) <-> green (-)

    // Tone
    int exposure = 0;    // +-2 EV at the extremes, applied in linear light
    int contrast = 0;
    int highlights = 0;  // luminance-weighted toward the bright end
    int shadows = 0;      // luminance-weighted toward the dark end
    int whites = 0;       // narrower/stronger than highlights, sets the white point
    int blacks = 0;        // narrower/stronger than shadows, sets the black point

    // Detail (local contrast via unsharp-mask at increasing blur radius)
    int texture = 0;   // small radius — fine detail
    int clarity = 0;    // medium radius — mid-frequency "punch"
    int dehaze = 0;      // larger radius + a haze-veil model

    // Presence
    int vibrance = 0;    // saturation boost weighted toward less-saturated pixels
    int saturation = 0;  // uniform saturation
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
    RawAdjustments adj;

    bool blackAndWhite = false;
    bool ovalOverlay = false;
    bool cornerOverlay = false;
    CornerPosition cornerPosition = CornerPosition::BottomRight;  // UI only offers BottomLeft/BottomRight
    double cornerSizeMm = 12;

    // Print composer (line 253-262).
    int printDpi = 300;
    double printMarginMm = 3;
    double printGapMm = 2;
    int printPhotoCount = 6;  // 2/4/6/8
    bool printBorder = false;
    bool printCenter = true;   // distribute leftover sheet space symmetrically instead of anchoring top-left
    double printOffsetXMm = 0; // manual nudge on top of centering, e.g. to dodge a printer's streaking band
    double printOffsetYMm = 0;
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
