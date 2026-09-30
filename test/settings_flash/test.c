#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#define PICO_FLASH_SIZE_BYTES (2*1024*1024)
#define FLASH_SECTOR_SIZE 4096
#define FLASH_PAGE_SIZE 256
static uint8_t chip[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t)chip)
#define pvPortMalloc malloc
#define vPortFree free
static int erases=0, programs=0;
static void vTaskSuspendAll(void){} static void xTaskResumeAll(void){}
static uint32_t save_and_disable_interrupts(void){return 0;} static void restore_interrupts(uint32_t i){(void)i;}
static void flash_range_erase(uint32_t off,size_t n){memset(chip+off,0xff,n);erases++;}
static void flash_range_program(uint32_t off,const uint8_t*d,size_t n){for(size_t i=0;i<n;i++)chip[off+i]&=d[i];programs++;}
#include "block.c"
static int fails=0;
#define CHECK(c,m) do{if(!(c)){printf("FAIL %s\n",m);fails++;}else printf("ok   %s\n",m);}while(0)
int main(void){
  uint8_t buf[216], rd[216];
  memset(chip,0xff,sizeof chip);
  mcu_hw_settings_read(rd,216); CHECK(rd[0]==0xff&&rd[215]==0xff,"blank flash reads as erased");
  mcu_hw_settings_compact(); CHECK(erases==0,"blank: no erase at power-up");
  for(int k=0;k<16;k++){ memset(buf,k,216); CHECK(mcu_hw_settings_write(buf,216),"write"); mcu_hw_settings_read(rd,216); CHECK(rd[0]==k&&rd[215]==k,"reads newest"); }
  CHECK(erases==0,"16 saves: no erase during use");
  memset(buf,0x77,216); mcu_hw_settings_write(buf,216); CHECK(erases==1,"17th save: one erase (sector full)");
  mcu_hw_settings_read(rd,216); CHECK(rd[0]==0x77,"reads after full-sector erase");
  for(int k=0;k<9;k++){ memset(buf,0x40+k,216); mcu_hw_settings_write(buf,216);} int e=erases;
  mcu_hw_settings_compact(); CHECK(erases==e+1,"power-up compacts a half-full log");
  mcu_hw_settings_read(rd,216); CHECK(rd[0]==0x48,"newest kept after compaction");
  CHECK(settings_newest()==0,"compacted into page 0");
  e=erases; mcu_hw_settings_compact(); CHECK(erases==e,"no compaction when room left");
  /* legacy: old build wrote raw blob at sector start */
  memset(chip+SETTINGS_OFFSET,0xff,FLASH_SECTOR_SIZE); memset(buf,0x5a,216); buf[0]='G';
  memcpy(chip+SETTINGS_OFFSET,buf,216);
  mcu_hw_settings_compact(); mcu_hw_settings_read(rd,216); CHECK(memcmp(rd,buf,216)==0,"old layout carried over at power-up");
  CHECK(!mcu_hw_settings_write(buf,300),"too-large write refused");
  printf("%d failures\n",fails); return fails!=0;
}
