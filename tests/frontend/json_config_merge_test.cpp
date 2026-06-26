// json_config_merge_test
//
// Pins down the config deep-merge used on app start (frontend::jsonutil::
// mergeMissingDefaults). This is what keeps an updated install from drifting
// away from a fresh install: keys a newer build adds to the bundled defaults
// must be filled into the user's existing config.json WITHOUT overwriting any
// value the user already set.

#include "frontend/utils/JsonConfigMerge.h"

#include "support/assert.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ju = frontend::jsonutil;

namespace {
QJsonObject obj(const char* json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}
} // namespace

int main()
{
    // 1) Missing top-level key is added; existing value is preserved.
    {
        QJsonObject user = obj(R"({"a": 1})");
        const QJsonObject defs = obj(R"({"a": 999, "b": 2})");
        const bool changed = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(changed, "merge reports a change when a key is added");
        MIB_EXPECT(user.value("a").toInt() == 1, "existing user value is NOT overwritten");
        MIB_EXPECT(user.value("b").toInt() == 2, "missing default key is added");
    }

    // 2) No change when the user already has every default key (even if values
    //    differ) -> the file should not be rewritten.
    {
        QJsonObject user = obj(R"({"a": 5, "b": 6})");
        const QJsonObject defs = obj(R"({"a": 1, "b": 2})");
        const bool changed = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(!changed, "no change when all keys present");
        MIB_EXPECT(user.value("a").toInt() == 5 && user.value("b").toInt() == 6,
                   "user values untouched");
    }

    // 3) Nested objects merge recursively: new nested key added, existing kept.
    {
        QJsonObject user = obj(R"({"image_processing": {"blur": 3}})");
        const QJsonObject defs =
            obj(R"({"image_processing": {"blur": 9, "threshold": 7}, "display_fps": 60})");
        const bool changed = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(changed, "nested + top-level additions report change");
        const QJsonObject ip = user.value("image_processing").toObject();
        MIB_EXPECT(ip.value("blur").toInt() == 3, "existing nested value preserved");
        MIB_EXPECT(ip.value("threshold").toInt() == 7, "missing nested key added");
        MIB_EXPECT(user.value("display_fps").toInt() == 60, "new top-level key added");
    }

    // 4) Type mismatch (user scalar where default is object) keeps the user's
    //    value rather than clobbering it.
    {
        QJsonObject user = obj(R"({"roi": "custom"})");
        const QJsonObject defs = obj(R"({"roi": {"x": 0, "y": 0}})");
        const bool changed = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(!changed, "type mismatch is left alone (no change)");
        MIB_EXPECT(user.value("roi").isString() && user.value("roi").toString() == "custom",
                   "user's scalar value preserved over default object");
    }

    // 5) Arrays are atomic: a user-set array is never merged element-wise.
    {
        QJsonObject user = obj(R"({"list": [1, 2]})");
        const QJsonObject defs = obj(R"({"list": [9, 9, 9]})");
        const bool changed = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(!changed, "existing array key is not changed");
        MIB_EXPECT(user.value("list").toArray().size() == 2, "user array preserved verbatim");
    }

    // 6) Empty user config gets fully populated from defaults.
    {
        QJsonObject user;
        const QJsonObject defs = obj(R"({"a": 1, "nested": {"b": 2}})");
        const bool changed = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(changed, "empty config is populated");
        MIB_EXPECT(user.value("a").toInt() == 1, "scalar default copied");
        MIB_EXPECT(user.value("nested").toObject().value("b").toInt() == 2,
                   "nested default copied");
    }

    // 7) Idempotence: merging twice yields no further change.
    {
        QJsonObject user = obj(R"({"a": 1})");
        const QJsonObject defs = obj(R"({"a": 1, "b": 2})");
        ju::mergeMissingDefaults(user, defs);
        const bool changedAgain = ju::mergeMissingDefaults(user, defs);
        MIB_EXPECT(!changedAgain, "second merge is a no-op");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("JSON config deep-merge (add-missing, preserve-existing) verified\n");
    }
    return mib::test::exitCode();
}
