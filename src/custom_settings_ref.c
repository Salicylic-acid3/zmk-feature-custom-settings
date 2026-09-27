/* SPDX-License-Identifier: MIT */
#include <errno.h>

#include "custom_settings_internal.h"
#include <cormoran/zmk/custom_settings/ref.h>

bool zmk_custom_setting_ref_equal(const struct zmk_custom_setting_ref *a,
                                  const struct zmk_custom_setting_ref *b) {
    return a->owner == b->owner && a->index == b->index && a->generation == b->generation &&
           a->kind == b->kind;
}

int zmk_custom_setting_ref_capture(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_ref *ref) {
    if (!setting || !ref) {
        return -EINVAL;
    }
    int ret = 0;
    k_mutex_lock(&custom_settings_lock, K_FOREVER);
    *ref = (struct zmk_custom_setting_ref){.owner = setting};
    if (zmk_custom_setting_keyspace_of(setting)) {
        const struct zmk_custom_setting_keyspace *owner = setting->_keyspace;
        ret = -ESTALE;
        for (uint32_t i = 0; i < owner->max_entries; ++i) {
            if (&owner->slots[i].setting == setting && owner->slots[i].in_use) {
                *ref = (struct zmk_custom_setting_ref){.owner = owner,
                                                       .index = i,
                                                       .generation = owner->slots[i].generation,
                                                       .kind = ZMK_CUSTOM_SETTING_REF_KEYSPACE};
                ret = 0;
                break;
            }
        }
    } else if (zmk_custom_setting_is_array(setting) &&
               setting->array_index != ZMK_CUSTOM_SETTING_ARRAY_NONE) {
        ret = -ENOENT;
        ZMK_CUSTOM_SETTING_FOREACH(parent) {
            if (zmk_custom_setting_is_array(parent) &&
                parent->array_state == setting->array_state) {
                *ref = (struct zmk_custom_setting_ref){.owner = parent,
                                                       .index = setting->array_index,
                                                       .kind = ZMK_CUSTOM_SETTING_REF_ARRAY};
                ret = 0;
                break;
            }
        }
    }
    k_mutex_unlock(&custom_settings_lock);
    return ret;
}

int zmk_custom_setting_ref_visit(const struct zmk_custom_setting_ref *ref,
                                 zmk_custom_setting_ref_visitor_t visit, void *context) {
    if (!ref || !ref->owner || !visit) {
        return -EINVAL;
    }
    k_mutex_lock(&custom_settings_lock, K_FOREVER);
    const struct zmk_custom_setting *setting = ref->owner;
    struct zmk_custom_setting view;
    int ret = 0;
    switch (ref->kind) {
    case ZMK_CUSTOM_SETTING_REF_STATIC:
        break;
    case ZMK_CUSTOM_SETTING_REF_ARRAY:
        if (!zmk_custom_setting_is_array(setting) || ref->index >= setting->array_state->size) {
            ret = -ENOENT;
            break;
        }
        view = *setting;
        view.array_index = ref->index;
        setting = &view;
        break;
    case ZMK_CUSTOM_SETTING_REF_KEYSPACE: {
        const struct zmk_custom_setting_keyspace *owner = ref->owner;
        if (!IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS_KEYSPACE) || ref->index >= owner->max_entries ||
            !owner->slots[ref->index].in_use ||
            owner->slots[ref->index].generation != ref->generation) {
            ret = -ESTALE;
            break;
        }
        setting = &owner->slots[ref->index].setting;
        break;
    }
    default:
        ret = -EINVAL;
    }
    if (ret == 0) {
        ret = visit(setting, context);
    }
    k_mutex_unlock(&custom_settings_lock);
    return ret;
}
