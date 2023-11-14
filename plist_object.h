#ifndef PLIST_OBJECT_H
#define PLIST_OBJECT_H

#include <map>
#include <string>
#include <type_traits> // for std::true_type, std::false_type
#include <variant>
#include <vector>

struct plist_object;

using plist_none = std::monostate;
using plist_array = std::vector<plist_object>;
using plist_dict = std::map<std::string, plist_object>;
using plist_real = double;
using plist_integer = int;
using plist_string = std::string;
using plist_true = std::true_type;
using plist_false = std::false_type;

using plist_variant = std::variant<
    plist_none,
    plist_array,
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

#endif // PLIST_OBJECT_H
