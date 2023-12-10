#ifndef ITEMDEFAULTS_H
#define ITEMDEFAULTS_H

#include <optional>

#include <QTableWidget>

struct ItemDefaults {
    Qt::ItemFlags flags{Qt::ItemIsEnabled};
    Qt::Alignment alignment{Qt::AlignCenter};
    std::optional<QFont> font{};
    std::optional<bool> checked{};

    constexpr auto use(const Qt::ItemFlags &v)
        -> ItemDefaults&
    {
        this->flags = v;
        return *this;
    }

    constexpr auto use(const Qt::Alignment &v)
        -> ItemDefaults&
    {
        this->alignment = v;
        return *this;
    }

    constexpr auto use(const std::optional<QFont> &v)
        -> ItemDefaults&
    {
        this->font = v;
        return *this;
    }

    constexpr auto use(const std::optional<bool> &v)
        -> ItemDefaults&
    {
        this->checked = v;
        return *this;
    }
};

auto createdItem(QTableWidget* parent,
                 int row, int column,
                 const ItemDefaults& defaults = ItemDefaults{})
    -> QTableWidgetItem *;

#endif // ITEMDEFAULTS_H
