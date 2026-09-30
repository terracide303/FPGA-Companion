#pragma once
#include <stdlib.h>
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define pdFALSE 0
#define pvPortMalloc malloc
#define vPortFree free
extern TickType_t now;
static inline TickType_t xTaskGetTickCount(void){return now;}
