/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct zmk_custom_setting;

/* A ref is a copyable identity, never a borrowed descriptor or pool pointer.
 * Array refs name an index, not the value currently occupying that index.
 * Keyspace refs also name a generation, so deletion/reuse returns ESTALE. */
struct zmk_custom_setting_ref {
    const void *owner;
    uint32_t index;
    uint32_t generation;
    enum {
        ZMK_CUSTOM_SETTING_REF_STATIC,
        ZMK_CUSTOM_SETTING_REF_ARRAY,
        ZMK_CUSTOM_SETTING_REF_KEYSPACE
    } kind;
};

typedef int (*zmk_custom_setting_ref_visitor_t)(const struct zmk_custom_setting *setting,
                                                void *context);
int zmk_custom_setting_ref_capture(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_ref *ref);
/* Runs under the settings lock. The descriptor is valid only during visit.
 * Visitors may call synchronous settings APIs, but must not wait for a worker
 * or retain the descriptor. Acquire outer locks before calling this API. */
int zmk_custom_setting_ref_visit(const struct zmk_custom_setting_ref *ref,
                                 zmk_custom_setting_ref_visitor_t visit, void *context);
bool zmk_custom_setting_ref_equal(const struct zmk_custom_setting_ref *a,
                                  const struct zmk_custom_setting_ref *b);
