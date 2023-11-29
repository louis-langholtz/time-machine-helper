#include <sys/xattr.h> // for listxattr system calls

#include <filesystem>
#include <sstream>

#include <QTreeWidgetItem>

#include "directoryreader.h"

namespace {

constexpr auto timeMachineAttrPrefix = "com.apple.timemachine.";
constexpr auto backupAttrPrefix = "com.apple.backup.";
constexpr auto backupdAttrPrefix    = "com.apple.backupd.";

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

auto getInterestingAttrs(const std::filesystem::path &path,
                         const std::vector<std::string>& xattrNames,
                         bool &hasTimeMachineAttrs)
    -> QMap<QString, QByteArray>
{
    auto attrs = QMap<QString, QByteArray>{};
    for (const auto& attrName: xattrNames) {
        if (attrName.starts_with(timeMachineAttrPrefix)) {
            hasTimeMachineAttrs = true;
        }
        else if (!attrName.starts_with(backupAttrPrefix) &&
                 !attrName.starts_with(backupdAttrPrefix)) {
            continue;
        }
        auto ec = std::error_code{};
        const auto buffer = readAttribute(path, attrName, ec);
        if (ec) {
            continue;
        }
        attrs.insert(QString::fromStdString(attrName), buffer);
    }
    return attrs;
}

}

DirectoryReader::DirectoryReader(
    QTreeWidgetItem *i, QObject *parent):
    QThread{parent},
    item{i}
{}

void DirectoryReader::run() {
    using std::filesystem::directory_iterator;
    using std::filesystem::directory_options;

    const QString pathName =
        item->data(0, Qt::ItemDataRole::UserRole).toString();
    const auto dirPath = std::filesystem::path(pathName.toStdString());
    auto ec = std::error_code{};
    const auto options = directory_options::skip_permission_denied;
    const auto it = directory_iterator{dirPath, options, ec};
    if (ec) {
        emit ended(this->item, ec);
        return;
    }

    for (const auto& dirEntryIter: it) {
        const auto& path = dirEntryIter.path();
        const auto filename = path.filename().string();
        if ((filename == ".") || (filename == "..")) {
            continue;
        }
        const auto xattrNames = readAttributeNames(path, ec);
        if (ec) {
            continue;
        }
        auto hasTmAttrs = false;
        const auto subdirAttrs = getInterestingAttrs(path, xattrNames, hasTmAttrs);
        if (!hasTmAttrs) {
            continue;
        }
        const auto status = dirEntryIter.status(ec);
        if (ec) {
            qWarning() << "can't get status"
                       << ec.message()
                       << ", path:" << path.c_str();
        }
        emit entry(this->item, subdirAttrs, path, status);
    }
    emit ended(this->item, std::error_code{});
}
