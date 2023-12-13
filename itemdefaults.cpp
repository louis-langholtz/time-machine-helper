#include "itemdefaults.h"

auto createdItem(QTableWidget* parent,
                 int row, int column,
                 const ItemDefaults& defaults)
    -> QTableWidgetItem *
{
    auto item = parent->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        item->setFlags(defaults.flags);
        item->setTextAlignment(defaults.alignment);
        if (defaults.font) {
            item->setFont(*defaults.font);
        }
        if (defaults.checked) {
            item->setCheckState(*(defaults.checked)
                                    ? Qt::CheckState::Checked
                                    : Qt::CheckState::Unchecked);
        }
        if (defaults.text_) {
            item->setText(*defaults.text_);
        }
        parent->setItem(row, column, item);
    }
    return item;
}
