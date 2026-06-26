#include "frontend/utils/JsonConfigMerge.h"

namespace frontend::jsonutil {

bool mergeMissingDefaults(QJsonObject& target, const QJsonObject& defaults)
{
    bool changed = false;
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        const QString& key = it.key();
        const QJsonValue defaultValue = it.value();

        if (!target.contains(key)) {
            // Key introduced by a newer build: add it with the shipped default.
            target.insert(key, defaultValue);
            changed = true;
            continue;
        }

        // Key present on both sides. Recurse only when both are objects so new
        // nested keys are filled in; otherwise keep the user's value untouched.
        if (defaultValue.isObject() && target.value(key).isObject()) {
            QJsonObject child = target.value(key).toObject();
            if (mergeMissingDefaults(child, defaultValue.toObject())) {
                target.insert(key, child);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace frontend::jsonutil
