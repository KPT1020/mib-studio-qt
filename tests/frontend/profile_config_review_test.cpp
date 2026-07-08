#include "frontend/utils/ProfileConfigReview.h"

#include "support/assert.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

namespace {

QJsonDocument doc(const char* json)
{
    return QJsonDocument::fromJson(QByteArray(json));
}

const frontend::configreview::ReviewRow* findRow(const QVector<frontend::configreview::ReviewRow>& rows,
                                                 const QString& setting)
{
    for (const auto& row : rows) {
        if (row.setting == setting) {
            return &row;
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    const QJsonDocument current = doc(R"({
        "image_processing": {
            "area_threshold_min": 60,
            "filters": {
                "enable_area_range_check": true,
                "enable_ring_ratio_check": false
            },
            "multi_image": {
                "enabled": false,
                "count": 3
            }
        },
        "target_fps": 120,
        "unchanged_top": "same"
    })");
    const QJsonDocument profile = doc(R"({
        "image_processing": {
            "area_threshold_min": 75,
            "filters": {
                "enable_area_range_check": true,
                "enable_ring_ratio_check": true
            },
            "multi_image": {
                "enabled": true,
                "count": 3
            }
        },
        "target_fps": 120,
        "new_profile_key": "added",
        "unchanged_top": "same"
    })");

    const auto result = frontend::configreview::buildReviewRows(current, profile);
    MIB_EXPECT(result.totalCount == 8, "all nested scalar settings are represented");
    MIB_EXPECT(result.changedCount == 4, "only changed, added, or removed values count as changed");

    const auto* area = findRow(result.rows, QStringLiteral("image_processing.area_threshold_min"));
    MIB_REQUIRE(area != nullptr, "nested changed setting appears");
    MIB_EXPECT(area->changed, "nested changed setting is marked changed");
    MIB_EXPECT(area->currentValue == QStringLiteral("60"), "current value is shown");
    MIB_EXPECT(area->profileValue == QStringLiteral("75"), "profile value is shown");
    MIB_EXPECT(area->section == QStringLiteral("image_processing"), "section is the top-level config object");

    const auto* unchanged = findRow(result.rows, QStringLiteral("image_processing.multi_image.count"));
    MIB_REQUIRE(unchanged != nullptr, "unchanged nested setting appears for all-settings mode");
    MIB_EXPECT(!unchanged->changed, "unchanged setting is available but not marked changed");

    const auto* added = findRow(result.rows, QStringLiteral("new_profile_key"));
    MIB_REQUIRE(added != nullptr, "profile-only value appears");
    MIB_EXPECT(added->changed, "profile-only value is changed");
    MIB_EXPECT(added->currentValue == QStringLiteral("<missing>"), "missing current value is explicit");
    MIB_EXPECT(added->profileValue == QStringLiteral("added"), "added profile value is shown");

    if (mib::test::exitCode() == 0) {
        std::printf("Profile config review rows verified\n");
    }
    return mib::test::exitCode();
}
