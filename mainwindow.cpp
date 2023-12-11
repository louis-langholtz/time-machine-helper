#include <algorithm> // std::find_if_not
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <utility> // for std::pair
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
#include <QMetaType>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMouseEvent>

#include "ui_mainwindow.h"
#include "directoryreader.h"
#include "itemdefaults.h"
#include "mainwindow.h"
#include "pathactiondialog.h"
#include "plistprocess.h"
#include "settings.h"
#include "settingsdialog.h"
#include "sortingdisabler.h"

namespace {

constexpr auto toolName = "Time Machine utility";

constexpr auto timeMachineAttrPrefix = "com.apple.timemachine.";
constexpr auto backupAttrPrefix = "com.apple.backup.";
constexpr auto backupdAttrPrefix = "com.apple.backupd.";

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
// version 4 appears to add "com.apple.backupd.fstypename" attr to volumes
constexpr auto snapshotVersionAttr  = "com.apple.backup.SnapshotVersion";
constexpr auto snapshotStateAttr    = "com.apple.backupd.SnapshotState";
constexpr auto snapshotNumberAttr   = "com.apple.backup.SnapshotNumber";

// Volume level attributes...
constexpr auto fileSystemTypeAttr   = "com.apple.backupd.fstypename";
constexpr auto volumeBytesUsedAttr  = "com.apple.backupd.VolumeBytesUsed";
constexpr auto volumeUuidAttr       = "com.apple.backupd.SnapshotVolumeUUID";

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
constexpr auto maxToolTipStringList = 10;

// A namespace scoped enum for the machines table columns...
namespace MachinesColumn {
enum Enum: int {
    Name = 0,
    Uuid,
    Model,
    Address,
    Destinations,
    Volumes,
    Backups,
};
}

// A namespace scoped enum for the backups table columns...
namespace BackupsColumn {
enum Enum: int {
    Name = 0,
    Type,
    Version,
    State,
    Number,
    Duration,
    Size,
    Volumes,
    Machine,
    Destination,
};
}

namespace VolumesColumn {
enum Enum: int {
    Name = 0,
    Uuid,
    Type,
    MaxUsed,
    Machines,
    Destinations,
    Backups,
};
}

constexpr auto itemFlags =
    Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsUserCheckable;

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

auto toStringList(const QList<QTableWidgetItem *> &items)
    -> QStringList
{
    QStringList result;
    for (const auto* item: items) {
        const auto string = item->data(Qt::UserRole).toString();
        if (!string.isEmpty()) {
            result += string;
        }
    }
    return result;
}

auto toStringList(const QSet<QString>& strings,
                  const int max = -1,
                  const QString &etc = "...")
    -> QStringList
{
    QStringList result;
    for (const auto& string: strings) {
        if ((max >= 0) && (max <= result.size())) {
            result << etc;
            break;
        }
        result << string;
    }
    return result;
}

auto anyStartsWith(const QMap<QString, QByteArray>& attrs,
                   const QStringList& prefices) -> bool
{
    auto first = attrs.keyBegin();
    const auto last = attrs.keyEnd();
    for (auto it = first; it != last; ++it) {
        for (const auto& prefix: prefices) {
            if (it->startsWith(prefix)) {
                return true;
            }
        }
    }
    return false;
}

auto toIndicatorPolicy(const std::filesystem::file_type& file_type)
    -> QTreeWidgetItem::ChildIndicatorPolicy
{
    using QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    using QTreeWidgetItem::ChildIndicatorPolicy::DontShowIndicator;
    return (file_type == std::filesystem::file_type::directory)
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
    const auto& names) -> std::set<QString>
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
    const auto& names) -> std::set<QString>
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

auto findRow(const QTableWidget& table,
             const std::initializer_list<std::pair<int, QString>>& keys)
    -> int
{
    const auto count = table.rowCount();
    for (auto row = 0; row < count; ++row) {
        auto found = true;
        for (const auto& key: keys) {
            const auto item = table.item(row, key.first);
            if (!item || item->text() != key.second) {
                found = false;
                break;
            }
        }
        if (found) {
            return row;
        }
    }
    return -1;
}

auto findChild(QTreeWidgetItem *parent,
               const std::filesystem::path& path)
    -> std::pair<QTreeWidgetItem*, int>
{
    const auto count = parent->childCount();
    for (auto i = 0; i < count; ++i) {
        const auto child = parent->child(i);
        const QString pathName =
            child->data(0, Qt::UserRole).toString();
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

auto duration(const QMap<QString, QByteArray>& attrs)
    -> std::optional<std::chrono::seconds>
{
    const auto begData = get(attrs, snapshotStartAttr);
    // Note: snapshotFinishAttr attribute appears to be removed
    //   from backup directories by "tmutil delete -p <dir>".
    const auto endData = get(attrs, snapshotFinishAttr);
    if (begData && endData) {
        using namespace std::chrono_literals;
        auto begOk = false;
        auto endOk = false;
        const auto beg = QString(*begData).toLongLong(&begOk) * 1us;
        const auto end = QString(*endData).toLongLong(&endOk) * 1us;
        if (begOk && endOk) {
            return {
                std::chrono::duration_cast<std::chrono::seconds>(end - beg)
            };
        }
    }
    return {};
}

auto concatenate(const std::filesystem::path::iterator& first,
                 const std::filesystem::path::iterator& last)
    -> std::filesystem::path
{
    auto result = std::filesystem::path{};
    for (auto it = first; it != last; ++it) {
        result /= *it;
    }
    return result;
}

void resizeColumnsToContents(QTableWidget* table)
{
    const auto count = table->columnCount();
    for (auto col = 0; col < count; ++col) {
        table->resizeColumnToContents(col);
    }
}

auto removeLast(std::input_iterator auto first,
                std::input_iterator auto& last)
{
    using OT = decltype(*last);
    if (last == first) {
        return OT{};
    }
    --last;
    return OT{*last};
}

auto isStorageDir(const QMap<QString, QByteArray>& attrs)
    -> bool
{
    const auto timeMachineMeta = get(attrs, timeMachineMetaAttr);
    return timeMachineMeta &&
           timeMachineMeta->startsWith("SnapshotStorage");
}

auto isMachineDir(const QMap<QString, QByteArray>& attrs)
    -> bool
{
    const auto machineUuid = get(attrs, machineUuidAttr);
    const auto machineAddr = get(attrs, machineMacAddrAttr);
    const auto machineModel = get(attrs, machineModelAttr);
    const auto machineName = get(attrs, machineCompNameAttr);
    return machineUuid || machineAddr || machineModel || machineName;
}

auto isVolumeDir(const QMap<QString, QByteArray>& attrs)
    -> bool
{
    return get(attrs, snapshotTypeAttr) ||
           get(attrs, totalBytesCopiedAttr);
}

auto toString(const std::optional<QByteArray> &data)
    -> std::optional<QString>
{
    if (!data) {
        return {};
    }
    auto first = data->rbegin();
    const auto last = data->rend();
    for (; first != last; ++first) {
        if (*first != 0) {
            break;
        }
    }
    return {QString::fromUtf8(QByteArrayView{data->data(), &(*first) + 1})};
}

}

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    destinationsTimer(new QTimer(this)),
    statusTimer(new QTimer(this)),
    pathInfoTimer(new QTimer{this})
{
    this->fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    this->ui->setupUi(this);

    this->ui->deletingPushButton->setStyleSheet(
        disabledAdminButtonStyle);
    this->ui->deletingPushButton->setDisabled(true);
    this->ui->uniqueSizePushButton->setDisabled(true);
    this->ui->restoringPushButton->setDisabled(true);
    this->ui->verifyingPushButton->setDisabled(true);
    this->ui->backupsTable->
        setSelectionMode(QAbstractItemView::SelectionMode::MultiSelection);
    this->ui->backupsTable->setMouseTracking(true);
    this->ui->backupsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    this->tmutilPath = Settings::tmutilPath();
    this->sudoPath = Settings::sudoPath();

    this->ui->destinationsTable->setTmutilPath(this->tmutilPath);
    this->ui->destinationsTable->horizontalHeader()
        ->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);

    connect(this->ui->actionAbout, &QAction::triggered,
            this, &MainWindow::showAboutDialog);
    connect(this->ui->actionSettings, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);
    connect(this->ui->deletingPushButton, &QPushButton::pressed,
            this, &MainWindow::deleteSelectedBackups);
    connect(this->ui->uniqueSizePushButton, &QPushButton::pressed,
            this, &MainWindow::uniqueSizeSelectedPaths);
    connect(this->ui->restoringPushButton, &QPushButton::pressed,
            this, &MainWindow::restoreSelectedPaths);
    connect(this->ui->verifyingPushButton, &QPushButton::pressed,
            this, &MainWindow::verifySelectedBackups);

    connect(this->ui->destinationsTable,
            &DestinationsWidget::failedToStartQuery,
            this, &MainWindow::handleQueryFailedToStart);
    connect(this->ui->destinationsTable, &DestinationsWidget::gotPaths,
            this, &MainWindow::updateMountPointsView);
    connect(this->ui->destinationsTable, &DestinationsWidget::gotError,
            this, &MainWindow::showStatus);
    connect(this->ui->destinationsTable, &DestinationsWidget::gotDestinations,
            this, &MainWindow::handleGotDestinations);

    connect(this->ui->destinationsTable, &DestinationsWidget::itemChanged,
            this, &MainWindow::handleItemChanged);
    connect(this->ui->machinesTable, &QTableWidget::itemChanged,
            this, &MainWindow::handleItemChanged);
    connect(this->ui->volumesTable, &QTableWidget::itemChanged,
            this, &MainWindow::handleItemChanged);

    connect(this->ui->backupsTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::selectedBackupsChanged);

    connect(this->destinationsTimer, &QTimer::timeout,
            this->ui->destinationsTable,
            &DestinationsWidget::queryDestinations);
    connect(this->statusTimer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);

    QTimer::singleShot(0, this, &MainWindow::readSettings);
    QTimer::singleShot(0,
                       this->ui->destinationsTable,
                       &DestinationsWidget::queryDestinations);
    QTimer::singleShot(0, this, &MainWindow::checkTmStatus);
}

MainWindow::~MainWindow()
{
    delete this->ui;
}

void MainWindow::updateMountPointsView(
    const std::map<std::string, plist_dict>& mountPoints)
{
    this->mountMap = mountPoints;
    if (mountPoints.empty()) {
        disconnect(this->ui->destinationsTable, &DestinationsWidget::gotPaths,
                   this, &MainWindow::updateMountPointsView);
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("No destination accessible!");
        msgBox.setInformativeText(
            "No backups or restores are possible when no destinations are accessible!");
        msgBox.exec();
        connect(this->ui->destinationsTable, &DestinationsWidget::gotPaths,
                this, &MainWindow::updateMountPointsView);
        return;
    }
    for (const auto& mountPoint: mountPoints) {
        this->updatePathInfo(mountPoint.first);
    }
}

void MainWindow::handleDirectoryReaderEnded(
    const std::filesystem::path& dir,
    std::error_code ec,
    const QSet<QString>& filenames)
{
    if (!ec) {
        this->reportDir(dir, filenames);
        return;
    }

    qDebug() << "MainWindow::handleDirectoryReaderEnded called for" << ec.message();

    if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
        return;
    }

    if (this->mountMap.contains(dir)) {
        this->destinationsTimer->stop();
    }

    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    // Following doesn't work on macos?
    msgBox.setTextFormat(Qt::TextFormat::MarkdownText);
    msgBox.setWindowTitle("Error!");
    msgBox.setText(QString("Unable to list contents of directory `%1`")
                       .arg(QString::fromStdString(dir.string())));
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

void MainWindow::reportDir(
    const std::filesystem::path& dir,
    const QSet<QString>& filenames)
{
    const auto it = this->pathInfoMap.find(dir);
    if (it == this->pathInfoMap.end()) {
        return;
    }
    const auto& attrs = it->second.attributes;
    if (isStorageDir(attrs)) {
        updateStorageDir();
        return;
    }
    if (isMachineDir(attrs)) {
        updateMachineDir(dir, filenames);
        return;
    }
    if (isVolumeDir(attrs)) {
        updateVolumeDir(dir, filenames);
        return;
    }
}

void MainWindow::updateStorageDir()
{
    resizeColumnsToContents(this->ui->machinesTable);
}

void MainWindow::updateMachineDir(const std::filesystem::path& dir,
                                  const QSet<QString>& filenames)
{
    const auto first = dir.begin();
    auto last = dir.end();
    const auto machName = QString::fromStdString(removeLast(first, last));
    (void)removeLast(first, last);
    const auto destName = QString::fromStdString(removeLast(first, last));

    auto backupsToDelete = QSet<QString>{};
    auto rowsToDelete = std::vector<int>{};
    const auto rows = this->ui->backupsTable->rowCount();
    for (auto row = 0; row < rows; ++row) {
        const auto nameItem = this->ui->backupsTable->item(row, BackupsColumn::Name);
        const auto machineItem = this->ui->backupsTable->item(row, BackupsColumn::Machine);
        const auto destItem = this->ui->backupsTable->item(row, BackupsColumn::Destination);
        if (!filenames.contains(nameItem->text()) &&
            (machineItem->text() == machName) &&
            (destItem->text() == destName)) {
            rowsToDelete.push_back(row);
            backupsToDelete.insert(nameItem->text());
        }
    }
    for (auto row: rowsToDelete) {
        this->ui->backupsTable->removeRow(row);
    }
    const auto deletedCount = static_cast<int>(rowsToDelete.size());
    if (deletedCount > 0) {
        qDebug() << "MainWindow::reportDir deleted would be" << deletedCount;
        const auto tbl = this->ui->volumesTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            if (const auto item = tbl->item(row, VolumesColumn::Machines)) {
                if (item->text() != machName) {
                    continue;
                }
            }
            if (const auto item = tbl->item(row, VolumesColumn::Backups)) {
                item->setData(Qt::UserRole, QVariant::fromValue(QSet<QString>{}));
            }
        }
    }
    if (const auto foundRow = findRow(*this->ui->machinesTable,
                                      {{MachinesColumn::Name, machName}});
        foundRow >= 0) {
        if (const auto item = this->ui->machinesTable->item(
                foundRow, MachinesColumn::Backups)) {
            auto set = item->data(Qt::UserRole).value<QSet<QString>>();
            set.removeIf([&](const QString& name){
                return backupsToDelete.contains(name);
            });
            set.unite(filenames);
            item->setData(Qt::UserRole, QVariant::fromValue(set));
            item->setData(Qt::DisplayRole, set.size());
            item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
        }
    }
    resizeColumnsToContents(this->ui->backupsTable);
}

void MainWindow::updateVolumeDir(const std::filesystem::path& dir,
                                 const QSet<QString>& filenames)
{
    const auto first = dir.begin();
    auto last = dir.end();
    const auto backupName = QString::fromStdString(removeLast(first, last));
    const auto machName = QString::fromStdString(removeLast(first, last));
    (void)removeLast(first, last);
    const auto destName = QString::fromStdString(removeLast(first, last));
    const auto foundRow = findRow(*this->ui->backupsTable,
                                  {{BackupsColumn::Name, backupName},
                                   {BackupsColumn::Machine, machName},
                                   {BackupsColumn::Destination, destName}});
    if (foundRow < 0) {
        return;
    }
    const auto item = this->ui->backupsTable->item(foundRow, BackupsColumn::Volumes);
    if (!item) {
        return;
    }
    item->setData(Qt::UserRole, QVariant::fromValue(filenames));
    item->setData(Qt::DisplayRole, filenames.size());
    item->setToolTip(toStringList(filenames, maxToolTipStringList).join(", "));
}

void MainWindow::updateDirEntry(
    const std::filesystem::path& path,
    const std::filesystem::file_status& status,
    const QMap<QString, QByteArray>& attrs)
{
    const auto pathInfo = PathInfo{status, attrs};
    const auto res = this->pathInfoMap.emplace(path, pathInfo);
    const auto changed = res.second || (res.first->second != pathInfo);
    if (!res.second) {
        res.first->second = pathInfo;
    }

    if (isStorageDir(attrs)) {
        // This is the "Backups.backupdb" like directory, just go deeper...
        this->updatePathInfo(path);
        return;
    }

    const auto filename = path.filename().string();

    if (isMachineDir(attrs)) {
        auto end = path.end(); --end; --end;
        const auto mp = concatenate(path.begin(), end);
        const auto it = this->mountMap.find(mp.string());
        this->updateMachine(filename, attrs,
                            ((it != this->mountMap.end())? it->second: plist_dict{}));
        this->updatePathInfo(path);
        return;
    }

    if (isVolumeDir(attrs)) {
        if (changed) {
            this->updateBackups(path, attrs);
        }
        this->updatePathInfo(path);
        return;
    }

    const auto fileSystemType = get(attrs, fileSystemTypeAttr);
    const auto volumeBytesUsed = get(attrs, volumeBytesUsedAttr);
    if (fileSystemType || volumeBytesUsed) {
        if (changed) {
            this->updateVolumes(path, attrs);
        }
        return;
    }
}

void MainWindow::updateMachine(const std::string& name,
                               const QMap<QString, QByteArray>& attrs,
                               const plist_dict &dict)
{
    const auto machineUuid = toString(get(attrs, machineUuidAttr));
    const auto machineAddr = toString(get(attrs, machineMacAddrAttr));
    const auto machineModel = toString(get(attrs, machineModelAttr));
    const auto machineName = get(attrs, machineCompNameAttr);
    const auto destination = get<plist_string>(dict, plist_string{"Name"});
    const auto destName = QString::fromStdString(destination.value_or(""));
    const auto machName = QString::fromStdString(name);
    const auto uuid = machineUuid.value_or(QString{});

    const auto tbl = this->ui->machinesTable;
    const SortingDisabler disableSort{tbl};
    const auto res = this->machineMap.emplace(
        machineUuid.value_or(machName),
        MachineInfo{});
    res.first->second.destinations.insert(destName);
    res.first->second.attributes.insert(attrs);
    const auto foundRow = findRow(*tbl, {{MachinesColumn::Name, machName},
                                         {MachinesColumn::Uuid, uuid}});
    const auto row = (foundRow < 0)? tbl->rowCount(): foundRow;
    if (foundRow < 0) {
        tbl->insertRow(row);
    }
    constexpr auto checked = std::optional<bool>{true};
    constexpr auto alignRight = Qt::AlignRight|Qt::AlignVCenter;
    const auto flags = Qt::ItemFlags{Qt::ItemIsEnabled};
    const auto opts = ItemDefaults{}.use(flags);
    const auto font =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (const auto item = createdItem(tbl, row, MachinesColumn::Name,
                                      ItemDefaults{}.use(flags|Qt::ItemIsUserCheckable)
                                          .use(checked))) {
        item->setText(machName);
    }
    if (const auto item = createdItem(tbl, row, MachinesColumn::Uuid,
                                      ItemDefaults{opts}.use(font))) {
        item->setText(uuid);
    }
    if (const auto item = createdItem(tbl, row, MachinesColumn::Model, opts)) {
        item->setText(machineModel.value_or(QString{}));
    }
    if (const auto item = createdItem(tbl, row, MachinesColumn::Address,
                                      ItemDefaults{opts}.use(font))) {
        item->setText(machineAddr.value_or(QString{}));
    }
    if (const auto item = createdItem(tbl, row, MachinesColumn::Destinations,
                                      ItemDefaults{opts}.use(font).use(alignRight))) {
        auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        set.insert(destName);
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setData(Qt::DisplayRole, set.size());
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
    }
    if (const auto item = createdItem(tbl, row, MachinesColumn::Volumes,
                                      ItemDefaults{opts}.use(font).use(alignRight))) {
        const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setData(Qt::DisplayRole, set.size());
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
    }
    if (const auto item = createdItem(tbl, row, MachinesColumn::Backups,
                                      ItemDefaults{opts}.use(font).use(alignRight))) {
        const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setData(Qt::DisplayRole, set.size());
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
    }
}

void MainWindow::updateBackups(const std::filesystem::path& path,
                               const QMap<QString, QByteArray>& attrs)
{
    const auto filename = path.filename().string();
    const auto first = path.begin();
    auto last = path.end();
    const auto backupName = QString::fromStdString(removeLast(first, last));
    const auto machName = QString::fromStdString(removeLast(first, last));
    (void) removeLast(first, last); // skip
    const auto destName = QString::fromStdString(removeLast(first, last));
    if (backupName.isEmpty() || machName.isEmpty()) {
        qWarning() << "MainWindow::updateBackups empty name?";
        return;
    }

    const auto tbl = this->ui->backupsTable;
    const SortingDisabler disableSort{tbl};
    const auto font =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);

    constexpr auto alignRight = Qt::AlignRight|Qt::AlignVCenter;
    constexpr auto alignLeft = Qt::AlignLeft|Qt::AlignVCenter;

    auto foundRow = findRow(*tbl, {{BackupsColumn::Name, backupName},
                                   {BackupsColumn::Machine, machName}});
    if (foundRow < 0) {
        foundRow = tbl->rowCount();
        tbl->insertRow(foundRow);
    }
    const auto flags = Qt::ItemIsEnabled|Qt::ItemIsSelectable;
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Name,
                                      ItemDefaults{}.use(flags).use(font))) {
        item->setText(backupName);
        item->setData(Qt::UserRole, QString::fromStdString(path));
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Type,
                                      ItemDefaults{}.use(flags))) {
        item->setText(toString(get(attrs, snapshotTypeAttr)).value_or(""));
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Version,
                                      ItemDefaults{}.use(flags).use(alignRight).use(font))) {
        const auto num = toLongLong(get(attrs, snapshotVersionAttr));
        item->setData(Qt::DisplayRole, num? QVariant::fromValue(*num): QVariant{});
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::State,
                                      ItemDefaults{}.use(flags))) {
        item->setText(toString(get(attrs, snapshotStateAttr)).value_or(""));
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Number,
                                      ItemDefaults{}.use(flags).use(alignRight).use(font))) {
        const auto num = toLongLong(get(attrs, snapshotNumberAttr));
        item->setData(Qt::DisplayRole, num? QVariant::fromValue(*num): QVariant{});
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Duration,
                                      ItemDefaults{}.use(flags).use(alignRight).use(font))) {
        const auto time = duration(attrs);
        item->setData(Qt::DisplayRole, time? QVariant::fromValue(*time): QVariant{});
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Size,
                                      ItemDefaults{}.use(flags).use(alignRight).use(font))) {
        const auto num = toLongLong(get(attrs, totalBytesCopiedAttr));
        item->setData(Qt::DisplayRole, num? QVariant::fromValue(*num): QVariant{});
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Volumes,
                                      ItemDefaults{}.use(flags).use(alignRight).use(font))) {
        const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Machine,
                                      ItemDefaults{}.use(flags))) {
        item->setText(machName);
    }
    if (const auto item = createdItem(tbl, foundRow, BackupsColumn::Destination,
                                      ItemDefaults{}.use(flags))) {
        item->setText(destName);
    }
}

void MainWindow::updateVolumes(const std::filesystem::path& path,
                               const QMap<QString, QByteArray>& attrs)
{
    const auto fsType = toString(get(attrs, fileSystemTypeAttr)).value_or("");
    const auto volumeBytesUsed = get(attrs, volumeBytesUsedAttr);
    const auto volumeUuid = toString(get(attrs, volumeUuidAttr)).value_or("");
    const auto first = path.begin();
    auto last = path.end();
    const auto volumeName = QString::fromStdString(removeLast(first, last));
    const auto backupName = QString::fromStdString(removeLast(first, last));
    const auto machName = QString::fromStdString(removeLast(first, last));
    (void) removeLast(first, last); // skip
    const auto destName = QString::fromStdString(removeLast(first, last));
    const auto mountPoint = concatenate(first, last);
    if (volumeName.isEmpty() || backupName.isEmpty() || machName.isEmpty()) {
        qWarning() << "MainWindow::updateVolumes empty name?";
        return;
    }
    const auto tbl = this->ui->volumesTable;
    const SortingDisabler disableSort{tbl};
    const auto font =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    constexpr auto flags = Qt::ItemFlags{Qt::ItemIsEnabled};
    constexpr auto checked = std::optional<bool>{true};
    constexpr auto alignRight = Qt::AlignRight|Qt::AlignVCenter;
    constexpr auto alignLeft = Qt::AlignLeft|Qt::AlignVCenter;
    const auto foundRow = findRow(*tbl, {{VolumesColumn::Name, volumeName},
                                         {VolumesColumn::Uuid, volumeUuid}});
    const auto row = (foundRow < 0)? tbl->rowCount(): foundRow;
    if (foundRow < 0) {
        tbl->insertRow(row);
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::Name,
                                      ItemDefaults{}.use(flags|Qt::ItemIsUserCheckable)
                                                    .use(checked))) {
        item->setText(volumeName);
        item->setData(Qt::UserRole, QString::fromStdString(path));
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::Uuid,
                                      ItemDefaults{}.use(font))) {
        item->setText(volumeUuid);
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::Type)) {
        if (!fsType.isEmpty()) { // older backups don't store type attr
            item->setText(fsType);
        }
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::MaxUsed,
                                      ItemDefaults{}.use(font).use(alignRight))) {
        const auto text = item->text();
        auto ok = false;
        const auto before = text.toLongLong(&ok);
        const auto latest = QString(volumeBytesUsed.value_or("")).toLongLong(&ok);
        const auto used = std::max(before, latest);
        item->setData(Qt::DisplayRole, used);
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::Machines,
                                      ItemDefaults{}.use(font).use(alignRight))) {
        auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        set.insert(machName);
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setData(Qt::DisplayRole, set.size());
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
        {
            const auto machTbl = this->ui->machinesTable;
            const SortingDisabler disableMachSort{machTbl};
            const auto mr =
                findRow(*machTbl, {{MachinesColumn::Name, machName}});
            if (mr >= 0) {
                if (const auto item = createdItem(machTbl, mr, MachinesColumn::Volumes,
                                                  ItemDefaults{}.use(font).use(alignRight))) {
                    auto set = item->data(Qt::UserRole).value<QSet<QString>>();
                    set.insert(volumeName);
                    item->setData(Qt::UserRole, QVariant::fromValue(set));
                    item->setData(Qt::DisplayRole, set.size());
                    item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
                }
            }
        }
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::Destinations,
                                      ItemDefaults{}.use(font).use(alignRight))) {
        auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        set.insert(destName);
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setData(Qt::DisplayRole, set.size());
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
    }
    if (const auto item = createdItem(tbl, row, VolumesColumn::Backups,
                                      ItemDefaults{}.use(font).use(alignRight))) {
        auto set = item->data(Qt::UserRole).value<QSet<QString>>();
        set.insert(backupName);
        item->setData(Qt::UserRole, QVariant::fromValue(set));
        item->setData(Qt::DisplayRole, set.size());
        item->setToolTip(toStringList(set, maxToolTipStringList).join(", "));
    }
    if (foundRow < 0) {
        resizeColumnsToContents(tbl);
    }
}

void MainWindow::updatePathInfo(const std::string& pathName)
{
    auto *workerThread = new DirectoryReader(pathName, this);
    connect(workerThread, &DirectoryReader::ended,
            this, &MainWindow::handleDirectoryReaderEnded);
    connect(workerThread, &DirectoryReader::entry,
            this, &MainWindow::updateDirEntry);
    connect(workerThread, &DirectoryReader::finished,
            workerThread, &QObject::deleteLater);
    connect(this, &MainWindow::destroyed,
            workerThread, &DirectoryReader::quit);
    workerThread->start();
}

void MainWindow::deleteSelectedBackups()
{
    const auto selectedPaths = toStringList(
        this->ui->backupsTable->selectedItems());
    qInfo() << "deleteSelectedBackups called for" << selectedPaths;

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
    dialog->setText("Are you sure that you want to delete the following backups?");
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
        this->ui->backupsTable->selectedItems());
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
        this->ui->backupsTable->selectedItems());
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

void MainWindow::verifySelectedBackups()
{
    const auto selectedPaths = toStringList(
        this->ui->backupsTable->selectedItems());
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

void MainWindow::selectedBackupsChanged()
{
    const auto selected = this->ui->backupsTable->selectedItems();
    const auto empty = selected.isEmpty();
    this->ui->deletingPushButton->setStyleSheet(
        empty
            ? disabledAdminButtonStyle
            : enabledAdminButtonStyle);
    this->ui->deletingPushButton->setEnabled(!empty);
    this->ui->verifyingPushButton->setEnabled(!empty);
    this->ui->uniqueSizePushButton->setEnabled(!empty);
    this->ui->restoringPushButton->setEnabled(!empty);
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
    connect(dialog, &SettingsDialog::tmutilStatusIntervalChanged,
            this, &MainWindow::changeTmutilStatusInterval);
    connect(dialog, &SettingsDialog::tmutilDestinationsIntervalChanged,
            this, &MainWindow::changeTmutilDestinationsInterval);
    connect(dialog, &SettingsDialog::pathInfoIntervalChanged,
            this, &MainWindow::changePathInfoInterval);

    connect(dialog, &SettingsDialog::finished,
            dialog, &SettingsDialog::deleteLater);
    dialog->exec();
}

void MainWindow::checkTmStatus()
{
    // need to call "tmutil status -X"
    auto *process = new PlistProcess{this};
    connect(process, &PlistProcess::gotPlist,
            this->ui->destinationsTable, &DestinationsWidget::handleStatus);
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
               this->ui->destinationsTable,
               &DestinationsWidget::queryDestinations);
    constexpr auto queryFailedMsg = "Unable to start destinations query";
    const auto tmutilPath = this->ui->destinationsTable->tmutilPath();
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
        this->ui->destinationsLabel->setText(
            tr("Destinations - none appear setup!"));
        errorMessage.showMessage(
            QString("%1 %2")
                .arg("No destinations appear setup.",
                     "Add a destination to Time Machine as soon as you can."));
    }
    else {
        this->ui->destinationsLabel->setText(
            tr("Destinations"));
    }
}

void MainWindow::handleTmutilPathChange(const QString &path)
{
    qDebug() << "MainWindow::handleTmutilPathChange called:"
             << path;

    disconnect(this->statusTimer, &QTimer::timeout,
               this, &MainWindow::checkTmStatus);
    disconnect(this->destinationsTimer, &QTimer::timeout,
               this->ui->destinationsTable,
               &DestinationsWidget::queryDestinations);

    this->tmutilPath = path;
    this->ui->destinationsTable->setTmutilPath(path);

    connect(this->statusTimer, &QTimer::timeout,
            this, &MainWindow::checkTmStatus);
    connect(this->destinationsTimer, &QTimer::timeout,
            this->ui->destinationsTable,
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

void MainWindow::handleItemChanged(QTableWidgetItem *)
{
    auto showDests = QSet<QString>{};
    {
        const auto tbl = this->ui->destinationsTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            const auto item = tbl->item(row, 0);
            if (!item) {
                continue;
            }
            const auto checkState = item->checkState();
            if (checkState != Qt::CheckState::Unchecked) {
                showDests.insert(item->text());
            }
        }
    }
    auto showMachs = QSet<QString>{};
    {
        const auto tbl = this->ui->machinesTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            auto item = tbl->item(row, MachinesColumn::Name);
            if (!item) {
                continue;
            }
            const auto checkState = item->checkState();
            if (checkState != Qt::CheckState::Unchecked) {
                showMachs.insert(item->text());
            }
        }
    }
    auto showVols = QSet<QString>{};
    {
        const auto tbl = this->ui->volumesTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            const auto item = tbl->item(row, VolumesColumn::Name);
            if (!item) {
                continue;
            }
            const auto checkState = item->checkState();
            if (checkState != Qt::CheckState::Unchecked) {
                showVols.insert(item->text());
            }
        }
    }
    {
        const auto tbl = this->ui->machinesTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            auto hide = false;
            if (const auto item = tbl->item(row, MachinesColumn::Destinations)) {
                const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
                hide |= !showDests.intersects(set);
            }
            if (const auto item = tbl->item(row, MachinesColumn::Volumes)) {
                const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
                hide |= !showVols.intersects(set);
            }
            tbl->setRowHidden(row, hide);
        }
    }
    {
        const auto tbl = this->ui->volumesTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            auto hide = false;
            if (const auto item = tbl->item(row, VolumesColumn::Destinations)) {
                const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
                hide |= !showDests.intersects(set);
            }
            if (const auto item = tbl->item(row, VolumesColumn::Machines)) {
                const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
                hide |= !showMachs.intersects(set);
            }
            tbl->setRowHidden(row, hide);
        }
    }
    {
        const auto tbl = this->ui->backupsTable;
        const auto count = tbl->rowCount();
        for (auto row = 0; row < count; ++row) {
            auto hide = false;
            if (const auto item = tbl->item(row, BackupsColumn::Destination)) {
                hide |= !showDests.contains(item->text());
            }
            if (const auto item = tbl->item(row, BackupsColumn::Machine)) {
                hide |= !showMachs.contains(item->text());
            }
            if (const auto item = tbl->item(row, BackupsColumn::Volumes)) {
                const auto set = item->data(Qt::UserRole).value<QSet<QString>>();
                hide |= !showVols.intersects(set);
            }
            tbl->setRowHidden(row, hide);
        }
    }
}

void MainWindow::changeTmutilStatusInterval(int msecs)
{
    this->statusTimer->start(msecs);
}

void MainWindow::changeTmutilDestinationsInterval(int msecs)
{
    this->destinationsTimer->start(msecs);
}

void MainWindow::changePathInfoInterval(int msecs)
{
    this->pathInfoTimer->start(msecs);
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
    this->pathInfoTimer->start(Settings::pathInfoInterval());
}
