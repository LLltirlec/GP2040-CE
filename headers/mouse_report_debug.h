/*
 * Debug: expose last raw HID mouse report for axis-order detection.
 * GET /api/getMouseReportDebug returns the last report bytes.
 */
#ifndef MOUSE_REPORT_DEBUG_H
#define MOUSE_REPORT_DEBUG_H

#include <stdint.h>

#define MOUSE_REPORT_DEBUG_MAX_LEN 16

// Store raw report (called from keyboard host listener). len must be <= MOUSE_REPORT_DEBUG_MAX_LEN.
void mouse_report_debug_store(uint8_t const* report, uint16_t len);

// Copy last stored report into buf, set *out_len. No report stored yet: *out_len = 0.
void mouse_report_debug_get(uint8_t* buf, uint16_t max_len, uint16_t* out_len);

#endif
