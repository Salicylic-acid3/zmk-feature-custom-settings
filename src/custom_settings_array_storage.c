/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <zephyr/logging/log.h>
#include "custom_settings_internal.h"
#include "custom_settings_array_storage.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static size_t stride(const struct zmk_custom_setting *array) {
    return ZMK_CUSTOM_SETTING_ARRAY_STRIDE(array->value_type);
}

static void *element(const struct zmk_custom_setting *array, uint32_t index) {
    return (uint8_t *)array->array_state->values + index * stride(array);
}

void array_value_read(const struct zmk_custom_setting *array, uint32_t index,
                      struct zmk_custom_setting_value *value) {
    *value = (struct zmk_custom_setting_value){.type = array->value_type};
    const void *data = element(array, index);
    if (ZMK_CUSTOM_SETTING_TYPE_IS_BLOB(array->value_type)) {
        const struct zmk_custom_setting_blob *blob = data;
        value->size = blob->size;
        if (blob->size) {
            memcpy(value->bytes_value, blob->data, blob->size);
        }
        /* The carrier has one extra byte for STRING termination. */
        value->string_value[blob->size] = '\0';
    } else {
        memcpy(&value->int32_value, data, stride(array));
    }
}

int array_value_write(const struct zmk_custom_setting *array, uint32_t index,
                      const struct zmk_custom_setting_value *value) {
    void *dest = element(array, index);
    if (ZMK_CUSTOM_SETTING_TYPE_IS_BLOB(array->value_type)) {
        size_t size = array->value_type == ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING
                          ? bounded_strlen(value->string_value, sizeof(value->string_value))
                          : value->size;
        return blob_write_locked(&zmk_custom_settings_shared_pool, dest, value->bytes_value, size,
                                 array->value_type == ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING);
    }
    memcpy(dest, &value->int32_value, stride(array));
    return 0;
}

void array_value_default(const struct zmk_custom_setting *array, uint32_t index) {
    void *dest = element(array, index);
    const struct zmk_custom_setting_value *legacy =
        array->array_state->defaults_are_carriers
            ? &((const struct zmk_custom_setting_value *)array->array_state->defaults)[index]
            : NULL;
    if (ZMK_CUSTOM_SETTING_TYPE_IS_BLOB(array->value_type)) {
        struct zmk_custom_setting_slice slice;
        size_t default_size;
        if (legacy) {
            slice.data = legacy->bytes_value;
            default_size = array->value_type == ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING
                               ? bounded_strlen(legacy->string_value, sizeof(legacy->string_value))
                               : legacy->size;
        } else {
            slice = ((const struct zmk_custom_setting_slice *)array->array_state->defaults)[index];
            default_size = slice.size;
        }
        struct zmk_custom_setting_blob *blob = dest;
        custom_settings_pool_release(&zmk_custom_settings_shared_pool, blob);
        if (default_size > CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE ||
            (default_size && !slice.data) || (legacy && legacy->type != array->value_type)) {
            LOG_ERR("Invalid array default: %s[%u]", array->key, index);
            return;
        }
        blob->data = (uint8_t *)slice.data;
        blob->size = default_size;
    } else if (legacy) {
        /* Only the adapter reads old carriers. Live storage stays typed. */
        memcpy(dest, &legacy->int32_value, stride(array));
    } else {
        memcpy(dest, (const uint8_t *)array->array_state->defaults + index * stride(array),
               stride(array));
    }
}

/* Swap ownership, not bytes in the pool. Stable nodes remain at their array
 * indices; list links are retargeted before swapping node contents. */
void array_value_swap(const struct zmk_custom_setting *array, uint32_t a, uint32_t b) {
    if (a == b) {
        return;
    }
    if (ZMK_CUSTOM_SETTING_TYPE_IS_BLOB(array->value_type)) {
        struct zmk_custom_setting_blob *left = element(array, a);
        struct zmk_custom_setting_blob *right = element(array, b);
        custom_settings_pool_swap(&zmk_custom_settings_shared_pool, left, right);
    } else {
        uint8_t saved[sizeof(struct zmk_custom_setting_behavior_value)];
        memcpy(saved, element(array, a), stride(array));
        memcpy(element(array, a), element(array, b), stride(array));
        memcpy(element(array, b), saved, stride(array));
    }
}
