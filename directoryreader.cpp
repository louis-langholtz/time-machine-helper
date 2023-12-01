#include <sys/xattr.h> // for listxattr system calls

#include <chrono>
#include <filesystem>
#include <optional>
#include <sstream>

#include <QDeadlineTimer>
#include <QTreeWidgetItem>

#include "directoryreader.h"

namespace {

// From https://stackoverflow.com/a/236803/7410358
template <typename Out>
void split(const std::string &s, char delim, Out result) {
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        *result++ = item;
    }
}

// From https://stackoverflow.com/a/236803/7410358
auto split(const std::string &s, char delim)
    -> std::vector<std::string>
{
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

auto readAttributeNames(const std::filesystem::path &path,
                        std::error_code &ec)
    -> std::vector<std::string>
{
    const auto size = ::listxattr(path.c_str(), nullptr, 0, 0);
    if (size == static_cast<ssize_t>(-1)) {
        ec = std::error_code{errno, std::generic_category()};
        return {};
    }
    if (size == 0) {
        ec = std::error_code{0, std::generic_category()};
        return {};
    }
    auto nameBuffer = std::string{};
    nameBuffer.resize(size + 1u);
    const auto result = listxattr(path.c_str(), nameBuffer.data(), size, 0);
    if (result == static_cast<ssize_t>(-1)) {
        ec = std::error_code{errno, std::generic_category()};
        return {};
    }
    ec = std::error_code{0, std::generic_category()};
    return split(nameBuffer, '\0');
}

auto readAttribute(const std::filesystem::path &path,
                   const std::string& attrName,
                   std::error_code &ec)
    -> QByteArray
{
    const auto reserveSize =
        ::getxattr(path.c_str(), attrName.c_str(), nullptr, 0, 0, 0);
    if (reserveSize == static_cast<ssize_t>(-1)) {
        ec = std::error_code{errno, std::generic_category()};
        return {};
    }
    QByteArray buffer(qsizetype(reserveSize), Qt::Initialization{});
    const auto actualSize = ::getxattr(
        path.c_str(), attrName.c_str(), buffer.data(), reserveSize, 0, 0);
    if (actualSize == static_cast<ssize_t>(-1)) {
        ec = std::error_code{errno, std::generic_category()};
        return {};
    }
    return buffer;
}

auto getAttributes(const std::filesystem::path &path,
                   const std::vector<std::string>& xattrNames)
    -> std::optional<QMap<QString, QByteArray>>
{
    auto attrs = QMap<QString, QByteArray>{};
    for (const auto& attrName: xattrNames) {
        auto ec = std::error_code{};
        const auto buffer = readAttribute(path, attrName, ec);
        if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
            return {};
        }
        if (ec) {
            continue;
        }
        attrs.insert(QString::fromStdString(attrName), buffer);
    }
    return {attrs};
}

}

DirectoryReader::DirectoryReader(std::filesystem::path dir,
                                 QObject *parent):
    QThread{parent},
    directory{std::move(dir)}
{}

DirectoryReader::~DirectoryReader()
{
    if (this->isRunning()) {
        qDebug() << "DirectoryReader::~DirectoryReader called while running";
    }

    this->requestInterruption();

    using namespace std::literals::chrono_literals;
    constexpr auto initialWait = 500ms;
    if (this->wait(QDeadlineTimer(initialWait))) {
        return;
    }

    this->terminate();
    this->wait();
}

void DirectoryReader::run() {
    using std::filesystem::directory_iterator;
    using std::filesystem::directory_options;

    auto ec = std::error_code{};
    const auto options = directory_options::skip_permission_denied;
    const auto it = directory_iterator{this->directory, options, ec};
    if (ec) {
        emit ended(this->directory, ec, {});
        return;
    }

    auto filenames = QSet<QString>{};
    for (const auto& dirEntryIter: it) {
        if (this->isInterruptionRequested()) {
            return;
        }
        const auto& path = dirEntryIter.path();
        const auto filename = path.filename().string();
        if ((filename == ".") || (filename == "..")) {
            continue;
        }
        const auto status = dirEntryIter.status(ec);
        if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
            continue;
        }
        if (ec) {
            qWarning() << "can't get status"
                       << ec.message()
                       << ", path:" << path.c_str();
        }
        const auto xattrNames = readAttributeNames(path, ec);
        if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
            continue;
        }
        const auto xattrMap = getAttributes(path, xattrNames);
        if (!xattrMap) {
            continue;
        }
        filenames.insert(QString::fromStdString(filename));
        emit entry(path, status, *xattrMap);
    }
    emit ended(this->directory, std::error_code{}, filenames);
}
