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

void DestinationsWidget::handleError(const QString& text)
{
    const auto status =
        QString("Call to '%1 %2 %3' erred: %4.")
            .arg(this->tmuPath,
                 tmutilDestInfoVerb,
                 tmutilXmlOption,
                 text);
    emit gotStatus(status);
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
