#include <filesystem>

#include <QFontDatabase>
#include <QMessageBox>
#include <QProcess>
#include <QXmlStreamReader>
#include <QMainWindow>

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
}

void DestinationsWidget::queryDestinations()
{
    auto process = new PlistProcess(this);
    connect(process, &PlistProcess::gotPlist,
            this, &DestinationsWidget::updateUI);
    connect(process, &PlistProcess::gotError,
            this, &DestinationsWidget::handleError);
    connect(process, &PlistProcess::finished,
            process, &PlistProcess::deleteLater);
    process->start(this->tmuPath,
                   QStringList() << tmutilDestInfoVerb << tmutilXmlOption);
}

void DestinationsWidget::handleStatus(const plist_object &plist)
{
    // display plist output from "tmutil status -X"
    qInfo() << "handleStatusPlist called!";
    const auto *dict = std::get_if<plist_dict>(&plist.value);
    if (!dict) {
        qWarning() << "handleStatusPlist: plist value not dict!";
        return;
    }

    const auto clientID = get<plist_string>(*dict, "ClientID");
    qDebug() << "clientID:" << clientID.value_or("<none>");

    // When running...
    const auto phase = get<plist_string>(*dict, "BackupPhase");
    const auto destID = get<plist_string>(*dict, "DestinationID");
    const auto destMP = get<plist_string>(*dict, "DestinationMountPoint");
    const auto fractOfProg = get<plist_real>(*dict, "FractionOfProgressBar");
    const auto prog = get<plist_dict>(*dict, "Progress");
    const auto runningTrue = get<plist_true>(*dict, "Running");

    // When not running...
    const auto percent = get<plist_real>(*dict, "Percent");
    const auto runningFalse = get<plist_false>(*dict, "Running");

    if (destMP) {
        const auto rows = this->rowCount();
        for (auto r = 0; r < rows; ++r) {
            const auto cell = this->item(r, 3);
            if (!cell || (cell->text() != destMP->c_str())) {
                continue;
            }
            auto item = this->item(r, 5);
            if (!item) {
                item = new QTableWidgetItem;
                this->setItem(r, 5, item);
            }
            item->setText(QString::fromStdString(phase.value_or("")));
        }
    }
}

void DestinationsWidget::handleError(const QString& text)
{
    const auto status =
        QString("Call to '%1 %2 %3' erred: %4.")
            .arg(this->tmuPath,
                 tmutilDestInfoVerb,
                 tmutilXmlOption,
                 text);
    emit gotError(status);
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("Got process error when trying to query system destinations.");
    msgBox.setInformativeText(status);
    msgBox.exec();
}

void DestinationsWidget::updateUI(const plist_object &plist)
{
    this->clearContents();

    const auto destinations = toPlistDictVector(
        get<plist_array>(std::get<plist_dict>(plist.value),
                         "Destinations").value());
    this->setRowCount(int(destinations.size()));
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    constexpr auto itemFlags =
        Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsUserCheckable;
    auto mountPoints = std::vector<std::string>{};
    auto row = 0;
    for (const auto& d: destinations) {
        if (const auto v = get<std::string>(d, "Name")) {
            const auto item =
                new QTableWidgetItem(QString::fromStdString(*v));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 0, item);
        }
        if (const auto v = get<std::string>(d, "ID")) {
            const auto item =
                new QTableWidgetItem(QString::fromStdString(*v));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 1, item);
        }
        if (const auto v = get<std::string>(d, "Kind")) {
            const auto item =
                new QTableWidgetItem(QString::fromStdString(*v));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 2, item);
        }
        if (const auto v = get<std::string>(d, "MountPoint")) {
            const auto item =
                new QTableWidgetItem(QString::fromStdString(*v));
            item->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            item->setFont(font);
            item->setFlags(itemFlags);
            this->setItem(row, 3, item);
            mountPoints.push_back(*v);
        }
        if (const auto v = get<int>(d, "LastDestination")) {
            const auto item =
                new QTableWidgetItem(QString::number(*v));
            item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 4, item);
        }
        ++row;
    }
    emit gotPaths(mountPoints);
}
