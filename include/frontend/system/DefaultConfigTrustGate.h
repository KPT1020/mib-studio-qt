#pragma once

#include <QString>

namespace frontend {

class DefaultConfigTrustGate {
public:
    enum class ProductionAction {
        ExperimentStart,
        FrameRecordingStart,
        CameraApply,
    };

    struct State {
        QString activeConfigPath;
        QString defaultConfigPath;
        QString activeDefaultHash;
        bool hasExternalConfig{false};
        bool hasActiveProfile{false};
        bool usingDefaultConfig{false};
        bool defaultHashConfirmed{false};

        bool trustedForProduction() const { return !usingDefaultConfig || defaultHashConfirmed; }
    };

    State state() const;
    bool isProductionActionAllowed(ProductionAction action, QString* messageOut = nullptr) const;
    bool confirmActiveDefault(QString* hashOut = nullptr, QString* errorOut = nullptr) const;

    static QString activeConfigPath();
    static QString defaultConfigPath();
    static QString defaultConfigHash(QString* errorOut = nullptr);
    static QString blockMessage(ProductionAction action);
};

} // namespace frontend
