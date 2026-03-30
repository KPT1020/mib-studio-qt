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

void HdfMetricsModel::setPixelToMicronFactor(double factor) {
    beginResetModel();
    pixelToMicronFactor_ = factor;
    endResetModel();
}

int HdfMetricsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return frames_ ? static_cast<int>(frames_->size()) : 0;
}

int HdfMetricsModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 17;
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
        case 4:  return QString::number(val.area * pixelToMicronFactor_ * pixelToMicronFactor_, 'f', 2);
        case 5:  return QString::number(val.areaRatio, 'f', 3);
        case 6:  return QString::number(val.ringRatio, 'f', 3);
        case 7:  return val.isValid ? "Yes" : "No";
        case 8:  return val.touchesBorder ? "Yes" : "No";
        case 9:  return val.hasSingleInnerContour ? "Yes" : "No";
        case 10: return val.inRange ? "Yes" : "No";
        case 11: return val.innerContourCount;
        case 12: return QString::number(val.brightness.q1, 'f', 2);
        case 13: return QString::number(val.brightness.q2, 'f', 2);
        case 14: return QString::number(val.brightness.q3, 'f', 2);
        case 15: return QString::number(val.brightness.q4, 'f', 2);
        case 16: return val.isTargetGroup ? "Yes" : "No";
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
            case 3:  return "Area (px)";
            case 4:  return QString::fromUtf8("Area (\xc2\xb5m\xc2\xb2)");
            case 5:  return "Area Ratio";
            case 6:  return "Ring Ratio";
            case 7:  return "Valid";
            case 8:  return "Touches Border";
            case 9:  return "Single Inner";
            case 10: return "In Range";
            case 11: return "Inner Count";
            case 12: return "Bright Q1";
            case 13: return "Bright Q2";
            case 14: return "Bright Q3";
            case 15: return "Bright Q4";
            case 16: return "Target Group";
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







