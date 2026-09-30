#pragma once
#include <stdio.h>
#define usb_debugf(...) do{printf(__VA_ARGS__);printf("\n");}while(0)
