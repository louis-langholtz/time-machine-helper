#include <algorithm> // std::find_if_not
#include <coroutine>
#include <optional>
#include <utility>
#include <set>
#include <string>
#include <vector>
#include <iterator>

#include <QtDebug>
#include <QObject>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QFileInfo>
#include <QStringView>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFont>
#include <QHeaderView>
#include <QPushButton>
#include <QTimer>
#include <QFontDatabase>
#include <QXmlStreamReader>

#include "ui_mainwindow.h"
#include "directoryreader.h"
#include "mainwindow.h"
#include "pathactiondialog.h"
#include "plistprocess.h"
#include "settingsdialog.h"

namespace {

constexpr auto toolName = "Time Machine utility";

// Content of this attribute seems to be comma separated list, where
// first element is one of the following:
//   "SnapshotStorage","MachineStore", "Backup", "VolumeStore"
constexpr auto timeMachineMetaAttr =
    "com.apple.timemachine.private.structure.metadata";

// Machine level attributes...
constexpr auto machineMacAddrAttr   = "com.apple.backupd.BackupMachineAddress";
constexpr auto machineCompNameAttr  = "com.apple.backupd.ComputerName";
constexpr auto machineUuidAttr      = "com.apple.backupd.HostUUID";
constexpr auto machineModelAttr     = "com.apple.backupd.ModelID";
constexpr auto snapshotTypeAttr     = "com.apple.backupd.SnapshotType";
constexpr auto totalBytesCopiedAttr = "com.apple.backupd.SnapshotTotalBytesCopied";

// Volume level attributes...
constexpr auto fileSystemTypeAttr   = "com.apple.backupd.fstypename";
constexpr auto volumeBytesUsedAttr  = "com.apple.backupd.VolumeBytesUsed";

constexpr auto fullDiskAccessStr = "Full Disk Access";
constexpr auto systemSettingsStr = "System Settings";
constexpr auto privacySecurityStr = "Privacy & Security";
constexpr auto cantListDirWarning = "Warning: unable to list contents of this directory!";

constexpr auto tmutilDeleteVerb     = "delete";
constexpr auto tmutilVerifyVerb     = "verifychecksums";
constexpr auto tmutilUniqueSizeVerb = "uniquesize";
constexpr auto tmutilStatusVerb     = "status";

constexpr auto backupsCountCol = 1;

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

std::optional<QByteArray> get(
    const QMap<QString, QByteArray>& attrs,
    const QString& key)
{
    const auto it = attrs.find(key);
    if (it != attrs.end()) {
        return {*it};
    }
    return {};
}

QString pathTooltip(const QMap<QString, QByteArray>& attrs)
{
    if (const auto v = get(attrs, machineUuidAttr)) {
        return "This is a \"machine directory\".";
    }
    if (const auto v = get(attrs, machineCompNameAttr)) {
        return "This is a \"machine directory\".";
    }
    if (const auto v = get(attrs, snapshotTypeAttr)) {
        return "This is a \"backup\".";
    }
    if (const auto v = get(attrs, fileSystemTypeAttr)) {
        return "This is a \"volume store\".";
    }
    return {};
}

auto findTopLevelItem(const QTreeWidget& tree, const QString& key)
    -> QTreeWidgetItem*
{
    const auto count = tree.topLevelItemCount();
    for (auto i = 0; i < count; ++i) {
        const auto item = tree.topLevelItem(i);
        if (item->text(0) == key) {
            return item;
        }
    }
    return nullptr;
}

auto findAddableTopLevelItems(
    const QTreeWidget& tree,
    const std::vector<std::string>& names) -> std::set<QString>
{
    auto result = std::set<QString>{};
    for (const auto& name: names) {
        const auto key = QString::fromStdString(name);
        const auto found = findTopLevelItem(tree, key);
        if (!found) {
            result.insert(key);
        }
    }
    return result;
}

auto findDeletableTopLevelItems(
    const QTreeWidget& tree,
    const std::vector<std::string>& names) -> std::set<QString>
{
    auto result = std::set<QString>{};
    const auto count = tree.topLevelItemCount();
    for (auto i = 0; i < count; ++i) {
        const auto key = tree.topLevelItem(i)->text(0);
        const auto it = std::find(names.begin(), names.end(),
                                  key.toStdString());
        if (it == names.end()) {
            result.insert(key);
        }
    }
    return result;
}

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
    this->ui->uniqueSizePushButton->setDisabled(true);
    this->ui->restoringPushButton->setDisabled(true);
    this->ui->verifyingPushButton->setDisabled(true);
    this->ui->mountPointsWidget->
        setSelectionMode(QAbstractItemView::SelectionMode::MultiSelection);

    this->tmUtilPath = SettingsDialog::tmutilPath();

    this->ui->destinationsWidget->setTmutilPath(this->tmUtilPath);
    this->ui->destinationsWidget->horizontalHeader()
        ->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);

    connect(this->ui->actionAbout, &QAction::triggered,
            this, &MainWindow::showAboutDialog);
    connect(this->ui->actionSettings, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);
    connect(this->fileSystemWatcher, &QFileSystemWatcher::directoryChanged,
            this, &MainWindow::updateMountPointsDir);
    connect(this->fileSystemWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::updateMountPointsFile);
    connect(this->ui->deletingPushButton, &QPushButton::pressed,
            this, &MainWindow::deleteSelectedPaths);
    connect(this->ui->uniqueSizePushButton, &QPushButton::pressed,
            this, &MainWindow::uniqueSizeSelectedPaths);
    connect(this->ui->restoringPushButton, &QPushButton::pressed,
            this, &MainWindow::restoreSelectedPaths);
    connect(this->ui->verifyingPushButton, &QPushButton::pressed,
            this, &MainWindow::verifySelectedPaths);

    connect(this->ui->destinationsWidget, &DestinationsWidget::failedToStartQuery,
            this, &MainWindow::handleQueryFailedToStart);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotPaths,
            this, &MainWindow::updateMountPointsView);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotError,
            this, &MainWindow::showStatus);

    connect(this->ui->mountPointsWidget, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::selectedPathsChanged);
    connect(this->ui->mountPointsWidget, &QTreeWidget::itemExpanded,
            this, &MainWindow::mountPointItemExpanded);
    connect(this->ui->mountPointsWidget, &QTreeWidget::itemCollapsed,
            this, &MainWindow::mountPointItemCollapsed);

    connect(this->timer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);
    connect(this->timer, &QTimer::timeout,
            this->ui->destinationsWidget, &DestinationsWidget::queryDestinations);
    QTimer::singleShot(0,
                       this->ui->destinationsWidget,
                       &DestinationsWidget::queryDestinations);
    this->timer->start(2500);
}

MainWindow::~MainWindow()
{
    delete this->ui;
}

void MainWindow::updateMountPointsView(const std::vector<std::string>& paths)
{
    {
        const auto pathsDel = findDeletableTopLevelItems(
            *(this->ui->mountPointsWidget), paths);
        for (const auto& path: pathsDel) {
            qDebug() << "removing old mountPoint path=" << path;
            delete findTopLevelItem(*(this->ui->mountPointsWidget), path);
        }
    }
    {
        constexpr auto policy = QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
        const auto pathsAdd = findAddableTopLevelItems(
            *(this->ui->mountPointsWidget), paths);
        for (const auto& path: pathsAdd) {
            // mp might be like "/Volumes/My Backup Disk"
            qDebug() << "adding new mountPoint path=" << path;
            const auto item = new QTreeWidgetItem(QTreeWidgetItem::UserType);
            item->setChildIndicatorPolicy(policy);
            item->setText(0, path);
            item->setData(0, Qt::ItemDataRole::UserRole, path);
            item->setFont(0, pathFont);
            item->setWhatsThis(0, QString("This is the file system info for '%1'")
                                      .arg(path));
            this->ui->mountPointsWidget->addTopLevelItem(item);
        }
    }
    if (paths.empty()) {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("No destination mounted!");
        msgBox.setInformativeText("No backups or restores are currently possible!");
        msgBox.exec();
    }
}

void MainWindow::reportDir(QTreeWidgetItem *item,
                           std::error_code ec)
{
    const QString pathName =
        item->data(0, Qt::ItemDataRole::UserRole).toString();
    if (!ec) {
        if (!this->fileSystemWatcher->addPath(pathName)) {
            qInfo() << "reportDir unable to add path to watcher:"
                    << pathName;
        }
        else {
            qDebug() << "reportDir added path to watcher:"
                     << pathName;
        }
        return;
    }

    item->setToolTip(0, cantListDirWarning);
    item->setBackground(0, QBrush(QColor(Qt::red)));

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
        const auto fileName =
            std::filesystem::path(appPath.toStdString()).filename();
        auto infoText = QString("Is macOS *%1* perhaps not enabled for '%2'?")
                            .arg(fullDiskAccessStr, fileName.c_str());
        infoText.append(QString("\nTo check, choose Apple menu  > %1 > %2 > %3")
                            .arg(systemSettingsStr,
                                 privacySecurityStr,
                                 fullDiskAccessStr));
        msgBox.setInformativeText(infoText);
        // perhaps also run:
        // open "x-apple.systempreferences:com.apple.preference.security"
    }
    msgBox.exec();
}

void MainWindow::addDirEntry(
    QTreeWidgetItem *parent,
    const QMap<QString, QByteArray>& attrs,
    const std::filesystem::path& path,
    const std::filesystem::file_status& status)
{
    using QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    using QTreeWidgetItem::ChildIndicatorPolicy::DontShowIndicator;

    auto col = 0;
    const auto item = new QTreeWidgetItem;
    item->setTextAlignment(col, Qt::AlignLeft|Qt::AlignVCenter);
    item->setFont(col, this->pathFont);
    item->setText(col, QString::fromStdString(path.filename().string()));
    item->setData(col, Qt::ItemDataRole::UserRole, QString(path.c_str()));
    item->setToolTip(col, pathTooltip(attrs));

    ++col;
    item->setTextAlignment(col, Qt::AlignRight);
    if (get(attrs, machineUuidAttr)) {
        item->setText(col, "?");
        item->setToolTip(col, "Expand to get value.");
    }

    auto isBackup = false;

    ++col;
    item->setTextAlignment(col, Qt::AlignRight);
    if (const auto v = get(attrs, snapshotTypeAttr)) {
        isBackup = true;
        item->setText(col, QString(*v));
    }

    ++col;
    item->setTextAlignment(col, Qt::AlignRight);
    if (const auto v = get(attrs, totalBytesCopiedAttr)) {
        isBackup = true;
        auto okay = false;
        const auto bytes = QString(*v).toLongLong(&okay);
        if (okay) {
            const auto megaBytes = double(bytes) / (1000 * 1000);
            item->setText(col, QString::number(megaBytes, 'f', 2));
        }
    }

    ++col;
    item->setTextAlignment(col, Qt::AlignCenter);
    if (const auto v = get(attrs, fileSystemTypeAttr)) {
        item->setText(col, QString(*v));
    }

    ++col;
    item->setTextAlignment(col, Qt::AlignRight);
    if (const auto v = get(attrs, volumeBytesUsedAttr)) {
        item->setText(col, QString(*v));
    }

    const auto indicatorPolicy =
        (status.type() == std::filesystem::file_type::directory)
                                     ? ShowIndicator
                                     : DontShowIndicator;

    // Following may not work. For more info, see:
    // https://stackoverflow.com/q/30088705/7410358
    // https://bugreports.qt.io/browse/QTBUG-28312
    item->setChildIndicatorPolicy(indicatorPolicy);

    parent->addChild(item);
    if (isBackup) {
        auto ok = false;
        const auto t = parent->text(backupsCountCol);
        parent->setText(backupsCountCol, QString::number(t.toLongLong(&ok) + 1));
    }
}

void MainWindow::mountPointItemExpanded(QTreeWidgetItem *item)
{
    qDebug() << "got mount point expanded signal"
             << "for item:" << item->text(0);
    const auto backupsCount = item->text(backupsCountCol);
    if (!backupsCount.isEmpty()) {
        item->setText(backupsCountCol, "?");
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

void MainWindow::mountPointItemCollapsed(QTreeWidgetItem *item)
{
    qDebug() << "got mount point collapsed signal"
             << "for item:" << item->text(0);
    for (const auto* child: item->takeChildren()) {
        delete child;
    }
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
    dialog->setPathPrefix("-p");
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
}

void MainWindow::uniqueSizeSelectedPaths()
{
    const auto selectedPaths = toStringList(this->ui->mountPointsWidget->selectedItems());
    qInfo() << "uniqueSizeSelectedPaths called for" << selectedPaths;

    const auto dialog = new PathActionDialog{this};
    {
        // Ensures output of tmutil shown as soon as available.
        auto env = dialog->environment();
        env.insert("STDBUF", "L"); // see "man 3 setbuf"
        dialog->setEnvironment(env);
    }
    dialog->setTmutilPath(this->tmUtilPath);
    dialog->setWindowTitle("Unique Size Dialog");
    dialog->setText("Are you sure that you want to uniquely size the following paths?");
    dialog->setPaths(selectedPaths);
    dialog->setAction(tmutilUniqueSizeVerb);
    //dialog->setAsRoot(true);
    dialog->show();
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

    const auto dialog = new PathActionDialog{this};
    {
        // Ensures output of tmutil shown as soon as available.
        auto env = dialog->environment();
        env.insert("STDBUF", "L"); // see "man 3 setbuf"
        dialog->setEnvironment(env);
    }
    dialog->setTmutilPath(this->tmUtilPath);
    dialog->setWindowTitle("Verify Dialog");
    dialog->setText("Are you sure that you want to verify the following paths?");
    dialog->setPaths(selectedPaths);
    dialog->setAction(tmutilVerifyVerb);
    //dialog->setAsRoot(true);
    dialog->show();
}

void MainWindow::selectedPathsChanged()
{
    qInfo() << "selectedPathsChanged called!";
    const auto selectionIsEmpty = this->ui->mountPointsWidget->selectedItems().empty();
    this->ui->deletingPushButton->setDisabled(selectionIsEmpty);
    this->ui->uniqueSizePushButton->setDisabled(selectionIsEmpty);
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

void MainWindow::showSettingsDialog()
{
    qDebug() << "showPreferencesDialog called";
    const auto dialog = new SettingsDialog{this};
    connect(dialog, &SettingsDialog::tmutilPathChanged,
            this, &MainWindow::handleTmutilPathChange);
    connect(dialog, &SettingsDialog::finished,
            dialog, &SettingsDialog::deleteLater);
    dialog->exec();
}

void MainWindow::checkTmStatus()
{
    // need to call "tmutil status -X"
    auto *process = new PlistProcess{this};
    connect(process, &PlistProcess::gotPlist,
            this->ui->destinationsWidget, &DestinationsWidget::handleStatus);
    connect(process, &PlistProcess::gotNoPlist,
            this, &MainWindow::handleTmStatusNoPlist);
    connect(process, &PlistProcess::gotReaderError,
            this, &MainWindow::handleTmStatusReaderError);
    connect(process, &PlistProcess::finished,
            this, &MainWindow::handleTmStatusFinished);
    connect(process, &PlistProcess::finished,
            process, &PlistProcess::deleteLater);
    process->start(this->tmUtilPath,
                   QStringList() << tmutilStatusVerb << "-X");
}

void MainWindow::showStatus(const QString& status)
{
    this->ui->statusbar->showMessage(status);
}

void MainWindow::handleQueryFailedToStart(const QString &text)
{
    qDebug() << "MainWindow::handleQueryFailedToStart called:"
             << text;
    disconnect(this->timer, &QTimer::timeout,
               this->ui->destinationsWidget,
               &DestinationsWidget::queryDestinations);
    constexpr auto queryFailedMsg = "Unable to start destinations query";
    const auto tmutilPath = this->ui->destinationsWidget->tmutilPath();
    const auto info = QFileInfo(tmutilPath);
    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Open);
    msgBox.setOptions(QMessageBox::Option::DontUseNativeDialog);
    msgBox.setWindowTitle("Error!"); // macOS ignored but set in case changes
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText(queryFailedMsg);
    msgBox.setDetailedText(text);
    auto informativeText = QString{};
    if (!info.exists()) {
        informativeText = QString("%1 '%21' not found!")
                              .arg(toolName, tmutilPath);
    }
    else if (!info.isFile()) {
        informativeText = QString("%1 path '%2' not a file!")
                              .arg(toolName, info.absoluteFilePath());
    }
    else if (!info.isExecutable()) {
        informativeText = QString("%1 file '%2' not executable!")
                              .arg(toolName, info.absoluteFilePath());
    }
    if (!informativeText.isEmpty()) {
        informativeText.append(
            QString("Perhaps the %1 path needs to be updated in settings?")
                .arg(toolName));
    }
    msgBox.setInformativeText(informativeText);
    if (msgBox.exec() == QMessageBox::Open) {
        this->showSettingsDialog();
    }
}

void MainWindow::handleTmutilPathChange(const QString &path)
{
    qDebug() << "MainWindow::handleTmutilPathChange called:"
             << path;

    disconnect(this->timer, &QTimer::timeout,
               this, &MainWindow::checkTmStatus);
    disconnect(this->timer, &QTimer::timeout,
               this->ui->destinationsWidget, &DestinationsWidget::queryDestinations);

    this->tmUtilPath = path;
    this->ui->destinationsWidget->setTmutilPath(path);

    connect(this->timer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);
    connect(this->timer, &QTimer::timeout,
            this->ui->destinationsWidget, &DestinationsWidget::queryDestinations);
}

void MainWindow::handleTmStatusNoPlist()
{
    qDebug() << "handleTmStatusNoPlist called";

    disconnect(this->timer, &QTimer::timeout,
               this, &MainWindow::checkTmStatus);
    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Open);
    msgBox.setOptions(QMessageBox::Option::DontUseNativeDialog);
    msgBox.setWindowTitle("Error!"); // macOS ignored but set in case changes
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText("Not getting status info!");
    const auto infoText =
        QString("Perhaps the %1 path needs to be updated in settings?")
            .arg(toolName);
    msgBox.setInformativeText(infoText);
    if (msgBox.exec() == QMessageBox::Open) {
        this->showSettingsDialog();
    }
}

void MainWindow::handleTmStatusReaderError(
    int lineNumber, int error, const QString &text)
{
    qDebug() << "handleTmStatusReaderError called:" << text;
    qDebug() << "line #" << lineNumber;
    qDebug() << "error" << error;
    this->showStatus(QString("Error reading Time Machine status: line %1, %2")
                         .arg(lineNumber)
                         .arg(text));
}

void MainWindow::handleTmStatusFinished(int code, int status)
{
    switch (QProcess::ExitStatus(status)) {
    case QProcess::NormalExit:
        break;
    case QProcess::CrashExit:
        this->showStatus(QString("When getting status: %1 exited abnormally")
                             .arg(toolName));
        return;
    }
    if (code != 0) {
        this->showStatus(QString("When getting status: %1 exited with code %2")
                             .arg(toolName).arg(code));
    }
}

