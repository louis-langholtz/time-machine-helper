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
        {
            const auto item = new QTableWidgetItem;
            const auto v = get<std::string>(d, "Name");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 0, item);
        }
        {
            const auto item = new QTableWidgetItem;
            const auto v = get<std::string>(d, "ID");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 1, item);
        }
        {
            const auto item = new QTableWidgetItem;
            const auto v = get<std::string>(d, "Kind");
            item->setText(QString::fromStdString(v.value_or("")));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 2, item);
        }
        const auto mp = get<std::string>(d, "MountPoint");
        {
            const auto item = new QTableWidgetItem;
            item->setText(QString::fromStdString(mp.value_or("")));
            item->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            item->setFont(font);
            item->setFlags(itemFlags);
            this->setItem(row, 3, item);
        }
        auto ec = std::error_code{};
        const auto si = mp? std::filesystem::space(*mp, ec): std::filesystem::space_info{};
        {
            const auto item = new QTableWidgetItem;
            if (mp && !ec) {
                const auto capacityInGb = double(si.capacity) / (1000 * 1000 * 1000);
                item->setText(QString::number(capacityInGb, 'f', 2));
            }
            item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 4, item);
        }
        {
            const auto item = new QTableWidgetItem;
            if (mp && !ec) {
                const auto freeInGb = double(si.free) / (1000 * 1000 * 1000);
                item->setText(QString::number(freeInGb, 'f', 2));
            }
            item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 5, item);
        }
        {
            const auto item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(itemFlags);
            this->setItem(row, 6, item);
        }
        if (mp) {
            mountPoints.push_back(*mp);
        }
        ++row;
    }
    emit gotPaths(mountPoints);
}
