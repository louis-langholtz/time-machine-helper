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
    void handleDirectoryReaderEntry(const std::filesystem::path& path,
                        const std::filesystem::file_status& status,
                        const QMap<QString, QByteArray>& attrs);
    void updateMachines(const std::string& name,
                       const QMap<QString, QByteArray>& attrs,
                       const plist_dict& dict);
    void updateBackups(const std::filesystem::path& path,
                       const QMap<QString, QByteArray>& attrs);
    void updateVolumes(const std::filesystem::path& path,
                       const QMap<QString, QByteArray>& attrs);
    void checkTmStatus();
    void checkTmDestinations();
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

    void handleGotDestinations(
        const std::vector<plist_dict>& destinations);
    void handleGotDestinations(const plist_array &plist);
    void handleGotDestinations(const plist_dict &plist);
    void handleTmDestinations(const plist_object &plist);
    void handleTmDestinationsError(int error, const QString &text);
    void handleTmDestinationsReaderError(qint64 lineNumber,
                                         int error,
                                         const QString& text);

    void handleTmStatus(const plist_object &plist);
    void handleTmStatusNoPlist();
    void handleTmStatusReaderError(qint64 lineNumber,
                                   int error,
                                   const QString& text);
    void handlePlistProcessFinished(
        const QString& program,
        const QStringList& arguments,
        int code,
        int status);

    void handleItemChanged(QTableWidgetItem *item);
    void changeTmutilStatusInterval(int msecs);
    void changeTmutilDestinationsInterval(int msecs);
    void changePathInfoInterval(int msecs);
    void updateStorageDir(const std::filesystem::path& dir,
                          const QSet<QString>& filenames);
    void updateMachineDir(const std::filesystem::path& dir,
                          const QSet<QString>& filenames);
    void updateVolumeDir(const std::filesystem::path& dir,
                         const QSet<QString>& filenames);

    QErrorMessage errorMessage;
    Ui::MainWindow *ui{};
    QTimer *destinationsTimer{};
    QTimer *statusTimer{};
    QTimer *pathInfoTimer{};
    QString tmutilPath;
    QString sudoPath;
    QFont fixedFont;
    std::map<std::string, plist_dict> mountMap;
    std::map<QString, MachineInfo> machineMap;
    std::map<std::filesystem::path, PathInfo> pathInfoMap;
    plist_dict lastStatus;
};

#endif // MAINWINDOW_H
