#pragma once

#include <stdint.h>

#define BUTTON_INPUT_DEBOUNCE_MS 200

int button_input_init(void);
int button_input_publish_press(int64_t uptime_ms);
int button_input_publish_press_debounced(int64_t uptime_ms);
void button_input_reset_debounce(void);
