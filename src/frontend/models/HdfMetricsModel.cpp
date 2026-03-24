#include "frontend/models/HdfMetricsModel.h"

#include "backend/services/ProcessingService.h"

namespace frontend {

HdfMetricsModel::HdfMetricsModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void HdfMetricsModel::setSource(const std::vector<backend::services::ProcessedFrame>* framesPtr) {
    beginResetModel();
    frames_ = framesPtr;
    endResetModel();
}

int HdfMetricsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return frames_ ? static_cast<int>(frames_->size()) : 0;
}

int HdfMetricsModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 15; // same columns as previous table
}

QVariant HdfMetricsModel::data(const QModelIndex& index, int role) const {
    if (!frames_ || !index.isValid() || role != Qt::DisplayRole) return {};
    const auto& frame = frames_->at(static_cast<size_t>(index.row()));
    const auto& val = frame.validation;
    switch (index.column()) {
        case 0:  return static_cast<qulonglong>(frame.index);
        case 1:  return static_cast<qulonglong>(frame.timestampNs);
        case 2:  return QString::number(val.deformability, 'f', 3);
        case 3:  return QString::number(val.area, 'f', 2);
        case 4:  return QString::number(val.areaRatio, 'f', 3);
        case 5:  return QString::number(val.ringRatio, 'f', 3);
        case 6:  return val.isValid ? "Yes" : "No";
        case 7:  return val.touchesBorder ? "Yes" : "No";
        case 8:  return val.hasSingleInnerContour ? "Yes" : "No";
        case 9:  return val.inRange ? "Yes" : "No";
        case 10: return val.innerContourCount;
        case 11: return QString::number(val.brightness.q1, 'f', 2);
        case 12: return QString::number(val.brightness.q2, 'f', 2);
        case 13: return QString::number(val.brightness.q3, 'f', 2);
        case 14: return QString::number(val.brightness.q4, 'f', 2);
        default: return {};
    }
}

QVariant HdfMetricsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case 0:  return "Index";
            case 1:  return "Timestamp";
            case 2:  return "Deformability";
            case 3:  return "Area";
            case 4:  return "Area Ratio";
            case 5:  return "Ring Ratio";
            case 6:  return "Valid";
            case 7:  return "Touches Border";
            case 8:  return "Single Inner";
            case 9:  return "In Range";
            case 10: return "Inner Count";
            case 11: return "Bright Q1";
            case 12: return "Bright Q2";
            case 13: return "Bright Q3";
            case 14: return "Bright Q4";
            default: return {};
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags HdfMetricsModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace frontend







