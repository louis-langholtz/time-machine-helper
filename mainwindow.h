#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <filesystem>
#include <map>
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
class QTreeWidgetItem;
class QTimer;

struct PathInfo {
    std::filesystem::file_status status;
    QMap<QString, QByteArray> attributes;
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
    void resizeMountPointsColumns();
    void updateMountPointsView(
        const std::vector<std::string>& paths);
    void mountPointItemExpanded(QTreeWidgetItem *item);
    void mountPointItemCollapsed(QTreeWidgetItem *item);
    void updatePathInfo(QTreeWidgetItem *item);
    void updatePathInfos();
    void deleteSelectedPaths();
    void uniqueSizeSelectedPaths();
    void restoreSelectedPaths();
    void verifySelectedPaths();
    void selectedPathsChanged();
    void showAboutDialog();
    void showSettingsDialog();
    void reportDir(const std::filesystem::path& dir,
                   std::error_code ec,
                   const QSet<QString>& filenames);
    void updateDirEntry(const std::filesystem::path& path,
                        const std::filesystem::file_status& status,
                        const QMap<QString, QByteArray>& attrs);
    void checkTmStatus();
    void showStatus(const QString& status);

    void addTreeWidgetItem(QTreeWidgetItem *parent,
                           const std::filesystem::path& path);
    void changeTreeWidgetItem(QTreeWidgetItem *parent,
                              const std::filesystem::path& path);

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
    QTimer *pathInfoTimer{};
    QString tmutilPath;
    QString sudoPath;
    QFont fixedFont;
    std::map<std::filesystem::path, PathInfo> pathInfoMap;
};

#endif // MAINWINDOW_H
