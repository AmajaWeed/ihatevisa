#pragma once

#include <array>
#include <string>

namespace ihv::core {

// One document-photo format preset. Ported 1:1 from the FORMATS table in
// VisaHater — Фото на документы.html (lines 310-317).
struct FormatPreset {
    std::string key;
    std::string name;
    double widthMm;
    double heightMm;
    double topMarginMm;
    double headSizeMm;
    std::string description;
    bool oval;
    bool corner;
};

namespace FormatPresets {

inline const FormatPreset PassportRf{
    "passport_rf", "Паспорт РФ (2013)", 35, 45, 5, 34, "35x45 мм · Голова 32-36 мм", false, false};
inline const FormatPreset ZagranRf{"zagran_rf",   "Загранпаспорт РФ", 35, 45, 5, 34,
                                    "35x45 мм · Овальная виньетка · Фон белый", true, false};
inline const FormatPreset Schengen{
    "schengen", "Шенген виза", 35, 45, 4, 34, "35x45 мм · Голова 70-80% · Без овала/уголка", false, false};
inline const FormatPreset Format3x4{"format_3x4", "Фото 3x4 см", 30, 40, 2, 25, "30x40 мм · Стандартное фото",
                                     false, false};
inline const FormatPreset Format25x35{"format_25x35", "Фото 2.5x3.5 см", 25, 35, 4, 26,
                                       "25x35 мм · Стандартное фото", false, false};
inline const FormatPreset Custom{"custom", "Индивидуальный", 35, 45, 5, 34, "Свои размеры", false, false};

inline const std::array<FormatPreset, 6> All{PassportRf, ZagranRf, Schengen, Format3x4, Format25x35, Custom};

inline const FormatPreset& byKey(const std::string& key) {
    for (const auto& f : All)
        if (f.key == key) return f;
    return PassportRf;
}

}  // namespace FormatPresets

}  // namespace ihv::core
