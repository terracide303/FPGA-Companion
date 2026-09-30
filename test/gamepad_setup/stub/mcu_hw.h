#pragma once
#include <stdbool.h>
bool mcu_hw_settings_read(void *buf, int len);
bool mcu_hw_settings_write(const void *buf, int len);
