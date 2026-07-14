#include "backend/app/AppBackend.h"
#include "backend/processing/ProcessingService.h"
#include "frontend/dialogs/ProcessingCoreDialog.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <QApplication>
#include <QLabel>

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    qputenv("MIB_DISABLED_SERVICES", QByteArrayLiteral("all"));
    qputenv("MIB_STUDIO_PROCESSING_CORE_BASE_URL",
            QByteArrayLiteral("http://invalid-registry.example"));
    // Keep the LUT catalog offline: AppBackend::initialize resolves the
    // manifest through a synchronous QEventLoop fetch, and the Windows
    // release runner's proxy resolution can stall past the CTest timeout.
    qputenv("MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL",
            QByteArrayLiteral("file:///nonexistent/mib-lut-manifest.json"));

    QApplication app(argc, argv);
    mib::test::TempDir dataRoot("processing_core_dialog");
    backend::AppBackend backend;
    MIB_REQUIRE(backend.initialize(dataRoot.path().string()),
                "backend initializes with all optional services disabled");

    const auto localCore = backend.processing().activeProcessingCoreIdentity();
    frontend::ProcessingCoreDialog dialog(backend);
    const auto labels = dialog.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);

    bool renderedLocalCore = false;
    bool renderedRegistryFailure = false;
    for (const auto* label : labels) {
        const QString text = label->text();
        renderedLocalCore = renderedLocalCore ||
                            (text.contains(QStringLiteral("Active core:")) &&
                             text.contains(QString::fromStdString(localCore.version)) &&
                             text.contains(QString::number(localCore.contractVersion)) &&
                             text.contains(QString::number(localCore.engineAbiVersion)) &&
                             text.contains(QString::fromStdString(localCore.source)));
        renderedRegistryFailure = renderedRegistryFailure ||
                                  text.contains(QStringLiteral("must use HTTPS"));
    }

    MIB_EXPECT(renderedLocalCore,
               "dialog renders the local active-core identity before registry success");
    MIB_EXPECT(renderedRegistryFailure,
               "registry failure remains visible alongside the local active core");
    return mib::test::exitCode();
}
