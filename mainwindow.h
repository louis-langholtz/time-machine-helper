#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <filesystem>
#include <vector>

#include <QMainWindow>

#include "coroutine.h"
#include "PlistObject.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QXmlStreamReader;
class QProcess;
class QFileSystemWatcher;
class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void readDestinationInfo();

public slots:
    void readMore();
    void processFinished(int exitCode, int exitStatus);
    void updateDestinationsWidget(const PlistObject& plist);
    void resizeMountPointsColumns();
    void updateBackupStatusWidget(const PlistObject& plist);
    void updateMountPointsView(const std::vector<std::filesystem::path>& paths);
    void updateMountPointsDir(const QString& path);
    void mountPointItemExpanded(QTreeWidgetItem *item);
    void deleteSelectedPaths();
    void restoreSelectedPaths();
    void verifySelectedPaths();
    void selectedPathsChanged();

signals:
    void gotDestinationsPlist(const PlistObject& plist);
    void gotMountPoints(const std::vector<std::filesystem::path>& paths);

private:
    Ui::MainWindow *ui{};
    QProcess *process{};
    QXmlStreamReader *reader{};
    QString currentText;
    await_handle<PlistVariant> awaiting_handle;
    coroutine_task<PlistObject> task;
    std::vector<std::map<std::string, PlistObject>> destinations;
    QString tmUtilPath;
    QFileSystemWatcher *fileSystemWatcher{};
};

PlistElementType toPlistElementType(
    const QStringView& string);

#endif // MAINWINDOW_H
