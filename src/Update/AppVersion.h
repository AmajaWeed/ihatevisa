#pragma once

#include <QString>
#include <algorithm>
#include <array>

#ifndef IHATEVISA_VERSION
#define IHATEVISA_VERSION "1.0.0"
#endif

namespace ihv::update::AppVersion {

inline QString current() { return QString::fromUtf8(IHATEVISA_VERSION); }

// win-x64 / osx-arm64 / osx-x64 — matches the updates.json package keys.
inline QString rid() {
#if defined(Q_OS_WIN)
    return "win-x64";
#elif defined(Q_OS_MAC)
#if defined(__aarch64__) || defined(__arm64__)
    return "osx-arm64";
#else
    return "osx-x64";
#endif
#else
    return "linux-x64";
#endif
}

// Parses "major.minor.patch[letter]" into 4 numeric components — a
// trailing letter on the 3rd part becomes the 4th slot (a=1, b=2, ...) so
// "1.0.0b" sorts strictly between "1.0.0" and "1.0.1". Real releases
// should stick to plain numeric versions; the letter parsing exists only
// as a legacy-compat fallback (matches iHateCards.NET's AppVersion).
inline std::array<int, 4> parse(const QString& v) {
    std::array<int, 4> parts{0, 0, 0, 0};
    QStringList segs = v.split('.');
    for (int i = 0; i < 3 && i < segs.size(); ++i) {
        QString seg = segs[i];
        if (!seg.isEmpty() && seg.back().isLetter()) {
            QChar letter = seg.back().toLower();
            parts[3] = letter.unicode() - QChar('a').unicode() + 1;
            seg.chop(1);
        }
        parts[i] = seg.toInt();
    }
    return parts;
}

inline int compare(const QString& a, const QString& b) {
    auto pa = parse(a), pb = parse(b);
    for (int i = 0; i < 4; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

inline bool isNewer(const QString& candidate, const QString& current) { return compare(candidate, current) > 0; }

}  // namespace ihv::update::AppVersion
