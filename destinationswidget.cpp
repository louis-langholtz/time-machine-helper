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

QString textForBackupStatus(
    const plist_dict& status,
    const std::string& mp)
{
    // When running...
    const auto phase = get<plist_string>(status, "BackupPhase");
    const auto destID = get<plist_string>(status, "DestinationID");
    const auto destMP = get<plist_string>(status, "DestinationMountPoint");
    const auto prog = get<plist_dict>(status, "Progress");

    auto result = QString{};
    if (destMP && destMP == mp) {
        result.append(QString::fromStdString(phase.value_or("")));
        if (prog) {
            if (const auto v = get<plist_real>(*prog, "Percent")) {
                if (!result.isEmpty()) {
                    result.append(' ');
                }
                result.append(QString::number(*v * 100.0, 'f', 1));
                result.append('%');
            }
        }
    }
    return result;
}

QString toolTipForBackupStatus(
    const plist_dict& status,
    const std::string& mp)
{
    // When running...
    const auto destMP = get<plist_string>(status, "DestinationMountPoint");
    if (destMP && destMP == mp) {
        const auto prog = get<plist_dict>(status, "Progress");
        if (prog) {
            if (const auto v = get<plist_real>(*prog, "TimeRemaining")) {
                return QString("About %1 minutes remaining.")
                    .arg(QString::number(*v / 60, 'f', 1));
            }
        }
    }
    return {};
}

}

DestinationsWidget::DestinationsWidget(QWidget *parent)
    : QTableWidget{parent}
{
    if (const auto item = this->horizontalHeaderItem(3)) {
        this->saveBg = item->background();
        item->setBackground(QBrush(QColor(255, 0, 0, 100)));
    }
}

QString DestinationsWidget::tmutilPath() const
{
    return this->tmuPath;
}

void DestinationsWidget::setTmutilPath(const QString& path)
{
    this->tmuPath = path;
}

QTableWidgetItem *DestinationsWidget::createdItem(
    int row, int column, Qt::Alignment textAlign)
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
    int lineNumber, int error, const QString& text)
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
            if (const auto item = this->horizontalHeaderItem(3)) {
                item->setBackground(this->saveBg);
            }
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

int DestinationsWidget::findRowWithMountPoint(const QString &key) const
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
                            .arg(plist_variant(plist_dict{}).index(),
                                 plist.value.index()));
}
