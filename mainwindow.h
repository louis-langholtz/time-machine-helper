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

class QTableWidget;
class QTableWidgetItem;
class QThreadPool;
class QTreeWidgetItem;
class QTimer;
class QMessageBox;
class QLabel;
class QSplitter;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;

class PathActionDialog;
class DirectoryReader;

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
    // track: physical volumes, volumes (in backups), backups,
    //   machines, & destinations.

    void selectedBackupsChanged();
    void handleQueryFailedToStart(const QString &text);
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
    void handleProgramFinished(
        const QString& program,
        const QStringList& args,
        int code,
        int status);
    void handleDestinationAction(const QString& actionName,
                                 const std::string& destId);

    void handleItemChanged(QTableWidgetItem *item);
    void handleRestoreSelectedPathsChanged(
        PathActionDialog *dialog,
        const QStringList& paths);
    void changeTmutilStatusInterval(int msecs);
    void changeTmutilDestinationsInterval(int msecs);
    void changePathInfoInterval(int msecs);
    void updateMountPointPaths();
    void updatePathInfo(const std::string& pathName);
    void updateStorageDir(const std::filesystem::path& dir,
                          const QSet<QString>& filenames);
    void updateMachineDir(const std::filesystem::path& dir,
                          const QSet<QString>& filenames);
    void updateVolumeDir(const std::filesystem::path& dir,
                         const QSet<QString>& filenames);

    /// @brief Thread pool just for directory reading.
    QThreadPool *directoryReaderThreadPool;

    QAction *actionAbout;
    QAction *actionQuit;
    QAction *actionSettings;
    QSplitter *centralWidget;
    QFrame *destinationsFrame;
    QVBoxLayout *destinationsLayout;
    QLabel *destinationsLabel;
    QTableWidget *destinationsTable;
    QFrame *machinesFrame;
    QVBoxLayout *machinesLayout;
    QTableWidget *machinesTable;
    QLabel *machinesLabel;
    QFrame *volumesFrame;
    QVBoxLayout *volumesLayout;
    QLabel *volumesLabel;
    QTableWidget *volumesTable;
    QFrame *backupsFrame;
    QVBoxLayout *backupsLayout;
    QLabel *backupsLabel;
    QTableWidget *backupsTable;
    QFrame *backupsActionsFrame;
    QHBoxLayout *backupsActionsLayout;
    QPushButton *deletingPushButton;
    QPushButton *verifyingPushButton;
    QPushButton *uniqueSizePushButton;
    QPushButton *restoringPushButton;
    QMenuBar *menubar;
    QMenu *menuActions;
    QStatusBar *statusbar;
    QToolBar *toolBar;

    QErrorMessage errorMessage;
    QMessageBox *noDestinationsDialog{};
    QTimer *destinationsTimer{};
    QTimer *statusTimer{};
    QTimer *pathInfoTimer{};
    QString tmutilPath;
    QString sudoPath;
    QFont fixedFont;
    std::map<std::string, plist_dict> mountMap;
    std::map<QString, MachineInfo> machineMap;
    std::map<std::filesystem::path, PathInfo> pathInfoMap;
    std::map<std::string, DirectoryReader*> directoryReaders;
    plist_dict lastStatus;
};

#endif // MAINWINDOW_H
