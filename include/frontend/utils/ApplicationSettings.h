#pragma once

#include <QString>

namespace frontend::applicationsettings {

// Establishes the stable QSettings identity used by every desktop entrypoint
// and copies any keys written by older builds under Qt's fallback
// "Unknown Organization" namespace. Existing values in the stable namespace
// always win, so the migration is safe to run on every startup.
bool initialize(QString* error = nullptr);

} // namespace frontend::applicationsettings
