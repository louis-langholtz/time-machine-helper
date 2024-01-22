#ifndef DIRECTORYREADER_H
#define DIRECTORYREADER_H

#include <filesystem>

#include <QByteArray>
#include <QDir>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QRunnable>
#include <QAtomicInteger>

class QTreeWidgetItem;

class DirectoryReader: public QObject, public QRunnable
{
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    DirectoryReader(std::filesystem::path dir,
                    QObject *parent = nullptr);
    ~DirectoryReader() override;

    auto path() const -> std::filesystem::path;

    [[nodiscard]] auto filter() const noexcept
        -> QDir::Filters;
    [[nodiscard]] auto readAttributes() const noexcept
        -> bool;

    auto isRunning() const noexcept -> bool;
    auto isInterruptionRequested() const noexcept -> bool;

    void requestInterruption();

    void setFilter(QDir::Filters filters);
    void setReadAttributes(bool value);

signals:
    void entry(const std::filesystem::path &path,
               const std::filesystem::file_status &status,
               const QMap<QString, QByteArray> &attrs);
    void ended(const std::filesystem::path &dir,
               std::error_code ec,
               const QSet<QString> &filenames);

private:
    void run() override;
    void read();
    void read(const std::filesystem::directory_iterator &it);
    auto read(const std::filesystem::directory_entry &dirEntry,
              QSet<QString> &filenames) -> bool;

    QAtomicInteger<bool> running{};
    QAtomicInteger<bool> interrupt{};

    std::filesystem::path directory;
    QDir::Filters filters{QDir::Dirs|QDir::NoSymLinks};
    bool readAttrs{true};
};

#endif // DIRECTORYREADER_H
