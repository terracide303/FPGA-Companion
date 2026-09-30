#pragma once
typedef int FIL; typedef int FRESULT;
#define FR_OK 0
#define FA_OPEN_EXISTING 1
#define FA_READ 2
#define FA_WRITE 4
#define FA_CREATE_ALWAYS 8
static inline int f_open(FIL*f,const char*n,int m){(void)f;(void)n;(void)m;return 1;}
static inline char* f_gets(char*b,int l,FIL*f){(void)b;(void)l;(void)f;return 0;}
static inline int f_close(FIL*f){(void)f;return 0;}
static inline int f_puts(const char*s,FIL*f){(void)s;(void)f;return 0;}
static inline int f_mkdir(const char*s){(void)s;return 0;}
