#ifndef PLIST_BUILDER_H
#define PLIST_BUILDER_H

#include "coroutine.h"

#include "plist_object.h"

/// @brief Indicates invalid variant type from awaiting handle.
struct invalid_plist_variant_type: std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

/// @brief Builder of plist_object objects.
auto plist_builder(await_handle<plist_variant> *awaiting_handle)
    -> coroutine_task<plist_object>;

/// @brief Builder of plist_array objects.
auto plist_array_builder(await_handle<plist_variant> *awaiting_handle)
    -> coroutine_task<plist_array>;

/// @brief Builder of plist_dict objects.
/// @throws invalid_plist_variant_type if key element not plist_string.
auto plist_dict_builder(await_handle<plist_variant> *awaiting_handle)
    -> coroutine_task<plist_dict>;

#endif // PLIST_BUILDER_H
