#include <filesystem>

#include <QFontDatabase>
#include <QMessageBox>
#include <QProcess>
#include <QXmlStreamReader>
#include <QMainWindow>
#include <QFileInfo>

#include "destinationswidget.h"
#include "plist_object.h"
#include "plistprocess.h"

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

auto toPlistDictVector(const plist_array& array)
    -> std::vector<plist_dict>
{
    auto result = std::vector<plist_dict>{};
    for (const auto& element: array) {
        result.push_back(std::get<plist_dict>(element.value));
    }
    return result;
}

auto decodeBackupPhase(const plist_string &name) -> QString
{
    if (name == "ThinningPostBackup") {
        return "Thinning Post Backup";
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

auto DestinationsWidget::createdItem(int row, int column,
                                     Qt::Alignment textAlign)
    -> QTableWidgetItem *
{
    auto item = this->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        item->setFlags(itemFlags);
        item->setTextAlignment(textAlign);
        this->setItem(row, column, item);
    }
    return item;
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
        const auto stItem = this->item(row, 6);
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
    const auto font =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto mountPoints = std::vector<std::string>{};
    auto row = 0;
    const auto rowCount = int(destinations.size());
    this->setRowCount(rowCount);
    emit gotDestinations(rowCount);
    if (rowCount == 0) {
        return;
    }
    this->setSortingEnabled(false);
    for (const auto& d: destinations) {
        {
            const auto item = this->createdItem(row, 0);
            const auto v = get<std::string>(d, "Name");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setToolTip("Backup disk a.k.a. backup destination.");
        }
        {
            const auto item = this->createdItem(row, 1);
            const auto v = get<std::string>(d, "ID");
            item->setText(QString::fromStdString(v.value_or("")));
        }
        {
            const auto item = this->createdItem(row, 2);
            const auto v = get<std::string>(d, "Kind");
            item->setText(QString::fromStdString(v.value_or("")));
        }
        const auto mp = get<std::string>(d, "MountPoint");
        {
            const auto textAlign = Qt::AlignLeft|Qt::AlignVCenter;
            const auto item = this->createdItem(row, 3, textAlign);
            item->setText(QString::fromStdString(mp.value_or("")));
            item->setFont(font);
        }
        auto ec = std::error_code{};
        const auto si = mp
                            ? std::filesystem::space(*mp, ec)
                            : std::filesystem::space_info{};
        {
            const auto textAlign = Qt::AlignRight|Qt::AlignVCenter;
            const auto item = this->createdItem(row, 4, textAlign);
            const auto text = (mp && !ec)
                ? QString::number(double(si.capacity) / gigabyte, 'f', 2)
                : QString{};
            item->setText(text);
        }
        {
            const auto textAlign = Qt::AlignRight|Qt::AlignVCenter;
            const auto item = this->createdItem(row, 5, textAlign);
            const auto text = (mp && !ec)
                ? QString::number(double(si.free) / gigabyte, 'f', 2)
                : QString{};
            item->setText(text);
        }
        {
            const auto status = this->lastStatus;
            const auto mountPoint = mp.value_or("");
            const auto item = this->createdItem(row, 6);
            item->setText(textForBackupStatus(status, mountPoint));
            item->setToolTip(toolTipForBackupStatus(status, mountPoint));
        }
        if (mp) {
            mountPoints.push_back(*mp);
        }
        ++row;
    }
    this->setSortingEnabled(true);
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
