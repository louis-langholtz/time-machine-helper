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

auto getInterestingAttrs(const std::filesystem::path &path,
                         bool &hasTimeMachineAttrs)
    -> QMap<QString, QByteArray>
{
    auto attrs = QMap<QString, QByteArray>{};
    const auto size = listxattr(path.c_str(), nullptr, 0, 0);
    if (size < 0) {
        qWarning() << "unable to get size for listing attrs for:"
                   << path.c_str();
        return {};
    }
    if (size == 0) {
        return {};
    }
    auto nameBuffer = std::string{};
    nameBuffer.resize(size + 1u);
    const auto result = listxattr(path.c_str(), nameBuffer.data(), size, 0);
    if (result < 0) {
        qWarning() << "unable to list attrs for:" << path.c_str();
        return {};
    }
    const auto xattrNames = split(nameBuffer, '\0');
    for (const auto& attrName: xattrNames) {
        if (attrName.starts_with(timeMachineAttrPrefix)) {
            hasTimeMachineAttrs = true;
        }
        else if (!attrName.starts_with(backupAttrPrefix) &&
                 !attrName.starts_with(backupdAttrPrefix)) {
            continue;
        }
        const auto reserveSize =
            getxattr(path.c_str(), attrName.c_str(), nullptr, 0, 0, 0);
        if (reserveSize < 0) {
            continue;
        }
        QByteArray buffer(qsizetype(reserveSize), Qt::Initialization{});
        const auto actualSize = getxattr(
            path.c_str(), attrName.c_str(), buffer.data(), reserveSize, 0, 0);
        if (actualSize < 0) {
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
    std::error_code ec;
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
        auto hasTmAttrs = false;
        const auto subdirAttrs = getInterestingAttrs(path, hasTmAttrs);
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
