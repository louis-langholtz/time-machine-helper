#include <sys/xattr.h> // for listxattr system calls

#include <filesystem>
#include <sstream>

#include <QTreeWidgetItem>

#include "directoryreader.h"

namespace {

static constexpr auto timeMachineAttrPrefix = "com.apple.timemachine.";

// Content of this attribute seems to be comma separated list, where
// first element is one of the following:
//   "SnapshotStorage","MachineStore", "Backup", "VolumeStore"
static constexpr auto timeMachineMetaAttr =
    "com.apple.timemachine.private.structure.metadata";

static constexpr auto backupAttrPrefix = "com.apple.backup.";

static constexpr auto backupdAttrPrefix    = "com.apple.backupd.";

// Machine level attributes...
static constexpr auto machineMacAddrAttr   = "com.apple.backupd.BackupMachineAddress";
static constexpr auto machineCompNameAttr  = "com.apple.backupd.ComputerName";
static constexpr auto machineUuidAttr      = "com.apple.backupd.HostUUID";
static constexpr auto machineModelAttr     = "com.apple.backupd.ModelID";
static constexpr auto snapshotTypeAttr     = "com.apple.backupd.SnapshotType";
static constexpr auto totalBytesCopiedAttr = "com.apple.backupd.SnapshotTotalBytesCopied";

// Volume level attributes...
static constexpr auto fileSystemTypeAttr   = "com.apple.backupd.fstypename";
static constexpr auto volumeBytesUsedAttr  = "com.apple.backupd.VolumeBytesUsed";

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
std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

std::map<std::string, std::vector<char>> getInterestingAttrs(
    const std::filesystem::path& path,
    bool &hasTimeMachineAttrs)
{
    auto attrs = std::map<std::string, std::vector<char>>{};
    const auto size = listxattr(path.c_str(), nullptr, 0, 0);
    if (size < 0) {
        qWarning() << "unable to get size for listing attrs for:" << path.c_str();
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
        do {
            if (attrName.starts_with(timeMachineAttrPrefix)) {
                hasTimeMachineAttrs = true;
                break;
            }
            if (attrName.starts_with(backupAttrPrefix)) {
                break;
            }
            if (attrName.starts_with(backupdAttrPrefix)) {
                break;
            }
            continue;
        } while (false);
        const auto reserveSize =
            getxattr(path.c_str(), attrName.c_str(), nullptr, 0, 0, 0);
        if (reserveSize < 0) {
            continue;
        }
        auto buffer = std::vector<char>(std::size_t(reserveSize) + 1u);
        const auto actualSize =
            getxattr(path.c_str(), attrName.c_str(), buffer.data(), reserveSize, 0, 0);
        if (actualSize < 0) {
            continue;
        }
        attrs.emplace(attrName, buffer);
    }
    return attrs;
}

template <class T>
std::optional<T> get(const std::map<std::string, T>& map, const std::string& key)
{
    if (const auto it = map.find(key); it != map.end()) {
        return {it->second};
    }
    return {};
}

std::string toString(const std::vector<char>& value)
{
    const auto first = value.rbegin();
    const auto last = value.rend();
    const auto it = std::find_if(first, last, [](char c){
        return c != '\0';
    });
    const auto diff = (it != last)
                          ? (&(*it) - value.data()) + 1
                          : std::ptrdiff_t(value.size());
    return std::string(value.data(), value.data() + diff);
}

}

DirectoryReader::DirectoryReader(
    QTreeWidgetItem *i, QObject *parent)
    : QThread{parent}, item{i}
{}

void DirectoryReader::run() {
    const QString pathName = item->data(0, Qt::ItemDataRole::UserRole).toString();

    std::error_code ec;
    using std::filesystem::directory_iterator;
    using std::filesystem::directory_options;
    const auto dirPath = std::filesystem::path(pathName.toStdString());
    auto ignore = false;
    const auto dirAttrs = getInterestingAttrs(dirPath, ignore);
    auto countMachineBackups = std::optional<std::size_t>{};
    if (const auto value = get<std::vector<char>>(dirAttrs, machineUuidAttr)) {
        qDebug() << "host UUID:"
                 << QString::fromStdString(toString(*value));
        countMachineBackups = {0};
    }
    const auto it = directory_iterator{dirPath,
                                       directory_options::skip_permission_denied,
                                       ec};
    if (ec) {
        emit ended(this->item, {}, ec.value());
        return;
    }

    for (const auto& dirEntryIter: it) {
        const auto path = dirEntryIter.path();
        const auto filename = path.filename().string();
        if (filename.compare(".") == 0 || filename.compare("..") == 0) {
            continue;
        }
        auto hasTimeMachinceAttrs = false;
        const auto subdirAttrs = getInterestingAttrs(path, hasTimeMachinceAttrs);
        if (!hasTimeMachinceAttrs) {
            continue;
        }
        qDebug() << "adding item" << filename.c_str();
        QMap<int, QString> textMap;
        QMap<int, QPair<int, QVariant>> dataMap;
        textMap.insert(0, QString::fromStdString(filename));
        dataMap.insert(0, {Qt::ItemDataRole::UserRole, QString(path.c_str())});
        if (const auto value = get<std::vector<char>>(subdirAttrs, machineCompNameAttr)) {
            qDebug() << "computer name:"
                     << QString::fromStdString(toString(*value));
        }
        if (const auto value = get<std::vector<char>>(subdirAttrs, snapshotTypeAttr)) {
            textMap.insert(4, QString::fromStdString(toString(*value)));
        }
        if (const auto value = get<std::vector<char>>(subdirAttrs, totalBytesCopiedAttr)) {
            const auto totalBytesCopiedAsString =
                QString::fromStdString(toString(*value));
            auto okay = false;
            const auto bytes = totalBytesCopiedAsString.toLongLong(&okay);
            if (okay) {
                const auto megaBytes = double(bytes) / (1000 * 1000);
                textMap.insert(5, QString::number(megaBytes, 'f', 2));
            }
        }
        if (const auto value = get<std::vector<char>>(subdirAttrs, fileSystemTypeAttr)) {
            textMap.insert(6, QString::fromStdString(toString(*value)));
        }
        if (const auto value = get<std::vector<char>>(subdirAttrs, volumeBytesUsedAttr)) {
            textMap.insert(7, QString::fromStdString(toString(*value)));
        }
        if (!is_directory(path)) {
            continue;
        }
        if (countMachineBackups) {
            countMachineBackups = {*countMachineBackups + 1u};
        }
        emit entry(this->item, textMap, dataMap);
    }
    QMap<int, QString> dirTextMap;
    if (countMachineBackups) {
        dirTextMap.insert(3, QString::number(*countMachineBackups));
        item->setTextAlignment(3, Qt::AlignRight);
    }
    emit ended(this->item, dirTextMap, 0);
}
