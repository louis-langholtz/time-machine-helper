#ifndef DIRECTORYREADER_H
#define DIRECTORYREADER_H

#include <filesystem>

#include <QByteArray>
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
    void entry(QTreeWidgetItem *item,
               const QMap<QString, QByteArray>& attrs,
               const std::filesystem::path& path,
               const std::filesystem::file_status& status);
    void ended(QTreeWidgetItem *item, std::error_code ec);

private:
    void run() override;

    QTreeWidgetItem *item{};
};

#endif // DIRECTORYREADER_H
