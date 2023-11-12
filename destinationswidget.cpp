#include <filesystem>

#include <QFontDatabase>
#include <QMessageBox>
#include <QProcess>
#include <QXmlStreamReader>
#include <QMainWindow>

#include "destinationswidget.h"
#include "plist_builder.h"
#include "plist_object.h"

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

plist_element_type toPlistElementType(const QStringView& string)
{
    if (string.compare("array") == 0) {
        return plist_element_type::array;
    }
    if (string.compare("dict") == 0) {
        return plist_element_type::dict;
    }
    if (string.compare("real") == 0) {
        return plist_element_type::real;
    }
    if (string.compare("integer") == 0) {
        return plist_element_type::integer;
    }
    if (string.compare("string") == 0) {
        return plist_element_type::string;
    }
    if (string.compare("key") == 0) {
        return plist_element_type::key;
    }
    if (string.compare("plist") == 0) {
        return plist_element_type::plist;
    }
    return plist_element_type::none;
}

}

DestinationsWidget::DestinationsWidget(QWidget *parent)
    : QTableWidget{parent}
{
}

QString DestinationsWidget::errorString(
    const QString& fallback) const
{
    return this->process
        ? this->process->errorString()
        : fallback;
}

void DestinationsWidget::readMore()
{
    qDebug() << "readMore called";
    while (!reader->atEnd()) {
        const auto tokenType = reader->readNext();
        switch (tokenType) {
        case QXmlStreamReader::NoToken:
            break;
        case QXmlStreamReader::Invalid:
            qWarning() << "invalid token type!";
            break;
        case QXmlStreamReader::StartDocument:
            qInfo() << "start document";
            break;
        case QXmlStreamReader::EndDocument:
            qInfo() << "end document";
            break;
        case QXmlStreamReader::StartElement:
        {
            qInfo() << "start element name:" << reader->name();
            const auto elementType = toPlistElementType(reader->name());
            switch (elementType) {
            case plist_element_type::none:
                break;
            case plist_element_type::array:
                this->awaiting_handle.set_value(plist_array{});
                break;
            case plist_element_type::dict:
                this->awaiting_handle.set_value(plist_dict{});
                break;
            case plist_element_type::real:
            case plist_element_type::integer:
            case plist_element_type::string:
            case plist_element_type::key:
                break;
            case plist_element_type::plist:
                this->task = plist_builder(&awaiting_handle);
                break;
            }
            break;
        }
        case QXmlStreamReader::EndElement:
        {
            qInfo() << "end element name:" << reader->name();
            const auto elementType = toPlistElementType(reader->name());
            switch (elementType) {
            case plist_element_type::none:
                break;
            case plist_element_type::array:
            case plist_element_type::dict:
                this->awaiting_handle.set_value(plist_variant{});
                break;
            case plist_element_type::real:
                this->awaiting_handle.set_value(currentText.toDouble());
                break;
            case plist_element_type::integer:
                this->awaiting_handle.set_value(currentText.toInt());
                break;
            case plist_element_type::string:
            case plist_element_type::key:
                this->awaiting_handle.set_value(currentText.toStdString());
                break;
            case plist_element_type::plist:
            {
                const auto plistObject = this->task();
                qInfo() << "result.value=" << plistObject.value.index();
                emit gotDestinationsPlist(plistObject);
                break;
            }
            }
            break;
        }
        case QXmlStreamReader::Characters:
            currentText = reader->text().toString();
            break;
        case QXmlStreamReader::Comment:
            break;
        case QXmlStreamReader::DTD:
            break;
        case QXmlStreamReader::EntityReference:
            qWarning() << "unresolved name:" << reader->name();
            break;
        case QXmlStreamReader::ProcessingInstruction:
            qWarning() << "unexpected processing instruction:"
                       << reader->text();
            break;
        }
    }
    if (reader->error() == QXmlStreamReader::PrematureEndOfDocumentError) {
        return;
    }
    if (reader->hasError()) {
        qWarning() << "xml reader had error:"
                   << reader->errorString();
        return;
    }
    qInfo() << "done reading";
}

void DestinationsWidget::queryDestinations()
{
    if (this->process && this->process->state() !=
                             QProcess::ProcessState::NotRunning) {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("Cannot read destination info.");
        msgBox.setInformativeText("Must wait till prior read has ended.");
        msgBox.exec();
        return;
    }
    delete this->process;
    this->process = new QProcess(this);
    this->reader = new QXmlStreamReader(this->process);
    connect(this, &DestinationsWidget::gotDestinationsPlist,
            this, &DestinationsWidget::updateUI);
    connect(this->process, &QIODevice::readyRead,
            this, &DestinationsWidget::readMore);
    connect(this->process, &QProcess::finished,
            this, &DestinationsWidget::processFinished);
    connect(this->process, &QProcess::errorOccurred,
            this, &DestinationsWidget::processErrorOccurred);
    this->process->start(this->tmuPath,
                         QStringList() << tmutilDestInfoVerb << tmutilXmlOption,
                         QIODeviceBase::ReadOnly);
}

void DestinationsWidget::processFinished(int exitCode, int exitStatus)
{
    auto title = QString{};
    auto text = QString{};
    if (exitStatus == QProcess::ExitStatus::CrashExit) {
        title = "Error!";
        text = QString("'%1' command line tool crashed!")
                   .arg(this->tmuPath);
    }
    else if (exitCode != 0) {
        title = "Error!";
        text = QString("Unexpected exit status for '%1' of %2!")
                   .arg(this->tmuPath, QString::number(exitCode));
    }
    else {
        text = QString("'%1 %2 %3' finished normally")
                   .arg(this->tmuPath, tmutilDestInfoVerb, tmutilXmlOption);
    }
    emit gotStatus(text);
    if (!title.isEmpty()) {
        QMessageBox::warning(this, title, text);
    }
}

void DestinationsWidget::processErrorOccurred(int error)
{
    const auto status =
        QString("Call to '%1 %2 %3' erred: %4.")
                            .arg(this->tmuPath,
                                 tmutilDestInfoVerb,
                                 tmutilXmlOption,
                                 errorString(noExplanationMsg));
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
