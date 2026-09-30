#pragma once
typedef void (*TimerCallbackFunction_t)(void*);
typedef void* TimerHandle_t;
extern TimerCallbackFunction_t timer_cb;
static inline TimerHandle_t xTimerCreate(const char*n,unsigned p,int r,void*id,void(*cb)(TimerHandle_t)){(void)n;(void)p;(void)r;(void)id;timer_cb=(TimerCallbackFunction_t)cb;return (void*)1;}
static inline int xTimerStart(TimerHandle_t t,int w){(void)t;(void)w;return 1;}
