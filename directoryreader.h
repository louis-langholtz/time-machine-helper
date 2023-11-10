#ifndef DIRECTORYREADER_H
#define DIRECTORYREADER_H

#include <QMap>
#include <QPair>
#include <QString>
#include <QThread>
#include <QVariant>

class QTreeWidgetItem;

class DirectoryReader: public QThread
{
    Q_OBJECT
public:
    DirectoryReader(QTreeWidgetItem *i,
                    QObject *parent = nullptr);

signals:
    void ended(QTreeWidgetItem *item,
               const QMap<int, QString>& textMap,
               int error);
    void entry(QTreeWidgetItem *item,
               const QMap<int, QString>& textMap,
               const QMap<int, QPair<int, QVariant>>& dataMap);

private:
    void run() override;

    QTreeWidgetItem *item{};
};

#endif // DIRECTORYREADER_H
