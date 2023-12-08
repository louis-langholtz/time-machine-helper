#ifndef PLIST_OBJECT_H
#define PLIST_OBJECT_H

#include <chrono>
#include <cstdint> // for std::int64_t
#include <map>
#include <optional>
#include <string>
#include <type_traits> // for std::true_type, std::false_type
#include <variant>
#include <vector>

struct plist_object;

using plist_none = std::monostate;
using plist_array = std::vector<plist_object>;
using plist_data = std::vector<char>;
using plist_date = std::chrono::time_point<std::chrono::system_clock>;
using plist_dict = std::map<std::string, plist_object>;
using plist_real = double;
using plist_integer = std::int64_t;
using plist_string = std::string;
using plist_true = std::true_type;
using plist_false = std::false_type;

using plist_variant = std::variant<
    plist_none,
    plist_array,
    plist_data,
    plist_date,
    plist_dict,
    plist_real,
    plist_integer,
    plist_string,
    plist_true,
    plist_false
    >;

enum class plist_element_type: std::size_t {
    none = 0,
    array,
    data,
    date,
    dict,
    real,
    integer,
    string,
    so_true,
    so_false,
    key,
    plist,
};

struct plist_object {
    plist_variant value;

    operator bool() const noexcept
    {
        return value.index() != 0;
    }
};

template <class T>
auto get(const plist_dict& map, const plist_string& key)
    -> std::optional<T>
{
    if (const auto it = map.find(key); it != map.end()) {
        if (const auto p = std::get_if<T>(&it->second.value)) {
            return {*p};
        }
    }
    return {};
}

#endif // PLIST_OBJECT_H
