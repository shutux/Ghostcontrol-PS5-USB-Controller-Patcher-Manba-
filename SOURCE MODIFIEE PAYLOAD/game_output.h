#pragma once

#include <stdint.h>

/* Starts the game-process hooks that capture scePad output calls. */
int game_output_start(void);

/* The vendored PS5 SDK kernel helpers share process-global scratch state.
 * Serialize shellui_pad calls with the game bridge. */
void game_output_kernel_lock(void);
void game_output_kernel_unlock(void);

/* Copies the newest DualSense USB output report when it changed.
 * cursor is private to each USB thread. Returns 1 when report[48] is valid. */
int game_output_take(uint32_t *cursor, uint8_t report[48]);
