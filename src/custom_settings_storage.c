/*
 * Copyright (c) 2026 Salicylic_acid3
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * How full the settings storage is, as two settings the app can read.
 *
 * Settings live in NVS on a flash partition of fixed size. When it is full,
 * settings_save_one() fails, and nothing above it says so to the person at
 * the keyboard: a setting changed from the app takes effect until the next
 * reboot and then comes back as it was. On ClickBoard ErgoTrack this was
 * every setting at once -- three layers of keymap, the bonds, sixty-odd
 * trackpad settings, two ripple maps and the macro, combo and tap-dance
 * slots had filled 32 KB -- and it was diagnosed by elimination, because
 * no number anywhere said "full".
 *
 * These two numbers are that. They are published as TEMPORARY settings
 * under this module's own subsystem id, so they never touch the storage
 * they describe, and refreshed after every save (anything that writes
 * through this module) and on a slow timer (for what writes around it: the
 * keymap, the bonds).
 *
 * The free figure is NVS's own estimate (nvs_calc_free_space), which keeps
 * one sector in reserve for garbage collection; it is the space a new
 * record can actually take, not the raw unused flash.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <cormoran/zmk/custom_settings.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* The module's own Studio subsystem (custom_settings_handler.c), so the
 * settings are listed without a registration of their own. */
#define STORAGE_SUBSYS "cormoran_custom_settings"

ZMK_CUSTOM_SETTING_DEFINE(custom_settings_storage_total, STORAGE_SUBSYS, "storage_total_bytes",
                          ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, ZMK_CUSTOM_SETTING_VALUE_INT32(0),
                          ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
                          ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                          ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

ZMK_CUSTOM_SETTING_DEFINE(custom_settings_storage_free, STORAGE_SUBSYS, "storage_free_bytes",
                          ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, ZMK_CUSTOM_SETTING_VALUE_INT32(0),
                          ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
                          ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                          ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

/* A save a second apart is one refresh, not two; and the keymap and bonds
 * write around this module, so the timer catches those. */
#define STORAGE_REPORT_DEBOUNCE K_MSEC(500)
#define STORAGE_REPORT_PERIOD K_SECONDS(60)

static void publish(const struct zmk_custom_setting *setting, int32_t number) {
    const struct zmk_custom_setting_value value = {
        .type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
        .int32_value = number,
    };
    int ret = zmk_custom_setting_write(setting, &value, ZMK_CUSTOM_SETTING_WRITE_MODE_TEMPORARY);

    if (ret < 0) {
        LOG_WRN("could not publish %s (%d)", setting->key, ret);
    }
}

static void storage_report_refresh(void) {
    void *storage = NULL;

    if (settings_storage_get(&storage) != 0 || storage == NULL) {
        return;
    }

    struct nvs_fs *fs = storage;
    const ssize_t free_bytes = nvs_calc_free_space(fs);
    const int32_t total = (int32_t)(fs->sector_count * fs->sector_size);

    publish(&custom_settings_storage_total, total);
    publish(&custom_settings_storage_free, free_bytes < 0 ? 0 : (int32_t)free_bytes);
    if (free_bytes >= 0 && free_bytes < (ssize_t)fs->sector_size) {
        LOG_WRN("settings storage nearly full: %d of %d bytes free", (int)free_bytes, total);
    }
}

static void storage_report_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(storage_report_work, storage_report_work_cb);

static void storage_report_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    storage_report_refresh();
    k_work_reschedule(&storage_report_work, STORAGE_REPORT_PERIOD);
}

static int storage_report_listener(const zmk_event_t *eh) {
    if (as_zmk_custom_settings_initialized(eh) != NULL) {
        k_work_reschedule(&storage_report_work, STORAGE_REPORT_DEBOUNCE);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    if (changed != NULL && (changed->kind == ZMK_CUSTOM_SETTING_CHANGED_SAVED ||
                            changed->kind == ZMK_CUSTOM_SETTING_CHANGED_RESET)) {
        k_work_reschedule(&storage_report_work, STORAGE_REPORT_DEBOUNCE);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(custom_settings_storage_report, storage_report_listener);
ZMK_SUBSCRIPTION(custom_settings_storage_report, zmk_custom_settings_initialized);
ZMK_SUBSCRIPTION(custom_settings_storage_report, zmk_custom_setting_changed);
