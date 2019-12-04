#ifndef VSRTL_REGISTERMODEL_H
#define VSRTL_REGISTERMODEL_H

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>

#include "vsrtl_netlistmodelbase.h"
#include "vsrtl_register.h"
#include "vsrtl_treeitem.h"

namespace vsrtl {
namespace core {
class RegisterBase;
class Design;
}  // namespace core
using namespace core;

class RegisterTreeItem : public NetlistTreeItem {
public:
    RegisterTreeItem(TreeItem* parent) : NetlistTreeItem(parent) {}

    enum class PortDirection { Input, Output };
    QVariant data(int column, int role = Qt::EditRole) const override;
    bool setData(int column, const QVariant& value, int role = Qt::EditRole) override;
    QList<QMenu*> getActions() const override;
    void setRegister(RegisterBase* reg);

    RegisterBase* m_register = nullptr;
};

class RegisterModel : public NetlistModelBase<RegisterTreeItem> {
    Q_OBJECT

public:
    enum columns { ComponentColumn, ValueColumn, WidthColumn, NUM_COLUMNS };
    RegisterModel(Design& arch, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

public slots:
    void invalidate() override;

private:
    void loadDesign(RegisterTreeItem* parent, const Design& component);
};

}  // namespace vsrtl

#endif  // VSRTL_REGISTERMODEL_H
