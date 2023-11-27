#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <filesystem>
#include <vector>

#include <QFont>
#include <QMainWindow>
#include <QMap>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QErrorMessage>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QXmlStreamReader;
class QFileSystemWatcher;
class QTreeWidgetItem;
class QTimer;

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
    void resizeMountPointsColumns();
    void updateMountPointsView(
        const std::vector<std::string>& paths);
    void updateMountPointsDir(const QString& path);
    void updateMountPointsFile(const QString& path);
    void mountPointItemExpanded(QTreeWidgetItem *item);
    void mountPointItemCollapsed(QTreeWidgetItem *item);
    void deleteSelectedPaths();
    void uniqueSizeSelectedPaths();
    void restoreSelectedPaths();
    void verifySelectedPaths();
    void selectedPathsChanged();
    void showAboutDialog();
    void showSettingsDialog();
    void reportDir(QTreeWidgetItem *item,
                   std::error_code ec);
    void addDirEntry(QTreeWidgetItem *parent,
                     const QMap<QString, QByteArray>& attrs,
                     const std::filesystem::path& path,
                     const std::filesystem::file_status& status);
    void checkTmStatus();
    void showStatus(const QString& status);

private:
    void handleQueryFailedToStart(const QString &text);
    void handleGotDestinations(int count);
    void handleTmutilPathChange(const QString &path);
    void handleSudoPathChange(const QString &path);
    void handleTmStatusNoPlist();
    void handleTmStatusReaderError(qint64 lineNumber,
                                   int error,
                                   const QString& text);
    void handleTmStatusFinished(int code, int status);

    QErrorMessage errorMessage;
    Ui::MainWindow *ui{};
    QTimer *destinationsTimer{};
    QTimer *statusTimer{};
    QString tmutilPath;
    QString sudoPath;
    QFileSystemWatcher *fileSystemWatcher{};
    QFont pathFont;
};

#endif // MAINWINDOW_H
