#pragma once

#include <QAbstractTableModel>
#include <vector>

namespace backend::services { struct ProcessedFrame; }

namespace frontend {

class HdfMetricsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit HdfMetricsModel(QObject* parent = nullptr);

    void setSource(const std::vector<backend::services::ProcessedFrame>* framesPtr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    const std::vector<backend::services::ProcessedFrame>* frames_{nullptr};
};

} // namespace frontend





