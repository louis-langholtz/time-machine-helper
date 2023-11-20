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
class QFileSystemWatcher;
class QTreeWidgetItem;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
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
    void addDirEntry(QTreeWidgetItem *item,
                     const QMap<QString, QByteArray>& attrs,
                     const std::filesystem::path& path,
                     const std::filesystem::file_status& status);
    void checkTmStatus();
    void showStatus(const QString& status);

signals:

private slots:
    void handleQueryFailedToStart(const QString &text);
    void handleGotDestinations(int count);
    void handleTmutilPathChange(const QString &path);
    void handleTmStatusNoPlist();
    void handleTmStatusReaderError(int lineNumber,
                                   int error,
                                   const QString& text);
    void handleTmStatusFinished(int code, int status);

private:
    QErrorMessage errorMessage;
    Ui::MainWindow *ui{};
    QTimer *timer{};
    QString tmUtilPath;
    QFileSystemWatcher *fileSystemWatcher{};
    QFont pathFont;
};

#endif // MAINWINDOW_H
