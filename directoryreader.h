#ifndef DIRECTORYREADER_H
#define DIRECTORYREADER_H

#include <filesystem>

#include <QByteArray>
#include <QDir>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QThread>

class QTreeWidgetItem;

class DirectoryReader: public QThread
{
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    DirectoryReader(std::filesystem::path dir,
                    QObject *parent = nullptr);
    ~DirectoryReader() override;

    [[nodiscard]] auto filter() const noexcept
        -> QDir::Filters;
    void setFilter(QDir::Filters filters);

signals:
    void entry(const std::filesystem::path& path,
               const std::filesystem::file_status& status,
               const QMap<QString, QByteArray>& attrs);
    void ended(const std::filesystem::path& dir,
               std::error_code ec,
               const QSet<QString>& filenames);

private:
    void run() override;

    std::filesystem::path directory;
    QDir::Filters filters{QDir::Dirs|QDir::NoSymLinks};
};

#endif // DIRECTORYREADER_H
