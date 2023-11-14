#include <algorithm> // std::find_if_not
#include <coroutine>
#include <optional>
#include <utility>
#include <string>
#include <vector>
#include <iterator>

#include <QtDebug>
#include <QObject>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>
#include <QStringView>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFont>
#include <QHeaderView>
#include <QPushButton>
#include <QTimer>
#include <QFontDatabase>

#include "ui_mainwindow.h"
#include "directoryreader.h"
#include "mainwindow.h"
#include "pathactiondialog.h"

namespace {

template <class T>
std::optional<T> get(const plist_dict& map, const std::string& key)
{
    if (const auto it = map.find(key); it != map.end()) {
        if (const auto p = std::get_if<T>(&it->second.value)) {
            return {*p};
        }
    }
    return {};
}

struct tmutil_destination {
    std::string id;
    std::string name;
    std::string kind;
    int last_destination{};
    std::filesystem::path mount_point;
};

tmutil_destination to_tmutil_destination(
    const plist_dict& object)
{
    auto result = tmutil_destination{};
    if (const auto value = get<std::string>(object, "ID")) {
        result.id = *value;
    }
    if (const auto value = get<std::string>(object, "Name")) {
        result.name = *value;
    }
    if (const auto value = get<std::string>(object, "Kind")) {
        result.kind = *value;
    }
    if (const auto value = get<int>(object, "LastDestination")) {
        result.last_destination = *value;
    }
    if (const auto value = get<std::string>(object, "MountPoint")) {
        result.mount_point = *value;
    }
    return result;
}

std::optional<tmutil_destination> to_tmutil_destination(
    const plist_object& object)
{
    if (const auto p = std::get_if<plist_dict>(&object.value)) {
        return to_tmutil_destination(*p);
    }
    return {};
}

std::vector<tmutil_destination> to_tmutil_destinations(
    const plist_array& array)
{
    auto result = std::vector<tmutil_destination>{};
    for (const auto& element: array) {
        if (const auto destination = to_tmutil_destination(element)) {
            result.push_back(*destination);
        }
    }
    return result;
}

std::vector<tmutil_destination> to_tmutil_destinations(
    const plist_object& object)
{
    if (const auto p = std::get_if<plist_dict>(&object.value)) {
        if (const auto it = p->find("Destinations"); it != p->end()) {
            if (const auto q = std::get_if<plist_array>(&it->second.value)) {
                return to_tmutil_destinations(*q);
            }
            qWarning() << "expected array";
            return {};
        }
        qWarning() << "expected destinations";
        return {};
    }
    qWarning() << "expected dict";
    return {};
}

QStringList toStringList(const QList<QTreeWidgetItem*>& items)
{
    QStringList result;
    for (const auto* item: items) {
        const auto string = item->data(0, Qt::ItemDataRole::UserRole).toString();
        result += string;
    }
    return result;
}

static constexpr auto tmutilSettingsKey = "tmutil_path";
static constexpr auto defaultTmutilPath = "/usr/bin/tmutil";
static constexpr auto fullDiskAccessStr = "Full Disk Access";
static constexpr auto systemSettingsStr = "System Settings";
static constexpr auto privacySecurityStr = "Privacy & Security";
static constexpr auto cantListDirWarning = "Warning: unable to list contents of this directory!";

static constexpr auto tmutilDeleteVerb   = "delete";
static constexpr auto tmutilStatusVerb   = "status";

}

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    timer(new QTimer(this)),
    fileSystemWatcher(new QFileSystemWatcher(this))
{
    this->pathFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    this->ui->setupUi(this);

    this->ui->deletingPushButton->setDisabled(true);
    this->ui->restoringPushButton->setDisabled(true);
    this->ui->verifyingPushButton->setDisabled(true);
    this->ui->mountPointsWidget->
        setSelectionMode(QAbstractItemView::SelectionMode::MultiSelection);

    QSettings settings;
    if (settings.contains(tmutilSettingsKey)) {
        qDebug() << "settings appears to contain:" << tmutilSettingsKey;
        qDebug() << "found:" << settings.value(tmutilSettingsKey).toString();
    }
    else {
        qInfo() << "settings don't appear to contain:" << tmutilSettingsKey;
        qInfo() << "setting value to default:" << defaultTmutilPath;
        settings.setValue(tmutilSettingsKey, QString(defaultTmutilPath));
    }
    const auto tmUtilPath = settings.value(tmutilSettingsKey);
    const auto tmutil_file_info = QFileInfo(tmUtilPath.toString());
    if (tmutil_file_info.isExecutable()) {
        qInfo() << "Executable absolute path is:"
                << tmutil_file_info.absoluteFilePath();
        this->tmUtilPath = tmUtilPath.toString();
    }
    else if (tmutil_file_info.exists()) {
        qWarning() << "exists but not executable!";
        QMessageBox::critical(this, "Error!",
                              QString("'%1' file not executable!")
                                  .arg(tmutil_file_info.absoluteFilePath()));
    }
    else {
        QMessageBox::critical(this, "Error!",
                              QString("'%1' file not found!")
                                  .arg(tmutil_file_info.absoluteFilePath()));
        qWarning() << "tmutil file not found!";
    }

    this->ui->destinationsWidget->horizontalHeader()
        ->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);

    connect(this->ui->actionAbout, &QAction::triggered,
            this, &MainWindow::showAboutDialog);
    connect(this->fileSystemWatcher, &QFileSystemWatcher::directoryChanged,
            this, &MainWindow::updateMountPointsDir);
    connect(this->fileSystemWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::updateMountPointsFile);
    connect(this->ui->mountPointsWidget, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::selectedPathsChanged);
    connect(this->ui->deletingPushButton, &QPushButton::pressed,
            this, &MainWindow::deleteSelectedPaths);
    connect(this->ui->restoringPushButton, &QPushButton::pressed,
            this, &MainWindow::restoreSelectedPaths);
    connect(this->ui->verifyingPushButton, &QPushButton::pressed,
            this, &MainWindow::verifySelectedPaths);
    connect(this->timer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotPaths,
            this, &MainWindow::updateMountPointsView);
    connect(this->ui->mountPointsWidget, &QTreeWidget::itemExpanded,
            this, &MainWindow::mountPointItemExpanded);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotStatus,
            this, &MainWindow::showStatus);

    this->timer->start(1000);

    this->ui->destinationsWidget->queryDestinations();
}

MainWindow::~MainWindow()
{
    delete this->ui;
}

void MainWindow::updateMountPointsView(const std::vector<std::string>& paths)
{
    qInfo() << "updateMountPointsView called with" << paths.size();
    constexpr auto policy = QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    for (const auto& mp: paths) {
        // mp might be like "/Volumes/My Backup Disk"
        qDebug() << "mountPoint path=" << mp;
        const auto si = std::filesystem::space(mp);
        const auto capacityInGb = double(si.capacity) / (1000 * 1000 * 1000);
        const auto freeInGb = double(si.free) / (1000 * 1000 * 1000);
        const auto item = new QTreeWidgetItem(QTreeWidgetItem::UserType);
        item->setChildIndicatorPolicy(policy);
        item->setText(0, mp.c_str());
        item->setData(0, Qt::ItemDataRole::UserRole, QString(mp.c_str()));
        item->setFont(0, pathFont);
        item->setWhatsThis(0, QString("This is the file system info for '%1'")
                                  .arg(mp.c_str()));
        item->setText(1, QString::number(capacityInGb, 'f', 2));
        item->setTextAlignment(1, Qt::AlignRight);
        item->setToolTip(1, QString("Capacity of this filesystem (%1 bytes).")
                                .arg(si.capacity));
        item->setText(2, QString::number(freeInGb, 'f', 2));
        item->setTextAlignment(2, Qt::AlignRight);
        item->setToolTip(2, QString("Free space of this filesystem (%1 bytes).")
                                .arg(si.free));
        this->ui->mountPointsWidget->addTopLevelItem(item);
    }
    this->ui->mountPointsWidget->resizeColumnToContents(0);
}

void MainWindow::reportDir(QTreeWidgetItem *item,
                           const QMap<int, QString>& textMap,
                           int error)
{
    const QString pathName = item->data(0, Qt::ItemDataRole::UserRole).toString();
    if (error == 0) {
        if (!this->fileSystemWatcher->addPath(pathName)) {
            qInfo() << "reportDir unable to add path to watcher:" << pathName;
        }
        else {
            qDebug() << "reportDir added path to watcher:" << pathName;
        }
        for (const auto& entry: textMap.toStdMap()) {
            item->setText(entry.first, entry.second);
        }
        return;
    }

    item->setToolTip(0, cantListDirWarning);
    item->setBackground(0, QBrush(QColor(Qt::red)));

    const auto ec = std::make_error_code(std::errc(error));

    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setTextFormat(Qt::TextFormat::MarkdownText); // doesn't work on macos?
    msgBox.setWindowTitle("Error!");
    msgBox.setText(QString("Unable to list contents of directory `%1`")
                       .arg(pathName));
    msgBox.setDetailedText(QString("Reason: %2")
                               .arg(QString::fromStdString(ec.message())));
    if (ec == std::make_error_code(std::errc::operation_not_permitted)) {
        const auto appPath = QCoreApplication::applicationFilePath();
        const auto fileName = std::filesystem::path(appPath.toStdString()).filename();
        auto infoText = QString("Is macOS *%1* perhaps not enabled for '%2'?")
                            .arg(fullDiskAccessStr, fileName.c_str());
        infoText.append(QString("\nTo check, choose Apple menu  > %1 > %2 > %3")
                            .arg(systemSettingsStr, privacySecurityStr, fullDiskAccessStr));
        msgBox.setInformativeText(infoText);
        // perhaps also run: open "x-apple.systempreferences:com.apple.preference.security"
    }
    msgBox.exec();
}

void MainWindow::addDirEntry(QTreeWidgetItem *item,
                             const QMap<int, QString> &textMap,
                             const QMap<int, QPair<int, QVariant> > &dataMap)
{
    using QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    using QTreeWidgetItem::ChildIndicatorPolicy::DontShowIndicator;

    const auto childItem = new QTreeWidgetItem(item);

    // Following may not work. For more info, see:
    // https://stackoverflow.com/q/30088705/7410358
    // https://bugreports.qt.io/browse/QTBUG-28312
    childItem->setChildIndicatorPolicy(ShowIndicator);

    childItem->setTextAlignment(3, Qt::AlignRight);
    childItem->setTextAlignment(4, Qt::AlignRight);
    childItem->setTextAlignment(5, Qt::AlignRight);
    childItem->setTextAlignment(6, Qt::AlignCenter);
    childItem->setTextAlignment(7, Qt::AlignRight);

    childItem->setFont(0, this->pathFont);
    auto isVolumeLevel = false;
    for (const auto& entry: textMap.toStdMap()) {
        childItem->setText(entry.first, entry.second);
        switch (entry.first) {
        case 4:
        case 5:
            // isBackupLevel = true;
            break;
        case 6:
        case 7:
            isVolumeLevel = true;
            break;
        }
    }
    for (const auto& entry: dataMap.toStdMap()) {
        childItem->setData(entry.first, entry.second.first, entry.second.second);
    }
    if (isVolumeLevel) {
        childItem->setChildIndicatorPolicy(DontShowIndicator);
    }
    item->addChild(childItem);
}

void MainWindow::mountPointItemExpanded(QTreeWidgetItem *item)
{
    qDebug() << "got mount point expanded signal"
             << "for item:" << item->text(0);
    for (const auto* child: item->takeChildren()) {
        delete child;
    }

    DirectoryReader *workerThread = new DirectoryReader(item, this);
    connect(workerThread, &DirectoryReader::ended,
            this, &MainWindow::reportDir);
    connect(workerThread, &DirectoryReader::entry,
            this, &MainWindow::addDirEntry);
    connect(workerThread, &DirectoryReader::finished,
            workerThread, &QObject::deleteLater);
    workerThread->start();
}

void MainWindow::resizeMountPointsColumns()
{
    this->ui->mountPointsWidget->resizeColumnToContents(1);
    this->ui->mountPointsWidget->resizeColumnToContents(2);
    this->ui->mountPointsWidget->resizeColumnToContents(3);
    this->ui->mountPointsWidget->resizeColumnToContents(4);
    this->ui->mountPointsWidget->resizeColumnToContents(5);
    this->ui->mountPointsWidget->resizeColumnToContents(6);
}

void MainWindow::updateBackupStatusWidget(const plist_object &plist)
{
    // display plist output from "tmutil status -X"
    qInfo() << "updateBackupStatusWidget called!";
}

void MainWindow::updateMountPointsDir(const QString &path)
{
    qInfo() << "updateMountPointsDir called for path:" << path;
    qInfo() << "d dirs watched" << this->fileSystemWatcher->directories();
    qInfo() << "d fils watched" << this->fileSystemWatcher->files();
}

void MainWindow::updateMountPointsFile(const QString &path)
{
    qInfo() << "updateMountPointsFile called for path:" << path;
    qInfo() << "f dirs watched" << this->fileSystemWatcher->directories();
    qInfo() << "f fils watched" << this->fileSystemWatcher->files();
}

void MainWindow::deleteSelectedPaths()
{
    const auto selectedPaths = toStringList(
        this->ui->mountPointsWidget->selectedItems());
    qInfo() << "deleteSelectedPaths called for" << selectedPaths;

    const auto dialog = new PathActionDialog{this};
    {
        // Ensures output of tmutil shown as soon as available.
        auto env = dialog->environment();
        env.insert("STDBUF", "L"); // see "man 3 setbuf"
        dialog->setEnvironment(env);
    }
    dialog->setTmutilPath(this->tmUtilPath);
    dialog->setWindowTitle("Deletion Dialog");
    dialog->setText("Are you sure that you want to delete the following paths?");
    dialog->setPaths(selectedPaths);
    dialog->setAction(tmutilDeleteVerb);
    dialog->setAsRoot(true);
    dialog->show();

    // Output from sudo tmutil delete -p backup1-path -p backup2-path
    // looks like:
    // Deleting: /Volumes/disk/Backups.backupdb/machine/backup1
    // Deleted (2.3G): /Volumes/disk/Backups.backupdb/machine/backup1
    // Deleting: /Volumes/disk/Backups.backupdb/machine/backup2
    // Deleted (1.2G): /Volumes/disk/Backups.backupdb/machine/backup2
    // Total deleted: 3.78 GB
    // May need to use something like osascript to authorize tmutil
    // osascript -e 'do shell script "tmutil delete -p path1 -p path2" with prompt "Time Machine Helper is trying to run a tmutil administrative command" with administrator privileges'
    // osascript -e 'do shell script "echo hello bin two" with prompt "Time Machine Helper is trying to run a tmutil administrative command" with administrator privileges'
}

void MainWindow::restoreSelectedPaths()
{
    const auto selectedPaths = toStringList(this->ui->mountPointsWidget->selectedItems());
    qInfo() << "restoreSelectedPaths called for" << selectedPaths;
}

void MainWindow::verifySelectedPaths()
{
    const auto selectedPaths = toStringList(this->ui->mountPointsWidget->selectedItems());
    qInfo() << "verifySelectedPaths called for" << selectedPaths;
}

void MainWindow::selectedPathsChanged()
{
    qInfo() << "selectedPathsChanged called!";
    const auto selectionIsEmpty = this->ui->mountPointsWidget->selectedItems().empty();
    this->ui->deletingPushButton->setDisabled(selectionIsEmpty);
    this->ui->restoringPushButton->setDisabled(selectionIsEmpty);
    this->ui->verifyingPushButton->setDisabled(selectionIsEmpty);
}

void MainWindow::showAboutDialog()
{
    QString text;
    text.append(QString("%1 %2.%3").arg(this->windowTitle(),
                                           QString::number(VERSION_MAJOR),
                                           QString::number(VERSION_MINOR)));
    text.append("\n\n");
    text.append(QString("Copyright %1").arg(COPYRIGHT));
    text.append("\n\n");
    text.append(QString("Source code available at:\n%1")
                    .arg("https://github.com/louis-langholtz/time-machine-helper"));
    text.append("\n\n");
    text.append(QString("Compiled with Qt version %1.\nRunning with Qt version %2.")
                    .arg(QT_VERSION_STR, qVersion()));
    QMessageBox::about(this, tr("About"), text);
}

void MainWindow::checkTmStatus()
{
    // need to call "tmutil status -X"
}

void MainWindow::showStatus(const QString& status)
{
    this->ui->statusbar->showMessage(status);
}

