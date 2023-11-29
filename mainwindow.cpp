#include <algorithm> // std::find_if_not
#include <optional>
#include <set>
#include <string>
#include <vector>

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
#include <QFileDialog>

#include "ui_mainwindow.h"
#include "directoryreader.h"
#include "mainwindow.h"
#include "pathactiondialog.h"
#include "plistprocess.h"
#include "settings.h"
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

// Backup level attributes...
constexpr auto snapshotTypeAttr     = "com.apple.backupd.SnapshotType";
constexpr auto snapshotStartAttr    = "com.apple.backupd.SnapshotStartDate";
constexpr auto snapshotFinishAttr   = "com.apple.backupd.SnapshotCompletionDate";
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
constexpr auto tmutilRestoreVerb    = "restore";
constexpr auto tmutilStatusVerb     = "status";

constexpr auto pathInfoUpdateTime = 10000;

enum FileColumn {
    filenameCol,
    backupsCountCol,
    snapshotTypeCol,
    durationCol,
    totalCopiedCol,
    filesysTypeCol,
    volBytesUseCol,
};

constexpr auto enabledAdminButtonStyle =
    "QPushButton {color: rgb(180, 0, 0);}";
constexpr auto disabledAdminButtonStyle =
    "QPushButton {color: rgb(180, 100, 100);}";

auto toLongLong(const std::optional<QByteArray>& value)
    -> std::optional<std::int64_t>
{
    auto okay = false;
    const auto number = QString{value.value_or(QByteArray{})}.toLongLong(&okay);
    return okay? std::optional<std::int64_t>{number}: std::optional<std::int64_t>{};
}

auto toMegaBytes(const std::optional<std::int64_t>& value)
    -> QString
{
    if (value) {
        const auto megaBytes = double(*value) / (1000 * 1000);
        return QString::number(megaBytes, 'f', 2);
    }
    return {};
}

auto toStringList(const QList<QTreeWidgetItem *> &items) -> QStringList
{
    QStringList result;
    for (const auto* item: items) {
        const auto string = item->data(0, Qt::ItemDataRole::UserRole).toString();
        result += string;
    }
    return result;
}

auto toIndicatorPolicy(const std::filesystem::file_type& value)
    -> QTreeWidgetItem::ChildIndicatorPolicy
{
    using QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    using QTreeWidgetItem::ChildIndicatorPolicy::DontShowIndicator;
    return (value == std::filesystem::file_type::directory)
               ? ShowIndicator
               : DontShowIndicator;
}

auto get(const QMap<QString, QByteArray> &attrs, const QString &key)
    -> std::optional<QByteArray>
{
    const auto it = attrs.find(key);
    if (it != attrs.end()) {
        return {*it};
    }
    return {};
}

auto pathTooltip(const QMap<QString, QByteArray> &attrs) -> QString
{
    if (const auto v = get(attrs, timeMachineMetaAttr)) {
        if (v->startsWith("SnapshotStorage")) {
            return "A \"backup store\".";
        }
    }
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

auto findItem(QTreeWidgetItem *item,
              std::filesystem::path::iterator first,
              const std::filesystem::path::iterator& last)
    -> QTreeWidgetItem*
{
    if (!item) {
        return nullptr;
    }
    for (; first != last; ++first) {
        auto foundChild = static_cast<QTreeWidgetItem*>(nullptr);
        const auto count = item->childCount();
        for (auto i = 0; i < count; ++i) {
            const auto child = item->child(i);
            if (child && child->text(0) == first->c_str()) {
                foundChild = child;
                break;
            }
        }
        if (!foundChild) {
            break;
        }
        item = foundChild;
    }
    return (first == last) ? item : nullptr;
}

auto findItem(QTreeWidget& tree,
              const std::filesystem::path::iterator& first,
              const std::filesystem::path::iterator& last)
    -> QTreeWidgetItem*
{
    const auto count = tree.topLevelItemCount();
    for (auto i = 0; i < count; ++i) {
        const auto item = tree.topLevelItem(i);
        if (!item) {
            continue;
        }
        const auto key = item->text(0);
        const auto root = std::filesystem::path{key.toStdString()};
        const auto result = std::mismatch(first, last, root.begin(), root.end());
        if (result.second == root.end()) {
            return findItem(item, result.first, last);
        }
    }
    return nullptr;
}

auto findChild(QTreeWidgetItem *parent,
               const std::filesystem::path& path)
    -> std::pair<QTreeWidgetItem*, int>
{
    const auto count = parent->childCount();
    for (auto i = 0; i < count; ++i) {
        const auto child = parent->child(i);
        const QString pathName =
            child->data(0, Qt::ItemDataRole::UserRole).toString();
        if (pathName == path.c_str()) {
            return {child, i};
        }
    }
    return {};
}

auto operator==(const std::filesystem::file_status& lhs,
                const std::filesystem::file_status& rhs) noexcept
{
    return (lhs.permissions() == rhs.permissions()) &&
           (lhs.type() == rhs.type());
}

auto operator==(const PathInfo& lhs, const PathInfo& rhs)
{
    return (lhs.status == rhs.status) &&
           (lhs.attributes == rhs.attributes);
}

void remove(std::set<QTreeWidgetItem*>& items, QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }
    items.erase(item);
    const auto count = item->childCount();
    for (auto i = 0; i < count; ++i) {
        remove(items, item->child(i));
    }
}

}

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    destinationsTimer(new QTimer(this)),
    statusTimer(new QTimer(this))
{
    this->fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    this->ui->setupUi(this);

    this->ui->deletingPushButton->setStyleSheet(
        disabledAdminButtonStyle);
    this->ui->deletingPushButton->setDisabled(true);
    this->ui->uniqueSizePushButton->setDisabled(true);
    this->ui->restoringPushButton->setDisabled(true);
    this->ui->verifyingPushButton->setDisabled(true);
    this->ui->mountPointsWidget->
        setSelectionMode(QAbstractItemView::SelectionMode::MultiSelection);

    this->tmutilPath = Settings::tmutilPath();
    this->sudoPath = Settings::sudoPath();

    this->ui->destinationsWidget->setTmutilPath(this->tmutilPath);
    this->ui->destinationsWidget->horizontalHeader()
        ->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);

    connect(this->ui->actionAbout, &QAction::triggered,
            this, &MainWindow::showAboutDialog);
    connect(this->ui->actionSettings, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);
    connect(this->ui->deletingPushButton, &QPushButton::pressed,
            this, &MainWindow::deleteSelectedPaths);
    connect(this->ui->uniqueSizePushButton, &QPushButton::pressed,
            this, &MainWindow::uniqueSizeSelectedPaths);
    connect(this->ui->restoringPushButton, &QPushButton::pressed,
            this, &MainWindow::restoreSelectedPaths);
    connect(this->ui->verifyingPushButton, &QPushButton::pressed,
            this, &MainWindow::verifySelectedPaths);

    connect(this->ui->destinationsWidget,
            &DestinationsWidget::failedToStartQuery,
            this, &MainWindow::handleQueryFailedToStart);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotPaths,
            this, &MainWindow::updateMountPointsView);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotError,
            this, &MainWindow::showStatus);
    connect(this->ui->destinationsWidget, &DestinationsWidget::gotDestinations,
            this, &MainWindow::handleGotDestinations);

    connect(this->ui->mountPointsWidget, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::selectedPathsChanged);
    connect(this->ui->mountPointsWidget, &QTreeWidget::itemExpanded,
            this, &MainWindow::mountPointItemExpanded);
    connect(this->ui->mountPointsWidget, &QTreeWidget::itemCollapsed,
            this, &MainWindow::mountPointItemCollapsed);

    connect(this->destinationsTimer, &QTimer::timeout,
            this->ui->destinationsWidget,
            &DestinationsWidget::queryDestinations);
    connect(this->statusTimer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);

    QTimer::singleShot(0, this, &MainWindow::readSettings);
    QTimer::singleShot(0,
                       this->ui->destinationsWidget,
                       &DestinationsWidget::queryDestinations);
    QTimer::singleShot(0, this, &MainWindow::checkTmStatus);
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
        constexpr auto policy =
            QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
        const auto pathsAdd = findAddableTopLevelItems(
            *(this->ui->mountPointsWidget), paths);
        for (const auto& path: pathsAdd) {
            // mp might be like "/Volumes/My Backup Disk"
            qDebug() << "adding new mountPoint path=" << path;
            const auto item = new QTreeWidgetItem(QTreeWidgetItem::UserType);
            item->setChildIndicatorPolicy(policy);
            item->setText(0, path);
            item->setToolTip(0, "A \"backup destination\".");
            item->setData(0, Qt::ItemDataRole::UserRole, path);
            item->setFont(0, this->fixedFont);
            item->setWhatsThis(0, QString("This is the file system info for '%1'")
                                      .arg(path));
            this->ui->mountPointsWidget->addTopLevelItem(item);
        }
    }
    if (paths.empty()) {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("No destination mounted!");
        msgBox.setInformativeText(
            "No backups or restores are currently possible!");
        msgBox.exec();
    }
}

void MainWindow::reportDir(const std::filesystem::path& dir,
                           std::error_code ec,
                           const QSet<QString>& filenames)
{
    if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
        qDebug() << "MainWindow::reportDir called for enoent";
        return;
    }
    const auto parent = ::findItem(*(this->ui->mountPointsWidget),
                                   dir.begin(), dir.end());
    if (!parent) {
        qDebug() << "MainWindow::reportDir parent not found";
        return;
    }
    const QString pathName =
        parent->data(0, Qt::ItemDataRole::UserRole).toString();
    if (!ec) {
        auto itemsToDelete = std::vector<QTreeWidgetItem*>{};
        const auto count = parent->childCount();
        for (auto i = 0; i < count; ++i) {
            const auto child = parent->child(i);
            if (!child) {
                continue;
            }
            const auto filename = child->text(0);
            if (!filenames.contains(filename)) {
                const auto key =
                    std::filesystem::path{pathName.toStdString() + "/" + filename.toStdString()};
                this->pathInfoMap.erase(key);
                itemsToDelete.push_back(child);
            }
        }
        if (!itemsToDelete.empty()) {
            qDebug() << "MainWindow::reportDir found" << itemsToDelete.size() << "items to delete";
        }
        for (auto* item: itemsToDelete) {
            delete item;
        }
        const auto deletedCount = static_cast<int>(itemsToDelete.size());
        const auto backupsCount = parent->text(backupsCountCol);
        if (!backupsCount.isEmpty()) {
            auto ok = false;
            parent->setText(backupsCountCol,
                            QString::number(backupsCount.toLongLong(&ok) - deletedCount));
        }
        return;
    }

    qDebug() << "MainWindow::reportDir called" << ec.message();

    parent->setToolTip(0, cantListDirWarning);
    parent->setBackground(0, QBrush(QColor(Qt::red)));

    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    // Following doesn't work on macos?
    msgBox.setTextFormat(Qt::TextFormat::MarkdownText);
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

void MainWindow::updateDirEntry(
    const std::filesystem::path& path,
    const std::filesystem::file_status& status,
    const QMap<QString, QByteArray>& attrs)
{
    const auto machineUuid = get(attrs, machineUuidAttr);
    const auto snapshotType = get(attrs, snapshotTypeAttr);
    const auto totalCopied = get(attrs, totalBytesCopiedAttr);
    const auto pathInfo = PathInfo{status, attrs};
    const auto res = this->pathInfoMap.emplace(path, pathInfo);
    const auto parent = ::findItem(*(this->ui->mountPointsWidget),
                                   path.begin(), --path.end());
    if (!parent) {
        qDebug() << "MainWindow::updateDirEntry parent not found";
        return;
    }
    auto item = static_cast<QTreeWidgetItem*>(nullptr);
    if (res.second) {
        item = new QTreeWidgetItem;
        item->setTextAlignment(filenameCol, Qt::AlignLeft|Qt::AlignVCenter);
        item->setFont(filenameCol, this->fixedFont);
        item->setText(filenameCol, QString::fromStdString(path.filename().string()));
        item->setData(filenameCol, Qt::ItemDataRole::UserRole, QString(path.c_str()));
        item->setToolTip(filenameCol, pathTooltip(attrs));
        item->setTextAlignment(backupsCountCol, Qt::AlignRight);
        item->setText(backupsCountCol, machineUuid? "?": "");
        item->setToolTip(backupsCountCol, machineUuid? "Expand to get value.": "");
        item->setTextAlignment(snapshotTypeCol, Qt::AlignRight);
        item->setTextAlignment(durationCol, Qt::AlignRight|Qt::AlignVCenter);
        item->setFont(durationCol, this->fixedFont);
        item->setTextAlignment(totalCopiedCol, Qt::AlignRight|Qt::AlignVCenter);
        item->setFont(totalCopiedCol, this->fixedFont);
        item->setTextAlignment(filesysTypeCol, Qt::AlignCenter);
        item->setTextAlignment(volBytesUseCol, Qt::AlignRight|Qt::AlignVCenter);
        item->setFont(volBytesUseCol, this->fixedFont);
        parent->addChild(item);
        if (snapshotType || totalCopied) {
            auto ok = false;
            const auto t = parent->text(backupsCountCol);
            parent->setText(backupsCountCol,
                            QString::number(t.toLongLong(&ok) + 1));
        }
    }
    else {
        item = ::findChild(parent, path).first;
        if (item->isExpanded()) {
            this->updatePathInfo(item);
        }
        if (res.first->second == pathInfo) {
            return;
        }
    }

    item->setText(snapshotTypeCol, QString{snapshotType.value_or(QByteArray{})});

    const auto begDate = get(attrs, snapshotStartAttr);
    // Note: snapshotFinishAttr attribute appears to be removed
    //   from backup directories by "tmutil delete -p <dir>".
    const auto endDate = get(attrs, snapshotFinishAttr);
    if (begDate && endDate) {
        auto begOkay = false;
        auto endOkay = false;
        const auto begMicroSecs = QString(*begDate).toLongLong(&begOkay);
        const auto endMicroSecs = QString(*endDate).toLongLong(&endOkay);
        if (begOkay && endOkay) {
            const auto elapsedMicroSecs = endMicroSecs - begMicroSecs;
            constexpr auto oneMillion = 1000000;
            const auto elapsedSecs =
                (elapsedMicroSecs + (oneMillion/2)) / oneMillion;
            const auto seconds = elapsedSecs % 60;
            const auto minutes = (elapsedSecs / 60) % 60;
            const auto hours = (elapsedSecs / 60 / 60);
            const auto timeString = QString("%1:%2:%3")
                                        .arg(hours, 1, 10, QChar('0'))
                                        .arg(minutes, 2, 10, QChar('0'))
                                        .arg(seconds, 2, 10, QChar('0'));
            item->setText(durationCol, timeString);
        }
    }
    else {
        item->setText(durationCol, QString{});
    }

    item->setText(totalCopiedCol, toMegaBytes(toLongLong(totalCopied)));
    item->setText(filesysTypeCol, QString{get(attrs, fileSystemTypeAttr).value_or(QByteArray{})});
    item->setText(volBytesUseCol, QString(get(attrs, volumeBytesUsedAttr).value_or(QByteArray{})));

    // Following may not work. For more info, see:
    // https://stackoverflow.com/q/30088705/7410358
    // https://bugreports.qt.io/browse/QTBUG-28312
    item->setChildIndicatorPolicy(toIndicatorPolicy(status.type()));
}

void MainWindow::updatePathInfo(QTreeWidgetItem *item)
{
    const QString pathName =
        item->data(0, Qt::ItemDataRole::UserRole).toString();
    auto *workerThread = new DirectoryReader(pathName.toStdString(), this);
    connect(workerThread, &DirectoryReader::ended,
            this, &MainWindow::reportDir);
    connect(workerThread, &DirectoryReader::entry,
            this, &MainWindow::updateDirEntry);
    connect(workerThread, &DirectoryReader::finished,
            workerThread, &QObject::deleteLater);
    connect(this, &MainWindow::destroyed,
            workerThread, &DirectoryReader::quit);
    workerThread->start();
}

void MainWindow::updatePathInfos()
{
    const auto count = this->ui->mountPointsWidget->topLevelItemCount();
    for (auto i = 0; i < count; ++i) {
        const auto item = this->ui->mountPointsWidget->topLevelItem(i);
        if (item->isExpanded()) {
            updatePathInfo(item);
        }
    }
}

void MainWindow::mountPointItemExpanded(QTreeWidgetItem *item)
{
    qDebug() << "got mount point expanded signal"
             << "for item:" << item->text(0);
    updatePathInfo(item);
    if (!this->pathInfoTimer) {
        this->pathInfoTimer = new QTimer{this};
        connect(this->pathInfoTimer, &QTimer::timeout, this, &MainWindow::updatePathInfos);
        this->pathInfoTimer->start(pathInfoUpdateTime);
    }
}

void MainWindow::mountPointItemCollapsed( // NOLINT(readability-convert-member-functions-to-static)
    QTreeWidgetItem *item)
{
    qDebug() << "got mount point collapsed signal"
             << "for item:" << item->text(0);
}

void MainWindow::resizeMountPointsColumns()
{
    const auto count = this->ui->mountPointsWidget->columnCount();
    for (auto col = 0; col < count; ++col) {
        this->ui->mountPointsWidget->resizeColumnToContents(col);
    }
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
    dialog->setTmutilPath(this->tmutilPath);
    dialog->setSudoPath(this->sudoPath);
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
    const auto selectedPaths = toStringList(
        this->ui->mountPointsWidget->selectedItems());
    qInfo() << "uniqueSizeSelectedPaths called for" << selectedPaths;

    const auto dialog = new PathActionDialog{this};
    {
        // Ensures output of tmutil shown as soon as available.
        auto env = dialog->environment();
        env.insert("STDBUF", "L"); // see "man 3 setbuf"
        dialog->setEnvironment(env);
    }
    dialog->setTmutilPath(this->tmutilPath);
    dialog->setWindowTitle("Unique Size Dialog");
    dialog->setText(
        "Are you sure that you want to uniquely size the following paths?");
    dialog->setPaths(selectedPaths);
    dialog->setAction(tmutilUniqueSizeVerb);
    dialog->show();
}

void MainWindow::restoreSelectedPaths()
{
    const auto selectedPaths = toStringList(
        this->ui->mountPointsWidget->selectedItems());
    qInfo() << "restoreSelectedPaths called for" << selectedPaths;

    QFileDialog dstDialog{this};
    dstDialog.setWindowTitle(tr("Destination Directory"));
    dstDialog.setDirectory("/");
    dstDialog.setLabelText(QFileDialog::Accept, "Select Destination");
    dstDialog.setFileMode(QFileDialog::Directory);
    //dialog.setOptions(QFileDialog::DontUseNativeDialog);
    dstDialog.setFilter(QDir::Hidden|QDir::Dirs|QDir::Drives);
    dstDialog.setNameFilter("*");
    if (!dstDialog.exec()) {
        return;
    }
    const auto files = dstDialog.selectedFiles();
    qDebug() << "openFileDialog:" << files;
    if (files.isEmpty()) {
        return;
    }
    qDebug() << files;

    // open path action dialog.
    const auto dialog = new PathActionDialog{this};
    {
        // Ensures output of tmutil shown as soon as available.
        auto env = dialog->environment();
        env.insert("STDBUF", "L"); // see "man 3 setbuf"
        dialog->setEnvironment(env);
    }
    dialog->setTmutilPath(this->tmutilPath);
    dialog->setWindowTitle("Restore Dialog");
    dialog->setText(QString(
        "Are you sure that you want to restore the following paths to '%2'?")
                        .arg(files.first()));
    dialog->setFirstArgs(QStringList() << "-v");
    dialog->setPaths(selectedPaths);
    dialog->setLastArgs(files);
    dialog->setAction(tmutilRestoreVerb);
    dialog->show();
}

void MainWindow::verifySelectedPaths()
{
    const auto selectedPaths = toStringList(
        this->ui->mountPointsWidget->selectedItems());
    qInfo() << "verifySelectedPaths called for" << selectedPaths;

    const auto dialog = new PathActionDialog{this};
    {
        // Ensures output of tmutil shown as soon as available.
        auto env = dialog->environment();
        env.insert("STDBUF", "L"); // see "man 3 setbuf"
        dialog->setEnvironment(env);
    }
    dialog->setTmutilPath(this->tmutilPath);
    dialog->setWindowTitle("Verify Dialog");
    dialog->setText(
        "Are you sure that you want to verify the following paths?");
    dialog->setPaths(selectedPaths);
    dialog->setAction(tmutilVerifyVerb);
    dialog->show();
}

void MainWindow::selectedPathsChanged()
{
    const auto selectionIsEmpty = this->ui->mountPointsWidget->
                                  selectedItems().empty();
    this->ui->deletingPushButton->setStyleSheet(
        selectionIsEmpty?
            disabledAdminButtonStyle:
            enabledAdminButtonStyle);
    this->ui->deletingPushButton->setDisabled(selectionIsEmpty);
    this->ui->uniqueSizePushButton->setDisabled(selectionIsEmpty);
    this->ui->restoringPushButton->setDisabled(selectionIsEmpty);
    this->ui->verifyingPushButton->setDisabled(selectionIsEmpty);
}

void MainWindow::showAboutDialog()
{
    QString text;
    text.append(QString("%1 %2.%3")
                    .arg(this->windowTitle())
                    .arg(VERSION_MAJOR)
                    .arg(VERSION_MINOR));
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
    qDebug() << "showSettingsDialog called";
    const auto dialog = new SettingsDialog{this};
    connect(dialog, &SettingsDialog::tmutilPathChanged,
            this, &MainWindow::handleTmutilPathChange);
    connect(dialog, &SettingsDialog::sudoPathChanged,
            this, &MainWindow::handleSudoPathChange);
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
    process->start(this->tmutilPath,
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
    disconnect(this->destinationsTimer, &QTimer::timeout,
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

void MainWindow::handleGotDestinations(int count)
{
    if (count == 0) {
        this->ui->destinationInfoLabel->setText(
            tr("Destination Info - no destinations appear setup!"));
        errorMessage.showMessage(
            QString("%1 %2")
                .arg("No destinations appear setup.",
                     "Add a destination to Time Machine as soon as you can."));
    }
    else {
        this->ui->destinationInfoLabel->setText(
            tr("Destination Info"));
    }
}

void MainWindow::handleTmutilPathChange(const QString &path)
{
    qDebug() << "MainWindow::handleTmutilPathChange called:"
             << path;

    disconnect(this->statusTimer, &QTimer::timeout,
               this, &MainWindow::checkTmStatus);
    disconnect(this->destinationsTimer, &QTimer::timeout,
               this->ui->destinationsWidget,
               &DestinationsWidget::queryDestinations);

    this->tmutilPath = path;
    this->ui->destinationsWidget->setTmutilPath(path);

    connect(this->statusTimer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);
    connect(this->destinationsTimer, &QTimer::timeout,
            this->ui->destinationsWidget,
            &DestinationsWidget::queryDestinations);
}

void MainWindow::handleSudoPathChange(const QString &path)
{
    qDebug() << "MainWindow::handleSudoPathChange called:"
             << path;
    this->sudoPath = path;
}

void MainWindow::handleTmStatusNoPlist()
{
    qDebug() << "handleTmStatusNoPlist called";

    disconnect(this->statusTimer, &QTimer::timeout,
               this, &MainWindow::checkTmStatus);
    QMessageBox msgBox;
    msgBox.setStandardButtons(QMessageBox::Open);
    msgBox.setOptions(QMessageBox::Option::DontUseNativeDialog);
    // macOS ignores following, but set in case changes
    msgBox.setWindowTitle("Error!");
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
    qint64 lineNumber, int error, const QString &text)
{
    qDebug() << "handleTmStatusReaderError called:" << text;
    qDebug() << "line #" << lineNumber;
    qDebug() << "error" << error;
    this->showStatus(
        QString("Error reading Time Machine status: line %1, %2")
            .arg(lineNumber)
            .arg(text));
}

void MainWindow::handleTmStatusFinished(int code, int status)
{
    switch (QProcess::ExitStatus(status)) {
    case QProcess::NormalExit:
        break;
    case QProcess::CrashExit:
        this->showStatus(
            QString("When getting status: %1 exited abnormally")
                .arg(toolName));
        return;
    }
    if (code != 0) {
        this->showStatus(
            QString("When getting status: %1 exited with code %2")
                .arg(toolName).arg(code));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "saving mainwindow geometry & state";
    Settings::setMainWindowGeometry(saveGeometry());
    Settings::setMainWindowState(saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::readSettings()
{
    if (!this->restoreGeometry(Settings::mainWindowGeometry())) {
        qDebug() << "unable to restore previous geometry";
    }
    if (!this->restoreState(Settings::mainWindowState())) {
        qDebug() << "unable to restore previous state";
    }
    this->destinationsTimer->start(Settings::tmutilDestInterval());
    this->statusTimer->start(Settings::tmutilStatInterval());
}
