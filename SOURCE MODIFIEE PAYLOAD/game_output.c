/*
 * Game output bridge for DS5Dongle.
 *
 * The import-hook implementation in bridge/wireless_ds4.c is vendored from
 * PuckbridgePS5/PoorDS4 under GPL-3.0-or-later; see bridge/NOTICE.md.
 */
#include "game_output.h"
#include "bridge/wireless_ds4.h"

#include <pthread.h>
#include <ps5/klog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    TFX_OFF,
    TFX_FEEDBACK,
    TFX_WEAPON,
    TFX_VIBRATION,
    TFX_MULTI_FEEDBACK,
    TFX_SLOPE,
    TFX_MULTI_VIBRATION
};

static pthread_mutex_t g_report_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_report[48];
static uint32_t g_report_seq;
static pthread_t g_output_thread;

void game_output_kernel_lock(void)
{
    pb_kernel_lock();
}

void game_output_kernel_unlock(void)
{
    pb_kernel_unlock();
}

/* Required by the vendored bridge. */
void poords4_log(const char *format, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);
    klog_printf("[GC output] %s", text);
}

static uint8_t clamp_strength(uint8_t value)
{
    return value > 8 ? 8 : value;
}

/* Pack the public ScePad strengths (0..8 per physical zone) into the
 * DualSense motor's active-zone and 3-bit strength fields. */
static void pack_zones(uint8_t dst[11], uint8_t effect,
                       const uint8_t strengths[10], uint8_t frequency)
{
    uint16_t active = 0;
    uint32_t packed = 0;
    int zone;

    memset(dst, 0, 11);
    dst[0] = effect;
    for (zone = 0; zone < 10; zone++) {
        uint8_t strength = clamp_strength(strengths[zone]);
        if (strength != 0) {
            active |= (uint16_t)(1u << zone);
            packed |= (uint32_t)(strength - 1u) << (zone * 3);
        }
    }
    dst[1] = (uint8_t)active;
    dst[2] = (uint8_t)(active >> 8);
    dst[3] = (uint8_t)packed;
    dst[4] = (uint8_t)(packed >> 8);
    dst[5] = (uint8_t)(packed >> 16);
    dst[6] = (uint8_t)(packed >> 24);
    dst[9] = frequency;
}

static void convert_trigger(uint8_t dst[11], const uint8_t command[56])
{
    uint32_t mode;
    const uint8_t *p = command + 8;
    uint8_t strengths[10] = {0};
    int start, end, zone;

    memcpy(&mode, command, sizeof(mode));
    memset(dst, 0, 11);
    switch (mode) {
    case TFX_FEEDBACK:
        start = p[0] > 9 ? 9 : p[0];
        for (zone = start; zone < 10; zone++)
            strengths[zone] = p[1];
        pack_zones(dst, 0x21, strengths, 0);
        break;
    case TFX_WEAPON:
        start = p[0] > 9 ? 9 : p[0];
        end = p[1] > 9 ? 9 : p[1];
        if (end <= start)
            end = start < 9 ? start + 1 : start;
        dst[0] = 0x25;
        dst[1] = (uint8_t)((1u << start) | (1u << end));
        dst[2] = (uint8_t)(((1u << start) | (1u << end)) >> 8);
        dst[3] = clamp_strength(p[2]);
        if (dst[3] != 0)
            dst[3]--;
        break;
    case TFX_VIBRATION:
        start = p[0] > 9 ? 9 : p[0];
        for (zone = start; zone < 10; zone++)
            strengths[zone] = p[1];
        pack_zones(dst, 0x26, strengths, p[2]);
        break;
    case TFX_MULTI_FEEDBACK:
        memcpy(strengths, p, sizeof(strengths));
        pack_zones(dst, 0x21, strengths, 0);
        break;
    case TFX_SLOPE:
        start = p[0] > 9 ? 9 : p[0];
        end = p[1] > 9 ? 9 : p[1];
        if (end < start)
            end = start;
        for (zone = start; zone < 10; zone++) {
            if (zone >= end || end == start) {
                strengths[zone] = p[3];
            } else {
                int span = end - start;
                int delta = (int)p[3] - (int)p[2];
                int value = (int)p[2] + delta * (zone - start) / span;
                strengths[zone] = (uint8_t)(value < 0 ? 0 : value);
            }
        }
        pack_zones(dst, 0x21, strengths, 0);
        break;
    case TFX_MULTI_VIBRATION:
        memcpy(strengths, p + 1, sizeof(strengths));
        pack_zones(dst, 0x26, strengths, p[0]);
        break;
    case TFX_OFF:
    default:
        /* 0x05 is the documented "clear effect" command. */
        dst[0] = 0x05;
        break;
    }
}

static void publish_report(const PbStatus *status)
{
    uint8_t report[48];

    memset(report, 0, sizeof(report));
    report[0] = 0x02;
    /* Compatible vibration, select rumble rather than audio haptics, and
     * update both adaptive-trigger motors. */
    report[1] = 0x0f;
    report[3] = status->vsmall; /* light/right motor */
    report[4] = status->vlarge; /* heavy/left motor */
    convert_trigger(report + 11, status->tcmd[1]); /* right trigger */
    convert_trigger(report + 22, status->tcmd[0]); /* left trigger */

    pthread_mutex_lock(&g_report_lock);
    memcpy(g_report, report, sizeof(g_report));
    g_report_seq++;
    if (g_report_seq == 0)
        g_report_seq = 1;
    pthread_mutex_unlock(&g_report_lock);
}

int game_output_take(uint32_t *cursor, uint8_t report[48])
{
    int changed = 0;
    if (!cursor || !report)
        return 0;
    pthread_mutex_lock(&g_report_lock);
    if (*cursor != g_report_seq && g_report_seq != 0) {
        memcpy(report, g_report, sizeof(g_report));
        *cursor = g_report_seq;
        changed = 1;
    }
    pthread_mutex_unlock(&g_report_lock);
    return changed;
}

static void *output_thread(void *unused)
{
    pid_t pid = -1;
    intptr_t args = 0;
    uint32_t seen_vseq = 0, seen_tseq = 0;
    int read_failures = 0;
    (void)unused;

    for (;;) {
        if (args == 0) {
            char why[192];
            int result = pb_hooks_install(0, 1, 0, &pid, &args,
                                          why, sizeof(why));
            if (result > 0) {
                klog_printf("[GC output] game pid=%d: %s\n", pid, why);
                seen_vseq = seen_tseq = 0;
                read_failures = 0;
            } else {
                args = 0;
                sleep(2);
                continue;
            }
        }

        PbStatus status;
        memset(&status, 0, sizeof(status));
        if (pb_hooks_read(pid, args, &status) != 0) {
            if (++read_failures >= 10) {
                klog_printf("[GC output] game hooks lost; waiting for next game\n");
                pid = -1;
                args = 0;
            }
            usleep(100000);
            continue;
        }
        read_failures = 0;
        if (status.vseq != seen_vseq || status.tseq != seen_tseq) {
            seen_vseq = status.vseq;
            seen_tseq = status.tseq;
            publish_report(&status);
        }
        usleep(8000);
    }
    return NULL;
}

int game_output_start(void)
{
    int result = pthread_create(&g_output_thread, NULL, output_thread, NULL);
    if (result == 0)
        pthread_detach(g_output_thread);
    return result;
}
