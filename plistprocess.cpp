#include <QDateTime>
#include <QtDebug>
#include <QProcess>
#include <QXmlStreamReader>

#include "plistprocess.h"
#include "plist_builder.h"

namespace {

auto toPlistElementType(const QStringView& string)
    -> plist_element_type
{
    if (string.compare("array") == 0) {
        return plist_element_type::array;
    }
    if (string.compare("data") == 0) {
        return plist_element_type::data;
    }
    if (string.compare("date") == 0) {
        return plist_element_type::date;
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
    if (string.compare("true") == 0) {
        return plist_element_type::so_true;
    }
    if (string.compare("false") == 0) {
        return plist_element_type::so_false;
    }
    if (string.compare("key") == 0) {
        return plist_element_type::key;
    }
    if (string.compare("plist") == 0) {
        return plist_element_type::plist;
    }
    return plist_element_type::none;
}

auto toPlistData(const QString& string)
    -> plist_data
{
    const auto ba = QByteArray::fromBase64(string.toUtf8());
    return plist_data(ba.begin(), ba.end());
}

auto toPlistDate(const QString& string)
    -> plist_date
{
    // Ex: "2023-11-15T15:54:30Z"
    // From https://www.apple.com/DTDs/PropertyList-1.0.dtd:
    // Contents should conform to a subset of ISO 8601 (in
    // particular, YYYY '-' MM '-' DD 'T' HH ':' MM ':' SS 'Z'.
    // Smaller units may be omitted with a loss of precision)
    const auto d = QDateTime::fromString(string, Qt::ISODate);
    const auto t = std::chrono::system_clock::from_time_t(d.toSecsSinceEpoch());
    return std::chrono::time_point<std::chrono::system_clock>{t};
}

}

PlistProcess::PlistProcess(QObject *parent):
    QObject{parent},
    process{new QProcess{this}},
    reader{new QXmlStreamReader{this->process}}
{
    connect(this->process, &QProcess::started,
            this, &PlistProcess::handleStarted);
    connect(this->process, &QProcess::readyReadStandardOutput,
            this, &PlistProcess::readMore);
    connect(this->process, &QProcess::errorOccurred,
            this, &PlistProcess::handleErrorOccurred);
    connect(this->process, &QProcess::finished,
            this, &PlistProcess::handleFinished);
}

bool PlistProcess::done() const noexcept
{
    return this->process == nullptr;
}

void PlistProcess::start(const QString& program,
                         const QStringList& args)
{
    if (this->process) {
        this->process->start(program, args, QProcess::ReadOnly);
    }
}

void PlistProcess::handleStarted()
{
    emit started();
}

void PlistProcess::handleErrorOccurred(int error)
{
    emit gotError(this->process->errorString());
}

void PlistProcess::handleFinished(int exitCode, int exitStatus)
{
    if (exitStatus == QProcess::ExitStatus::CrashExit) {
        emit gotError("Program exited abnormally.");
    }
    else if (exitCode != 0) {
        emit gotError(QString("Unexpected exit status of %1.")
                          .arg(QString::number(exitCode)));
    }
    delete this->process;
    this->process = nullptr;
    emit finished();
}

void PlistProcess::readMore()
{
    while (!reader->atEnd()) {
        const auto tokenType = reader->readNext();
        switch (tokenType) {
        case QXmlStreamReader::NoToken:
            break;
        case QXmlStreamReader::Invalid:
            break;
        case QXmlStreamReader::StartDocument:
            break;
        case QXmlStreamReader::EndDocument:
            break;
        case QXmlStreamReader::StartElement:
        {
            const auto elementType =
                toPlistElementType(reader->name());
            switch (elementType) {
            case plist_element_type::none:
                break;
            case plist_element_type::array:
                this->awaiting_handle.set_value(plist_array{});
                break;
            case plist_element_type::dict:
                this->awaiting_handle.set_value(plist_dict{});
                break;
            case plist_element_type::data:
            case plist_element_type::date:
            case plist_element_type::so_true:
            case plist_element_type::so_false:
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
            const auto elementType =
                toPlistElementType(reader->name());
            switch (elementType) {
            case plist_element_type::none:
                break;
            case plist_element_type::array:
            case plist_element_type::dict:
                this->awaiting_handle.set_value(plist_variant{});
                break;
            case plist_element_type::data:
                this->awaiting_handle.set_value(toPlistData(currentText));
                break;
            case plist_element_type::date:
                this->awaiting_handle.set_value(toPlistDate(currentText));
                break;
            case plist_element_type::so_true:
                this->awaiting_handle.set_value(plist_true{});
                break;
            case plist_element_type::so_false:
                this->awaiting_handle.set_value(plist_false{});
                break;
            case plist_element_type::real:
                this->awaiting_handle
                    .set_value(currentText.toDouble());
                break;
            case plist_element_type::integer:
                this->awaiting_handle
                    .set_value(currentText.toInt());
                break;
            case plist_element_type::string:
            case plist_element_type::key:
                this->awaiting_handle
                    .set_value(currentText.toStdString());
                break;
            case plist_element_type::plist:
            {
                const auto plistObject = this->task();
                emit gotPlist(plistObject);
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
            emit gotInfo(QString("unresolved name: %1")
                             .arg(reader->name()));
            break;
        case QXmlStreamReader::ProcessingInstruction:
            qWarning() << "unexpected processing instruction:"
                       << reader->text();
            emit gotInfo(QString("found processing instruction: %1")
                             .arg(reader->text()));
            break;
        }
    }
    using QXmlStreamReader::PrematureEndOfDocumentError;
    if (reader->error() == PrematureEndOfDocumentError) {
        return;
    }
    if (reader->hasError()) {
        qWarning() << "xml reader had error:"
                   << reader->errorString();
        emit gotError(QString("xml reader had error: %1")
                          .arg(this->reader->errorString()));
        return;
    }
}
