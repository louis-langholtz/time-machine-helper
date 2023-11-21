#include <coroutine>

#include "plist_builder.h"

auto plist_array_builder(await_handle<plist_variant> *awaiting_handle)
    -> coroutine_task<plist_array>
{
    auto result = plist_array{};
    while (const auto object = co_await plist_builder(awaiting_handle)) {
        result.push_back(object);
    }
    co_return result;
}

auto plist_dict_builder(await_handle<plist_variant> *awaiting_handle)
    -> coroutine_task<plist_dict>
{
    auto result = plist_dict{};
    for (;;) {
        const auto dict_key = co_await *awaiting_handle;
        if (dict_key.index() == 0) {
            break;
        }
        const auto pstring = std::get_if<plist_string>(&dict_key);
        if (!pstring) {
            throw invalid_plist_variant_type{"dict key not string?"};
        }
        const auto dict_value = co_await plist_builder(awaiting_handle);
        result.emplace(*pstring, dict_value);
    }
    co_return result;
}

auto plist_builder(await_handle<plist_variant> *awaiting_handle)
    -> coroutine_task<plist_object>
{
    auto result = plist_object{};
    const auto variant = co_await *awaiting_handle;
    const auto element_type = plist_element_type(variant.index());
    switch (element_type) {
    case plist_element_type::none:
        break;
    case plist_element_type::array:
    {
        result.value = co_await plist_array_builder(awaiting_handle);
        break;
    }
    case plist_element_type::dict:
    {
        result.value = co_await plist_dict_builder(awaiting_handle);
        break;
    }
    case plist_element_type::data:
    case plist_element_type::date:
    case plist_element_type::so_true:
    case plist_element_type::so_false:
    case plist_element_type::real:
    case plist_element_type::integer:
    case plist_element_type::string:
    case plist_element_type::key:
        result.value = variant;
        break;
    case plist_element_type::plist:
        break;
    }
    co_return result;
}
