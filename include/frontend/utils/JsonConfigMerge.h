// Deep-merge helper for reconciling a user's on-disk config.json with the
// bundled defaults shipped in a new build. Pure QtCore logic, no I/O, so it can
// be unit tested without the app.
#pragma once

#include <QJsonObject>

namespace frontend::jsonutil {

// Recursively add keys present in `defaults` but missing from `target`.
//
// - Existing keys in `target` are NEVER overwritten (user values win).
// - When both sides hold an object for the same key, they are merged
//   recursively, so newly-added nested keys are filled in while existing nested
//   values are preserved.
// - Arrays and scalars are treated atomically: copied only when the key is
//   absent, otherwise left exactly as the user had them.
//
// Returns true if `target` was modified. This lets an updated install converge
// to the same effective config as a fresh install for any keys introduced by a
// newer build, without discarding the user's customizations.
bool mergeMissingDefaults(QJsonObject& target, const QJsonObject& defaults);

} // namespace frontend::jsonutil
