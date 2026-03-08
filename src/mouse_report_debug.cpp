#include "mouse_report_debug.h"
#include <cstring>

static uint8_t s_report[MOUSE_REPORT_DEBUG_MAX_LEN];
static uint16_t s_len = 0;

void mouse_report_debug_store(uint8_t const* report, uint16_t len) {
    if (report == nullptr || len > MOUSE_REPORT_DEBUG_MAX_LEN)
        len = 0;
    s_len = len;
    if (len > 0)
        memcpy(s_report, report, len);
}

void mouse_report_debug_get(uint8_t* buf, uint16_t max_len, uint16_t* out_len) {
    if (out_len) *out_len = 0;
    if (buf == nullptr || out_len == nullptr) return;
    uint16_t copy = s_len;
    if (copy > max_len) copy = max_len;
    *out_len = copy;
    if (copy > 0)
        memcpy(buf, s_report, copy);
}
