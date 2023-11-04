#ifndef PLISTOBJECT_H
#define PLISTOBJECT_H

#include <map>
#include <string>
#include <variant>
#include <vector>

struct PlistObject;

using PlistVariant = std::variant<
    std::monostate,
    std::vector<PlistObject>, // array
    std::map<std::string, PlistObject>, // dict
    double, // real
    int, // integer
    std::string // string
    >;

enum class PlistElementType: std::size_t {
    none = 0,
    array,
    dict,
    real,
    integer,
    string,
    key,
    plist,
};

struct PlistObject {
    PlistVariant value;
    operator bool() const noexcept
    {
        return value.index() != 0;
    }
};

#endif // PLISTOBJECT_H
