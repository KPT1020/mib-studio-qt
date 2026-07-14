#pragma once

#include <QString>

#include <cstdint>

class QSettings;

namespace frontend::processingcoresettings {

struct Selection {
    QString version;
    QString sha256;
    std::uint32_t contractVersion{0};
    std::uint32_t engineAbiVersion{0};
    QString runtimeFingerprint;
    QString releaseTag;
    QString manifestSha256;
    QString path;
    QString appMinVersion;
    QString appMaxVersion;
};

// Writes the complete explicit selection and synchronizes it before returning
// success. On a sync failure, the prior in-memory values are restored so a
// failed candidate never becomes the logical selection for this process.
bool persistSelection(QSettings& settings, const Selection& selection, QString* error = nullptr);

} // namespace frontend::processingcoresettings
