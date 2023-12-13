#include <filesystem>

#include <QFontDatabase>
#include <QMessageBox>
#include <QProcess>
#include <QXmlStreamReader>
#include <QMainWindow>
#include <QFileInfo>
#include <QProgressBar>

#include "destinationswidget.h"
#include "itemdefaults.h"
#include "plist_object.h"
#include "plistprocess.h"
#include "sortingdisabler.h"

namespace {

constexpr auto destinationsKey = "Destinations";
constexpr auto tmutilDestInfoVerb = "destinationinfo";
constexpr auto tmutilXmlOption    = "-X";
constexpr auto noExplanationMsg = "no explanation";

/// @note Toplevel key within the status plist dictionary.
constexpr auto backupPhaseKey = "BackupPhase";

/// @note Toplevel key within the status plist dictionary.
constexpr auto destinationMountPointKey = "DestinationMountPoint";

/// @note Toplevel key within the status plist dictionary.
constexpr auto destinationIdKey = "DestinationID";

/// @note Toplevel key within the status plist dictionary. Its
///   entry value is another dictionary with progress related details.
constexpr auto progressKey = "Progress";

/// @note Key within the "Progress" entry's dictionary.
constexpr auto timeRemainingKey = "TimeRemaining";

/// @note Key within the "Progress" entry's dictionary.
constexpr auto percentKey = "Percent";

/// @note Key within the "Progress" entry's dictionary.
constexpr auto bytesKey = "bytes";

/// @note Key within the "Progress" entry's dictionary.
constexpr auto totalBytesKey = "totalBytes";

/// @note Key within the "Progress" entry's dictionary.
constexpr auto numFilesKey = "files";

/// @note Key within the "Progress" entry's dictionary.
constexpr auto totalFilesKey = "totalFiles";

/// @brief Flags for <code>QTableWidgetItem</code> objects.
constexpr auto itemFlags =
    Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsUserCheckable;

constexpr auto gigabyte = 1000 * 1000 * 1000;

auto decodeBackupPhase(const plist_string &name) -> QString
{
    if (name == "ThinningPostBackup") {
        // a.k.a. "Cleaning up"
        return "Thinning Post Backup";
    }
    if (name == "FindingChanges") {
        return "Finding Changes";
    }
    return QString::fromStdString(name);
}

auto textForBackupStatus(const plist_dict &status, const std::string &mp)
    -> QString
{
    // When running...
    const auto destMP = get<plist_string>(status, destinationMountPointKey);
    if (destMP && destMP == mp) {
        auto result = QStringList{};
        if (const auto v = get<plist_string>(status, backupPhaseKey)) {
            result << decodeBackupPhase(*v);
        }
        if (const auto prog = get<plist_dict>(status, progressKey)) {
            if (const auto v = get<plist_real>(*prog, percentKey)) {
                result << QString("%1%")
                              .arg(QString::number(*v * 100.0, 'f', 1));
            }
        }
        return result.join(' ');
    }
    return QString{};
}

auto secondsToUserTime(plist_real value) -> QString
{
    static constexpr auto secondsPerMinutes = 60;
    return QString("~%1 minutes")
        .arg(QString::number(value / secondsPerMinutes, 'f', 1));
}

auto toolTipForBackupStatus(const plist_dict &status, const std::string &mp)
    -> QString
{
    // When running...
    const auto destMP = get<plist_string>(status, destinationMountPointKey);
    if (destMP && destMP == mp) {
        auto result = QStringList{};
        if (const auto v = get<plist_string>(status, destinationIdKey)) {
            result << QString("Destination ID: %1.").arg(v->c_str());
        }
        if (const auto prog = get<plist_dict>(status, progressKey)) {
            if (const auto v = get<plist_integer>(*prog, bytesKey)) {
                result << QString("Number of bytes: %1.").arg(*v);
            }
            if (const auto v = get<plist_integer>(*prog, totalBytesKey)) {
                result << QString("Total bytes: %1.").arg(*v);
            }
            if (const auto v = get<plist_integer>(*prog, numFilesKey)) {
                result << QString("Number of files: %1.").arg(*v);
            }
            if (const auto v = get<plist_integer>(*prog, totalFilesKey)) {
                result << QString("Total files: %1.").arg(*v);
            }
            if (const auto v = get<plist_real>(*prog, timeRemainingKey)) {
                result << QString("Allegedly, %1 remaining.")
                              .arg(secondsToUserTime(*v));
            }
        }
        return result.join('\n');
    }
    return {};
}

class TableWidgetItem: public QTableWidgetItem {
public:
    static auto cellWidget(const QTableWidgetItem &item) -> QWidget*;
    static auto progressBar(const QTableWidgetItem &item) -> QProgressBar*;

    using QTableWidgetItem::QTableWidgetItem;

    [[nodiscard]] auto clone() const -> QTableWidgetItem* override;

    auto operator<(const QTableWidgetItem &other) const -> bool override;
};

auto TableWidgetItem::cellWidget(const QTableWidgetItem &item) -> QWidget*
{
    if (const auto w = item.tableWidget()) {
        return w->cellWidget(item.row(), item.column());
    }
    return {};
}

auto TableWidgetItem::progressBar(const QTableWidgetItem &item)
    -> QProgressBar*
{
    return dynamic_cast<QProgressBar*>(cellWidget(item));
}

auto TableWidgetItem::clone() const -> QTableWidgetItem*
{
    return new TableWidgetItem{*this};
}

auto TableWidgetItem::operator<(const QTableWidgetItem &other) const -> bool
{
    if (this->text().isEmpty() && other.text().isEmpty()) {
        const auto thisProgress = progressBar(*this);
        const auto otherProgress = progressBar(other);
        if (thisProgress && otherProgress) {
            return thisProgress->value() < otherProgress->value();
        }
    }
    return QTableWidgetItem::operator<(other);
}

}

DestinationsWidget::DestinationsWidget(QWidget *parent)
    : QTableWidget{parent}
{
}

auto DestinationsWidget::tmutilPath() const -> QString
{
    return this->tmuPath;
}

void DestinationsWidget::setTmutilPath(const QString& path)
{
    this->tmuPath = path;
}

void DestinationsWidget::queryDestinations()
{
    auto process = new PlistProcess(this);
    connect(process, &PlistProcess::gotPlist,
            this, &DestinationsWidget::updateUI);
    connect(process, &PlistProcess::errorOccurred,
            this, &DestinationsWidget::handleErrorOccurred);
    connect(process, &PlistProcess::gotReaderError,
            this, &DestinationsWidget::handleReaderError);
    connect(process, &PlistProcess::finished,
            this, &DestinationsWidget::handleQueryFinished);
    connect(process, &PlistProcess::finished,
            process, &PlistProcess::deleteLater);
    process->start(this->tmuPath,
                   QStringList() << tmutilDestInfoVerb
                                 << tmutilXmlOption);
}

void DestinationsWidget::handleStatus(const plist_object &plist)
{
    // display plist output from "tmutil status -X"
    const auto *dict = std::get_if<plist_dict>(&plist.value);
    if (!dict) {
        qWarning() << "handleStatusPlist: plist value not dict!";
        return;
    }
    this->lastStatus = *dict;
    const auto rows = this->rowCount();
    for (auto row = 0; row < rows; ++row) {
        const auto mpItem = this->item(row, 3);
        if (!mpItem) {
            continue;
        }
        const auto stItem = this->item(row, 7);
        if (!stItem) {
            continue;
        }
        const auto mountPoint = mpItem->text().toStdString();
        stItem->setText(textForBackupStatus(*dict, mountPoint));
        stItem->setToolTip(toolTipForBackupStatus(*dict, mountPoint));
    }
}

void DestinationsWidget::handleReaderError(
    qint64 lineNumber, int error, const QString& text)
{
    const auto status =
        QString("'%1 %2 %3' erred reading line %4, code %5: %6.")
            .arg(this->tmuPath,
                 tmutilDestInfoVerb,
                 tmutilXmlOption,
                 QString::number(lineNumber),
                 QString::number(error),
                 text);
    emit gotError(status);
}

void DestinationsWidget::handleErrorOccurred(int error, const QString &text)
{
    qDebug() << "handleErrorOccurred:"
             << error << text;
    switch (QProcess::ProcessError(error)) {
    case QProcess::FailedToStart:{
        emit failedToStartQuery(text);
        break;
    }
    case QProcess::Crashed:
    case QProcess::Timedout:
    case QProcess::ReadError:
    case QProcess::WriteError:
    case QProcess::UnknownError:
        break;
    }
}

void DestinationsWidget::handleQueryFinished(int code, int status)
{
    if (status == QProcess::ExitStatus::CrashExit) {
        const auto text =
            QString("'%1 %2 %3' exited abnormally.")
                .arg(this->tmuPath,
                     tmutilDestInfoVerb,
                     tmutilXmlOption);
        emit gotError(text);
    }
    else if (code != 0) {
        const auto text =
            QString("'%1 %2 %3' exit code was %4.")
                .arg(this->tmuPath,
                     tmutilDestInfoVerb,
                     tmutilXmlOption,
                     QString::number(code));
        emit gotError(text);
    }
}

void DestinationsWidget::update(
    const std::vector<plist_dict>& destinations)
{
    constexpr auto alignRight = Qt::AlignRight|Qt::AlignVCenter;
    const auto fixedFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto smallFont =
        QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
    auto mountPoints = std::map<std::string, plist_dict>{};
    auto row = 0;
    const auto rowCount = int(destinations.size());
    const SortingDisabler disableSort{this};
    this->setRowCount(rowCount);
    emit gotDestinations(rowCount);
    if (rowCount == 0) {
        return;
    }
    for (const auto& d: destinations) {
        const auto mp = get<std::string>(d, "MountPoint");
        const auto id = get<std::string>(d, "ID");
        auto ec = std::error_code{};
        const auto si = mp
                            ? std::filesystem::space(*mp, ec)
                            : std::filesystem::space_info{};
        const auto flags =
            Qt::ItemFlags{mp? Qt::ItemIsEnabled: Qt::NoItemFlags};
        auto checked = false;
        {
            const auto on = std::optional<bool>{mp && !ec};
            const auto item = createdItem(this, row, 0,
                                          ItemDefaults{}.use(on));
            item->setFlags(flags|Qt::ItemIsUserCheckable);
            item->setText(QString::fromStdString(
                get<std::string>(d, "Name").value_or("")));
            item->setToolTip("Backup disk a.k.a. backup destination.");
            checked = item->checkState() == Qt::CheckState::Checked;
        }
        {
            const auto item = createdItem(this, row, 1,
                                          ItemDefaults{}.use(fixedFont));
            item->setFlags(flags);
            item->setText(QString::fromStdString(id.value_or("")));
        }
        {
            const auto item = createdItem(this, row, 2);
            item->setFlags(flags);
            item->setText(QString::fromStdString(
                get<std::string>(d, "Kind").value_or("")));
        }
        {
            constexpr auto align = Qt::AlignLeft|Qt::AlignVCenter;
            const auto item = createdItem(this, row, 3,
                                          ItemDefaults{}.use(align).use(fixedFont));
            item->setFlags(flags);
            item->setText(QString::fromStdString(mp.value_or("")));
        }
        {
            const auto used = si.capacity - si.free;
            const auto percentUsage = (si.capacity != 0u)
                ? static_cast<int>((double(used) / double(si.capacity)) * 100.0)
                : 0;
            auto widget = new QProgressBar{this};
            widget->setOrientation(Qt::Horizontal);
            constexpr auto percentMin = 0;
            constexpr auto percentMax = 100;
            widget->setRange(percentMin, percentMax);
            widget->setValue(percentUsage);
            widget->setTextVisible(true);
            widget->setAlignment(Qt::AlignTop);
            widget->setToolTip(QString("Used %1% (%2b of %3b with %4b remaining).")
                                   .arg(percentUsage)
                                   .arg(used)
                                   .arg(si.capacity)
                                   .arg(si.free));
            this->setCellWidget(row, 4, widget);

            const auto align = Qt::AlignRight|Qt::AlignBottom;
            const auto text = (mp && !ec)
                                  ? QString("%1%").arg(percentUsage)
                                  : QString{};
            const auto item = createdItem(this, row, 4,
                                          ItemDefaults{}.use(align)
                                                        .use(smallFont));
            item->setFlags(flags);
            item->setText(text);
        }
        {
            const auto item = createdItem(this, row, 5,
                                          ItemDefaults{}.use(alignRight)
                                                        .use(fixedFont));
            item->setFlags(flags);
            if (mp && !ec) {
                item->setData(Qt::EditRole, double(si.capacity) / gigabyte);
            }
            else {
                item->setText(QString{});
            }
        }
        {
            const auto item = createdItem(this,
                                          row,
                                          6,
                                          ItemDefaults{}
                                              .use(alignRight)
                                              .use(fixedFont));
            item->setFlags(flags);
            if (mp && !ec) {
                item->setData(Qt::EditRole, double(si.free) / gigabyte);
            }
            else {
                item->setText(QString{});
            }
        }
        {
            const auto status = this->lastStatus;
            const auto mountPoint = mp.value_or("");
            const auto item = createdItem(this, row, 7,
                                          ItemDefaults{}.use(fixedFont));
            item->setFlags(flags);
            item->setText(textForBackupStatus(status, mountPoint));
            item->setToolTip(toolTipForBackupStatus(status, mountPoint));
        }
        if (mp && checked) {
            mountPoints.emplace(*mp, d);
        }
        ++row;
    }
    emit gotPaths(mountPoints);
}

void DestinationsWidget::update(const plist_array &plist)
{
    auto destinations = std::vector<plist_dict>{};
    for (const auto& element: plist) {
        const auto p = std::get_if<plist_dict>(&element.value);
        if (!p) {
            emit gotError(QString(
                "Unexpected type of element %1 in '%2' key entry array!")
                              .arg(&element - plist.data())
                              .arg(destinationsKey));
            continue;
        }
        destinations.push_back(*p);
    }
    update(destinations);
}

void DestinationsWidget::update(const plist_dict &plist)
{
    const auto it = plist.find(destinationsKey);
    if (it == plist.end()) {
        emit wrongQueryInfo(QString("'%1' key entry not found!")
                                .arg(destinationsKey));
    }
    const auto p = std::get_if<plist_array>(&(it->second.value));
    if (!p) {
        emit wrongQueryInfo(
            QString("'%1' key entry not array - entry index is %2!")
                .arg(destinationsKey)
                .arg(it->second.value.index()));
    }
    update(*p);
}

auto DestinationsWidget::findRowWithMountPoint(const QString &key) const
    -> int
{
    const auto rows = this->rowCount();
    for (auto r = 0; r < rows; ++r) {
        const auto cell = this->item(r, 3);
        if (cell && (cell->text() == key)) {
            return r;
        }
    }
    return -1;
}

void DestinationsWidget::updateUI(const plist_object &plist)
{
    if (const auto p = std::get_if<plist_dict>(&plist.value)) {
        return this->update(*p);
    }
    emit wrongQueryInfo(QString(
        "Got wrong plist value type: expected index of %1, got %2!")
                            .arg(plist_variant(plist_dict{}).index())
                            .arg(plist.value.index()));
}
