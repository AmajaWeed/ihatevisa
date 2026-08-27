#pragma once

#include <QString>
#include <vector>

#include "Core/EditorState.h"
#include "Core/PhotoDocument.h"

namespace ihv::project::HateFile {

inline constexpr int FormatVersion = 1;

struct Project {
    core::EditorState state;
    std::vector<core::PhotoDocumentPtr> photos;
    int activePhotoId = -1;
};

// Saves a .hate project: a ZIP container (manifest.json / layout.json /
// print-settings.json / assets/photo-NNN.<ext> [+ -processed.png if
// background-removed]) — same approach as .docx/.pptx, per the iHateVisa
// spec's Задача 3. Assets are stored uncompressed (already-compressed
// image formats) at their original bytes so re-export stays bit-identical
// across save/open cycles. Returns true on success.
bool save(const QString& path, const Project& project, QString* error = nullptr);

// Opens a .hate project. Tolerant of formatVersion > FormatVersion (opens
// what it understands) but rejects missing/unversioned manifests. Returns
// true on success.
bool open(const QString& path, Project& outProject, QString* error = nullptr);

}  // namespace ihv::project::HateFile
