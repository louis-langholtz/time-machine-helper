#ifndef ITEMDEFAULTS_H
#define ITEMDEFAULTS_H

#include <optional>

#include <QTableWidget>

struct ItemDefaults {
    Qt::ItemFlags flags{Qt::ItemIsEnabled};
    Qt::Alignment alignment{Qt::AlignCenter};
    std::optional<QFont> font{};
    std::optional<Qt::CheckState> checked{};
    std::optional<QString> text_{};

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

    constexpr auto use(const std::optional<Qt::CheckState> &v)
        -> ItemDefaults&
    {
        this->checked = v;
        return *this;
    }

    constexpr auto text(const QString& v)
        -> ItemDefaults&
    {
        this->text_ = v;
        return *this;
    }
};

auto createdItem(QTableWidget* parent,
                 int row, int column,
                 const ItemDefaults& defaults = ItemDefaults{})
    -> QTableWidgetItem *;

#endif // ITEMDEFAULTS_H
