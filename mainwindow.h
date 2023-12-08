#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <filesystem>
#include <map>

#include <QFont>
#include <QMainWindow>
#include <QMap>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QErrorMessage>

#include "plist_object.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QXmlStreamReader;
class QTableWidgetItem;
class QTreeWidgetItem;
class QTimer;

struct PathInfo {
    std::filesystem::file_status status;
    QMap<QString, QByteArray> attributes;
};

struct MachineInfo {
    QMap<QString, QByteArray> attributes;
    QSet<QString> destinations;
};

class MainWindow : public QMainWindow
{
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void closeEvent(QCloseEvent *event) override;

    void readSettings();
    void updateMountPointsView(
        const std::map<std::string, plist_dict>& mountPoints);
    void updatePathInfo(const std::string& pathName);
    void deleteSelectedBackups();
    void uniqueSizeSelectedPaths();
    void restoreSelectedPaths();
    void verifySelectedBackups();
    void showAboutDialog();
    void showSettingsDialog();
    void handleDirectoryReaderEnded(
        const std::filesystem::path& dir,
        std::error_code ec,
        const QSet<QString>& filenames);
    void reportDir(const std::filesystem::path& dir,
                   const QSet<QString>& filenames);
    void updateDirEntry(const std::filesystem::path& path,
                        const std::filesystem::file_status& status,
                        const QMap<QString, QByteArray>& attrs);
    void updateMachine(const std::string& name,
                       const QMap<QString, QByteArray>& attrs,
                       const plist_dict& dict);
    void updateBackups(const std::filesystem::path& path,
                       const QMap<QString, QByteArray>& attrs);
    void updateVolumes(const std::filesystem::path& path,
                       const QMap<QString, QByteArray>& attrs);
    void checkTmStatus();
    void showStatus(const QString& status);

    void addTreeWidgetItem(QTreeWidgetItem *parent,
                           const std::filesystem::path& path);
    void changeTreeWidgetItem(QTreeWidgetItem *parent,
                              const std::filesystem::path& path);

private:
    void selectedBackupsChanged();
    void handleQueryFailedToStart(const QString &text);
    void handleGotDestinations(int count);
    void handleTmutilPathChange(const QString &path);
    void handleSudoPathChange(const QString &path);
    void handleTmStatusNoPlist();
    void handleTmStatusReaderError(qint64 lineNumber,
                                   int error,
                                   const QString& text);
    void handleTmStatusFinished(int code, int status);
    void handleMachineItemChanged(QTableWidgetItem *item);
    void changeTmutilStatusInterval(int msecs);
    void changeTmutilDestinationsInterval(int msecs);
    void changePathInfoInterval(int msecs);

    QErrorMessage errorMessage;
    Ui::MainWindow *ui{};
    QTimer *destinationsTimer{};
    QTimer *statusTimer{};
    QTimer *pathInfoTimer{};
    QString tmutilPath;
    QString sudoPath;
    QFont fixedFont;
    std::map<std::string, plist_dict> mountMap;
    std::map<std::string, MachineInfo> machineMap;
    std::map<std::filesystem::path, PathInfo> pathInfoMap;
};

#endif // MAINWINDOW_H
