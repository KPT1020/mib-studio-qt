#pragma once

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <QStringList>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace backend::services {
class SyringePumpService;
}

class QTimer;
class QVBoxLayout;
namespace Ui {
class SyringePumpTab;
}

namespace frontend {

class PumpRowWidget;

class SyringePumpTab : public QWidget {
    Q_OBJECT
public:
    using ReservedPortNamesProvider = std::function<QStringList()>;
    using PumpHandle = uint64_t;

    explicit SyringePumpTab(backend::services::SyringePumpService& pumpService,
                            ReservedPortNamesProvider reservedPortNamesProvider = {},
                            QWidget* parent = nullptr);
    ~SyringePumpTab();

private slots:
    void onAddPump();
    void onRemoveLastPump();
    void onUpdateStatus();

private:
    void ensureMinimumPumpCount(size_t minCount, const QStringList& defaultNames);
    void rebuildPumpRows();
    void wirePumpRow(PumpRowWidget* rowWidget);
    QStringList reservedPortNamesExcluding(PumpHandle selfHandle) const;
    void updatePumpUI(PumpRowWidget* rowWidget);
    bool connectPumpFromConfig(PumpRowWidget* rowWidget);

    bool loadJsonConfig(nlohmann::json& config) const;
    bool writeJsonConfig(const nlohmann::json& config) const;
    nlohmann::json readOrCreateConfig() const;
    nlohmann::json defaultPumpJson(const QString& name, int defaultAddress) const;
    void migrateLegacyConfig(nlohmann::json& config) const;
    void loadConfig();
    void saveConfig();
    QString configPath() const;

    Ui::SyringePumpTab* ui{nullptr};
    backend::services::SyringePumpService& pumpService_;
    ReservedPortNamesProvider reservedPortNamesProvider_;
    std::vector<std::unique_ptr<PumpRowWidget>> pumpRows_;
    QVBoxLayout* rowsLayout_{nullptr};
    QTimer* statusUpdateTimer_{nullptr};
};

} // namespace frontend
