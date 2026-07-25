// UX-6/UX-10 (epic #304): readiness/override provenance and config JSON must
// round-trip through the HDF5 experiment file, and legacy files without the
// attributes must read back as "not recorded" (false) rather than erroring.
#include "backend/processing/ProcessingService.h"
#include "backend/recording/Hdf5Service.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <string>

int main()
{
    mib::test::TempDir tempDir("hdf_provenance");

    const std::string path = (tempDir / "provenance.h5").string();
    backend::services::Hdf5Service hdf5;
    MIB_REQUIRE(hdf5.initialize(tempDir.path().string()), "hdf5 init");
    MIB_REQUIRE(hdf5.openFile(path), "open file");
    MIB_REQUIRE(hdf5.initializeDatasets(), "init datasets");

    // Provenance attributes require /experiment_info to exist first.
    std::string out;
    MIB_EXPECT(!hdf5.writeReadinessJson("{}"),
               "readiness write before writeExperimentInfo is rejected");

    backend::services::ProcessingConfig config{};
    backend::services::ProcessingService::Roi roi{10, 20, 100, 50};
    MIB_REQUIRE(hdf5.writeExperimentInfo(1000, 2000, 0, 0, config, roi, nullptr, nullptr),
                "writeExperimentInfo");

    const std::string configJson = R"({"processing":{"area_threshold_min":55}})";
    const std::string readinessJson =
        R"({"schema_version":1,"operator":"jdoe","override":{"reason":"demo"}})";
    MIB_REQUIRE(hdf5.writeConfigJson(configJson), "writeConfigJson");
    MIB_REQUIRE(hdf5.writeReadinessJson(readinessJson), "writeReadinessJson");
    hdf5.closeFile();

    // Read back.
    MIB_REQUIRE(hdf5.loadFile(path), "reload file");
    out.clear();
    MIB_EXPECT(hdf5.readConfigJson(out) && out == configJson,
               "config JSON round-trips byte-exact");
    out.clear();
    MIB_EXPECT(hdf5.readReadinessJson(out) && out == readinessJson,
               "readiness JSON round-trips byte-exact");
    hdf5.closeFile();

    // Legacy file: experiment info but no provenance attributes.
    const std::string legacyPath = (tempDir / "legacy.h5").string();
    MIB_REQUIRE(hdf5.openFile(legacyPath), "open legacy file");
    MIB_REQUIRE(hdf5.initializeDatasets(), "legacy init datasets");
    MIB_REQUIRE(hdf5.writeExperimentInfo(1, 2, 0, 0, config, roi, nullptr, nullptr),
                "legacy writeExperimentInfo");
    hdf5.closeFile();
    MIB_REQUIRE(hdf5.loadFile(legacyPath), "reload legacy");
    out.clear();
    MIB_EXPECT(!hdf5.readReadinessJson(out) && out.empty(),
               "legacy file reads back as not-recorded, not an error");
    MIB_EXPECT(!hdf5.readConfigJson(out) && out.empty(),
               "legacy config json also not-recorded");
    hdf5.closeFile();

    return mib::test::exitCode();
}
