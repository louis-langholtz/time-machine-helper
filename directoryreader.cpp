#include <sys/xattr.h> // for listxattr system calls

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <sstream>

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QTreeWidgetItem>

#include "directoryreader.h"

namespace {

template <class T>
struct AtomicIntegerHolder
{
    QAtomicInteger<T> *held{};
    T originalValue{};

    AtomicIntegerHolder(QAtomicInteger<T> *t, T value): held{t}
    {
        if (this->held) {
            this->originalValue = std::exchange(*(this->held), value);
        }
    }

    ~AtomicIntegerHolder() noexcept
    {
        if (this->held) {
            (void) std::exchange(*(this->held), this->originalValue);
        }
    }

    AtomicIntegerHolder(const AtomicIntegerHolder& other) = delete;
    auto operator=(const AtomicIntegerHolder& other) -> AtomicIntegerHolder = delete;
};

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

auto okay(QDir::Filters filters,
          const std::filesystem::perms& perms)
    -> bool
{
    using std::filesystem::perms::none;
    using std::filesystem::perms::owner_read;
    using std::filesystem::perms::owner_write;
    using std::filesystem::perms::owner_exec;
    const auto mask = ((filters & QDir::Readable)? owner_read: none)|
                      ((filters & QDir::Writable)? owner_write: none)|
                      ((filters & QDir::Executable)? owner_exec: none);
    return (perms & mask) == mask;
}

auto okay(QDir::Filters filters,
          const std::filesystem::file_status& status)
    -> bool
{
    switch (status.type()) {
    case std::filesystem::file_type::regular:
        return (filters & QDir::Files) &&
               okay(filters, status.permissions());
    case std::filesystem::file_type::directory:
        return (filters & QDir::Dirs) &&
               okay(filters, status.permissions());
    case std::filesystem::file_type::symlink:
        return !(filters & QDir::NoSymLinks);
    case std::filesystem::file_type::block:
    case std::filesystem::file_type::character:
    case std::filesystem::file_type::fifo:
    case std::filesystem::file_type::socket:
        return filters & QDir::System;
    case std::filesystem::file_type::none:
    case std::filesystem::file_type::not_found:
    case std::filesystem::file_type::unknown:
        break;
    }
    return false;
}

auto okay(QDir::Filters filters,
          const std::string& name)
    -> bool
{
    if (!name.starts_with(".")) {
        return true;
    }
    if (!(filters & QDir::Hidden)) {
        return false;
    }
    if (name == "." && (filters & QDir::NoDot)) {
        return false;
    }
    if (name == ".." && (filters & QDir::NoDotDot)) {
        return false;
    }
    return true;
}

}

DirectoryReader::DirectoryReader(std::filesystem::path dir,
                                 QObject *parent):
    QObject{parent},
    directory{std::move(dir)}
{
}

DirectoryReader::~DirectoryReader()
{
    if (this->isRunning()) {
        qDebug() << "DirectoryReader::~DirectoryReader awaiting runner end";
        QEventLoop loop;
        connect(this, &DirectoryReader::ended, &loop, &QEventLoop::quit);
        this->requestInterruption();
        loop.exec();
        qDebug() << "DirectoryReader::~DirectoryReader runner ended"
                 << !(this->isRunning());
    }
}

auto DirectoryReader::isRunning() const noexcept -> bool
{
    return this->running;
}

auto DirectoryReader::isInterruptionRequested() const noexcept
    -> bool
{
    return this->interrupt;
}

void DirectoryReader::requestInterruption()
{
    qDebug() << "DirectoryReader::requestInterruption called for" << this->directory;
    this->interrupt = true;
}

auto DirectoryReader::path() const -> std::filesystem::path
{
    return this->directory;
}

auto DirectoryReader::filter() const noexcept
    -> QDir::Filters
{
    return this->filters;
}

auto DirectoryReader::readAttributes() const noexcept
    -> bool
{
    return this->readAttrs;
}

void DirectoryReader::setFilter(QDir::Filters filters)
{
    this->filters = filters;
}

void DirectoryReader::setReadAttributes(bool value)
{
    this->readAttrs = value;
}

void DirectoryReader::run()
{
    const AtomicIntegerHolder hold(&(this->running), true);
    if (hold.originalValue) {
        throw std::logic_error{"called while running"};
    }
    this->read();
}

void DirectoryReader::read()
{
    using std::filesystem::directory_iterator;
    using std::filesystem::directory_options;

    auto ec = std::error_code{};
    const auto options = directory_options::skip_permission_denied;
    const auto it = directory_iterator{this->directory, options, ec};
    if (ec) {
        emit ended(this->directory, ec, {});
        return;
    }
    read(it);
}

void DirectoryReader::read(const std::filesystem::directory_iterator& it)
{
    auto filenames = QSet<QString>{};
    for (const auto& dirEntry: it) {
        if (this->isInterruptionRequested() || !read(dirEntry, filenames)) {
            break;
        }
    }
    emit ended(this->directory, std::error_code{}, filenames);
}

auto DirectoryReader::read(const std::filesystem::directory_entry &dirEntry,
                           QSet<QString> &filenames)
    -> bool
{
    auto ec = std::error_code{};
    const auto& path = dirEntry.path();
    const auto filename = path.filename().string();
    if (!okay(this->filters, filename)) {
        return true;
    }
    const auto status = (this->filters & QDir::NoSymLinks)
                            ? dirEntry.symlink_status(ec)
                            : dirEntry.status(ec);
    if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
        return true;
    }
    if (ec) {
        qWarning() << "can't get status"
                   << ec.message()
                   << ", path:" << path.c_str();
    }
    if (!okay(this->filters, status)) {
        return true;
    }
    if (this->readAttrs) {
        auto xattrMap = QMap<QString, QByteArray>{};
        const auto xattrNames = readAttributeNames(path, ec);
        if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
            return true;
        }
        for (const auto& attrName: xattrNames) {
            if (this->isInterruptionRequested()) {
                return false;
            }
            const auto buffer = readAttribute(path, attrName, ec);
            if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
                break;
            }
            if (ec) {
                continue;
            }
            xattrMap.insert(QString::fromStdString(attrName), buffer);
        }
        if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
            return true;
        }
        emit entry(path, status, xattrMap);
    }
    else {
        emit entry(path, status, QMap<QString,QByteArray>{});
    }
    filenames.insert(QString::fromStdString(filename));
    return true;
}
