// license:BSD-3-Clause
// copyright-holders:Antonio Giner
//============================================================
//
//  scanline.h - Windows scanline polling
//
//============================================================

#pragma once

bool scanline_init(const char *output_name);
void scanline_exit();
bool get_vblank_timestamp_external(uint64_t *counter, uint64_t *timestamp);
void wait_for_vertical_blank();
void scanline_poll(uint32_t *scanline, bool *in_vblank);
