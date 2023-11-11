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

#include "plist_object.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QXmlStreamReader;
class QProcess;
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
    void updateBackupStatusWidget(const plist_object& plist);
    void updateMountPointsView(const std::vector<std::string>& paths);
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
    void checkTmStatus();
    void showStatus(const QString& status);

signals:

private:
    Ui::MainWindow *ui{};
    QTimer *timer{};
    QString tmUtilPath;
    QFileSystemWatcher *fileSystemWatcher{};
    QFont pathFont;
};

#endif // MAINWINDOW_H
