#include "frontend/models/JsonTableModel.h"

namespace frontend {

JsonTableModel::JsonTableModel(QObject* parent)
	: QAbstractTableModel(parent) {}

void JsonTableModel::clear() {
	beginResetModel();
	columnNames_.clear();
	cellData_.clear();
	endResetModel();
}

void JsonTableModel::setFromFlatten(const QStringList& columns, const QVector<QVector<QString>>& rows) {
	beginResetModel();
	columnNames_ = columns;
	cellData_ = rows;
	endResetModel();
}

int JsonTableModel::rowCount(const QModelIndex& parent) const {
	if (parent.isValid()) return 0;
	return cellData_.size();
}

int JsonTableModel::columnCount(const QModelIndex& parent) const {
	if (parent.isValid()) return 0;
	return columnNames_.size();
}

QVariant JsonTableModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid()) return {};
	if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
	const int r = index.row();
	const int c = index.column();
	if (r < 0 || r >= cellData_.size()) return {};
	const auto& row = cellData_.at(r);
	if (c < 0 || c >= row.size()) return {};
	return row.at(c);
}

QVariant JsonTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (role != Qt::DisplayRole) return {};
	if (orientation == Qt::Horizontal) {
		if (section >= 0 && section < columnNames_.size()) return columnNames_.at(section);
		return {};
	}
	return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags JsonTableModel::flags(const QModelIndex& index) const {
	if (!index.isValid()) return Qt::NoItemFlags;
	Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	// Editability: if two-column key/value, only value column editable; otherwise all editable
	if (columnNames_.size() == 2 && columnNames_.at(0) == "key" && columnNames_.at(1) == "value") {
		if (index.column() == 1) f |= Qt::ItemIsEditable;
	} else {
		f |= Qt::ItemIsEditable;
	}
	return f;
}

bool JsonTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
	if (!index.isValid() || role != Qt::EditRole) return false;
	const int r = index.row();
	const int c = index.column();
	if (r < 0 || r >= cellData_.size()) return false;
	auto& row = cellData_[r];
	if (c < 0 || c >= row.size()) return false;
	const QString newText = value.toString();
	if (row[c] == newText) return true;
	row[c] = newText;
	emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
	return true;
}

} // namespace frontend





