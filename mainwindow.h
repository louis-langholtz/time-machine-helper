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

#include "coroutine.h"
#include "plist_object.h"

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
    void updateDestinationsWidget(const plist_object& plist);
    void resizeMountPointsColumns();
    void updateBackupStatusWidget(const plist_object& plist);
    void updateMountPointsView(const std::vector<std::filesystem::path>& paths);
    void updateMountPointsDir(const QString& path);
    void mountPointItemExpanded(QTreeWidgetItem *item);
    void deleteSelectedPaths();
    void restoreSelectedPaths();
    void verifySelectedPaths();
    void selectedPathsChanged();
    void showAboutDialog();
    void reportDir(QTreeWidgetItem *item,
                   const QMap<int, QString>& textMap,
                   int error);
    void addDirEntry(QTreeWidgetItem *item,
                     const QMap<int, QString>& textMap,
                     const QMap<int, QPair<int, QVariant>>& dataMap);

signals:
    void gotDestinationsPlist(const plist_object& plist);
    void gotMountPoints(const std::vector<std::filesystem::path>& paths);

private:
    Ui::MainWindow *ui{};
    QProcess *process{};
    QXmlStreamReader *reader{};
    QString currentText;
    await_handle<plist_variant> awaiting_handle;
    coroutine_task<plist_object> task;
    std::vector<std::map<std::string, plist_object>> destinations;
    QString tmUtilPath;
    QFileSystemWatcher *fileSystemWatcher{};
    QFont pathFont{"Courier"};
};

plist_element_type toPlistElementType(
    const QStringView& string);

#endif // MAINWINDOW_H
