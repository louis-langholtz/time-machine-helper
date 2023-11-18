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

static constexpr auto tmutilDestInfoVerb = "destinationinfo";
static constexpr auto tmutilXmlOption    = "-X";
constexpr auto noExplanationMsg = "no explanation";

auto toPlistDictVector(const plist_array& array)
    -> std::vector<plist_dict>
{
    auto result = std::vector<plist_dict>{};
    for (const auto& element: array) {
        result.push_back(std::get<plist_dict>(element.value));
    }
    return result;
}

}

DestinationsWidget::DestinationsWidget(QWidget *parent)
    : QTableWidget{parent}
{
    if (const auto item = this->horizontalHeaderItem(3)) {
        this->saveBg = item->background();
        item->setBackground(QBrush(QColor(255, 0, 0, 255)));
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
    int row, int column)
{
    auto i = this->item(row, column);
    if (!i) {
        i = new QTableWidgetItem;
        this->setItem(row, column, i);
    }
    return i;
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
                   QStringList() << tmutilDestInfoVerb << tmutilXmlOption);
}

void DestinationsWidget::handleStatus(const plist_object &plist)
{
    // display plist output from "tmutil status -X"
    const auto *dict = std::get_if<plist_dict>(&plist.value);
    if (!dict) {
        qWarning() << "handleStatusPlist: plist value not dict!";
        return;
    }

    const auto clientID = get<plist_string>(*dict, "ClientID");

    // When running...
    const auto phase = get<plist_string>(*dict, "BackupPhase");
    const auto destID = get<plist_string>(*dict, "DestinationID");
    const auto destMP = get<plist_string>(*dict, "DestinationMountPoint");
    const auto fractOfProg = get<plist_real>(*dict, "FractionOfProgressBar");
    const auto prog = get<plist_dict>(*dict, "Progress");

    if (destMP) {
        const auto rows = this->rowCount();
        for (auto r = 0; r < rows; ++r) {
            const auto cell = this->item(r, 3);
            if (!cell || (cell->text() != destMP->c_str())) {
                continue;
            }
            auto item = this->item(r, 6);
            auto text = QString{};
            auto tooltip = QString{};
            text.append(QString::fromStdString(phase.value_or("")));
            if (prog) {
                if (const auto v = get<plist_real>(*prog, "Percent")) {
                    if (!text.isEmpty()) {
                        text.append(' ');
                    }
                    text.append(QString::number(*v * 100.0, 'f', 1));
                    text.append('%');
                }
                if (const auto v = get<plist_real>(*prog, "TimeRemaining")) {
                    tooltip = QString("About %1 minutes remaining.")
                                  .arg(QString::number(*v / 60, 'f', 1));
                }
            }
            item->setText(text);
            item->setToolTip(tooltip);
        }
    }
}

void DestinationsWidget::handleReaderError(
    int lineNumber, const QString& text)
{
    const auto status =
        QString("'%1 %2 %3' erred reading line %4: %5.")
            .arg(this->tmuPath,
                 tmutilDestInfoVerb,
                 tmutilXmlOption,
                 QString::number(lineNumber),
                 text);
    emit gotError(status);
}

void DestinationsWidget::handleErrorOccurred(int error, const QString &text)
{
    qDebug() << "handleErrorOccurred:"
             << error << text;
    switch (QProcess::ProcessError(error)) {
    case QProcess::FailedToStart:{
        emit queryFailedToStart(text);
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

void DestinationsWidget::handleQueryFinished(int exitCode, int exitStatus)
{
    if (exitStatus == QProcess::ExitStatus::CrashExit) {
        const auto status =
            QString("'%1 %2 %3' exited abnormally.")
                .arg(this->tmuPath,
                     tmutilDestInfoVerb,
                     tmutilXmlOption);
        emit gotError(status);
    }
    else if (exitCode != 0) {
        const auto status =
            QString("'%1 %2 %3' exit code was %4.")
                .arg(this->tmuPath,
                     tmutilDestInfoVerb,
                     tmutilXmlOption,
                     QString::number(exitCode));
        emit gotError(status);
    }
}

void DestinationsWidget::updateUI(const plist_object &plist)
{
    const auto destinations = toPlistDictVector(
        get<plist_array>(std::get<plist_dict>(plist.value),
                         "Destinations").value());
    this->setRowCount(int(destinations.size()));
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    constexpr auto itemFlags =
        Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsUserCheckable;
    auto mountPoints = std::vector<std::string>{};
    auto row = 0;
    if (destinations.empty()) {
        if (const auto item = this->horizontalHeaderItem(3)) {
            item->setBackground(Qt::GlobalColor::red);
        }
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("No destinations setup?!");
        auto info = QString{};
        info.append("No backups or restores are currently possible! ");
        info.append("Add a destination to Time Machine as soon as you can.");
        msgBox.setInformativeText(info);
        msgBox.exec();
        return;
    }
    for (const auto& d: destinations) {
        {
            const auto item = this->createdItem(row, 0);
            const auto v = get<std::string>(d, "Name");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
        }
        {
            const auto item = this->createdItem(row, 1);
            const auto v = get<std::string>(d, "ID");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
        }
        {
            const auto item = this->createdItem(row, 2);
            const auto v = get<std::string>(d, "Kind");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
        }
        const auto mp = get<std::string>(d, "MountPoint");
        {
            if (const auto item = this->horizontalHeaderItem(3)) {
                item->setBackground(this->saveBg);
            }
            const auto item = this->createdItem(row, 3);
            item->setText(QString::fromStdString(mp.value_or("")));
            item->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            item->setFont(font);
            item->setFlags(itemFlags);
        }
        auto ec = std::error_code{};
        const auto si = mp? std::filesystem::space(*mp, ec): std::filesystem::space_info{};
        {
            const auto item = this->createdItem(row, 4);
            if (mp && !ec) {
                const auto capacityInGb = double(si.capacity) / (1000 * 1000 * 1000);
                item->setText(QString::number(capacityInGb, 'f', 2));
            }
            item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            item->setFlags(itemFlags);
        }
        {
            const auto item = this->createdItem(row, 5);
            if (mp && !ec) {
                const auto freeInGb = double(si.free) / (1000 * 1000 * 1000);
                item->setText(QString::number(freeInGb, 'f', 2));
            }
            item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            item->setFlags(itemFlags);
        }
        {
            const auto item = this->createdItem(row, 6);
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
        }
        if (mp) {
            mountPoints.push_back(*mp);
        }
        ++row;
    }
    emit gotPaths(mountPoints);
}
