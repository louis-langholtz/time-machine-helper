#ifndef PLIST_BUILDER_H
#define PLIST_BUILDER_H

#include "coroutine.h"

#include "plist_object.h"

/// @brief Indicates invalid variant type from awaiting handle.
struct invalid_plist_variant_type: std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

/// @brief Builder of plist_object objects.
coroutine_task<plist_object> plist_builder(
    await_handle<plist_variant> *awaiting_handle);

/// @brief Builder of plist_array objects.
coroutine_task<plist_array> plist_array_builder(
    await_handle<plist_variant> *awaiting_handle);

/// @brief Builder of plist_dict objects.
/// @throws invalid_plist_variant_type if key element not plist_string.
coroutine_task<plist_dict> plist_dict_builder(
    await_handle<plist_variant> *awaiting_handle);

#endif // PLIST_BUILDER_H
