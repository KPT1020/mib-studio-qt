// Regression: the metrics CSV export must surface Young's modulus (from the
// LUT). Before the fix the column was absent entirely; a NaN value (query
// outside LUT coverage) must render as an empty field, not the literal "nan".
#include "frontend/utils/HdfReviewExportPaths.h"

#include "backend/processing/ProcessingService.h"

#include "support/assert.h"

#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <limits>

namespace paths = frontend::hdfreviewexport;

namespace {

QString lastField(const QString& row)
{
    const QStringList fields = row.split(',');
    return fields.isEmpty() ? QString() : fields.last();
}

} // namespace

int main()
{
    const QString header = paths::metricsCsvHeader();
    MIB_EXPECT(header.contains("Young's Modulus (kPa)"),
               "header advertises the Young's modulus column");
    MIB_EXPECT(header.endsWith("Young's Modulus (kPa)"),
               "Young's modulus is the final metrics column");

    // The header and every row must have the same number of columns.
    const int headerColumns = header.split(',').size();

    // Finite Young's modulus -> numeric field with 3 decimals.
    {
        backend::services::FilterResult val{};
        val.deformability = 0.25;
        val.area = 100.0;
        val.youngsModulus = 12.34;
        const QString row = paths::metricsCsvRow("Valid", val, /*index=*/7,
                                                 /*timestampNs=*/1234, /*conversionFactor=*/0.4886);
        MIB_EXPECT(row.split(',').size() == headerColumns,
                   "finite row column count matches header");
        MIB_EXPECT(lastField(row) == "12.340",
                   "finite Young's modulus renders with 3 decimals");
        MIB_EXPECT(row.startsWith("Valid,7,1234,"),
                   "row carries frame type, index and timestamp");
    }

    // NaN Young's modulus (outside LUT coverage) -> empty trailing field.
    {
        backend::services::FilterResult val{};
        val.youngsModulus = std::numeric_limits<double>::quiet_NaN();
        const QString row = paths::metricsCsvRow("Invalid", val, /*index=*/0,
                                                 /*timestampNs=*/0, /*conversionFactor=*/0.4886);
        MIB_EXPECT(row.split(',').size() == headerColumns,
                   "NaN row column count matches header");
        MIB_EXPECT(lastField(row).isEmpty(),
                   "NaN Young's modulus renders as an empty field");
        MIB_EXPECT(!row.contains("nan") && !row.contains("NaN"),
                   "NaN is never written literally");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("HDF review metrics CSV Young's modulus column verified\n");
    }
    return mib::test::exitCode();
}
