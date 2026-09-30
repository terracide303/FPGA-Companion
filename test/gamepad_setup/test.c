#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <timers.h>
#include "gamepad_setup.h"
TickType_t now=0; TimerCallbackFunction_t timer_cb;
static unsigned char flash[4096]; static int flash_ok=0;
bool mcu_hw_settings_read(void*b,int l){memcpy(b,flash,l);return true;}
bool mcu_hw_settings_write(const void*b,int l){memset(flash,0xff,sizeof flash);memcpy(flash,b,l);flash_ok++;return true;}
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok   %s\n",msg);}while(0)

/* pad model: returns report for a set of pressed controls (bitmask over GP_CTRL_*) */
typedef void (*padfn)(unsigned pressed, unsigned char *r);
static int RLEN=8;

/* pad 1: dpad as 8-bit axes idle 0x80 in bytes 0/1, buttons bits in byte 5, hat none, noisy counter byte 7 */
static int ctr=0;
static void pad1(unsigned p, unsigned char *r){
  memset(r,0,8); r[0]=0x80; r[1]=0x80;
  if(p&(1<<GP_CTRL_LEFT)) r[0]=0x00; if(p&(1<<GP_CTRL_RIGHT)) r[0]=0xff;
  if(p&(1<<GP_CTRL_UP)) r[1]=0x00; if(p&(1<<GP_CTRL_DOWN)) r[1]=0xff;
  for(int i=4;i<12;i++) if(p&(1<<i)) r[5-(i>=8)+ (i>=8)*1] |= 1<<(i&7); /* byte 5 all */
  r[7]=ctr++;  /* report counter noise */
}
/* pad 2: hat in low nibble of byte 2 (neutral 0x0f), buttons in high nibble byte2 + byte3, 16-bit signed LX at bytes 4..5 */
static void pad2(unsigned p, unsigned char *r){
  memset(r,0,8);
  int hat=0x0f;
  if(p&(1<<GP_CTRL_UP)) hat=0; if(p&(1<<GP_CTRL_RIGHT)) hat=2; if(p&(1<<GP_CTRL_DOWN)) hat=4; if(p&(1<<GP_CTRL_LEFT)) hat=6;
  if((p&(1<<GP_CTRL_UP))&&(p&(1<<GP_CTRL_RIGHT))) hat=1;
  r[2]=hat; for(int i=4;i<8;i++) if(p&(1<<i)) r[2]|=0x10<<(i-4);
  for(int i=8;i<12;i++) if(p&(1<<i)) r[3]|=1<<(i-8);
  short lx=0; r[4]=lx&0xff; r[5]=(lx>>8)&0xff;
}
static unsigned char ax,ay,joy,extra;
static void feed(const hid_report_t*rep,padfn f,unsigned p){unsigned char r[8]; f(p,r); gamepad_setup_feed(rep,r,RLEN);}
static void apply(const hid_report_t*rep,padfn f,unsigned p){unsigned char r[8]; f(p,r); if(!gamepad_setup_feed(rep,r,RLEN)) { if(!gamepad_setup_apply(rep,r,RLEN,&joy,&ax,&ay,&extra)) {joy=extra=0xEE;} } }

static void learn(const hid_report_t*rep,padfn f){
  feed(rep,f,0);                      /* pad seen before */
  gamepad_setup_start(GP_MODE_SETUP);
  now=0; feed(rep,f,0); now=600; for(int i=0;i<5;i++) feed(rep,f,0);  /* idle + noise */
  timer_cb(0);
  for(int c=0;c<GP_CTRLS_ASKED;c++){ feed(rep,f,1u<<c); feed(rep,f,0); }
}
int main(void){
  hid_report_t r1={0}; r1.vid=0x0079; r1.pid=0x0011;
  hid_report_t r2={0}; r2.vid=0x054c; r2.pid=0x0268;
  gamepad_setup_init();

  learn(&r1,pad1);
  CHECK(gamepad_setup_status()->phase==GP_PHASE_SAVED,"pad1 saved");
  static const unsigned char J[GP_CTRLS]={8,4,2,1,0x10,0x20,0,0,0x40,0x80,0,0};
  static const unsigned char X[GP_CTRLS]={0,0,0,0,0,0,4,8,0,0,1,2};
  for(int c=0;c<GP_CTRLS_ASKED;c++){ apply(&r1,pad1,1u<<c); char m[64]; sprintf(m,"pad1 ctrl %d -> joy %02x extra %02x",c,joy,extra); CHECK(joy==J[c]&&extra==X[c],m);} 
  apply(&r1,pad1,0); CHECK(joy==0&&extra==0,"pad1 idle -> nothing");

  learn(&r2,pad2);
  CHECK(gamepad_setup_status()->phase==GP_PHASE_SAVED,"pad2 saved");
  for(int c=0;c<GP_CTRLS_ASKED;c++){ apply(&r2,pad2,1u<<c); char m[64]; sprintf(m,"pad2 ctrl %d -> joy %02x extra %02x",c,joy,extra); CHECK(joy==J[c]&&extra==X[c],m);} 
  apply(&r2,pad2,(1<<GP_CTRL_UP)|(1<<GP_CTRL_RIGHT)); CHECK(joy==(8|1),"pad2 hat diagonal up-right");
  apply(&r2,pad2,0); CHECK(joy==0&&extra==0,"pad2 idle -> nothing");

  /* reload from flash */
  gamepad_setup_init(); apply(&r1,pad1,1<<GP_CTRL_LEFT); CHECK(joy==2,"pad1 after reload from flash");

  /* remove pad1 */
  feed(&r1,pad1,0); gamepad_setup_start(GP_MODE_REMOVE); now=0; feed(&r1,pad1,0); now=600; feed(&r1,pad1,0); timer_cb(0);
  feed(&r1,pad1,1<<GP_CTRL_A); feed(&r1,pad1,0);
  CHECK(gamepad_setup_status()->phase==GP_PHASE_REMOVED,"pad1 removed");
  apply(&r1,pad1,1<<GP_CTRL_A); CHECK(joy==0xEE,"pad1 back on auto-detect");
  apply(&r2,pad2,1<<GP_CTRL_A); CHECK(joy==0x10,"pad2 setup still there");
  gamepad_setup_init(); apply(&r1,pad1,1<<GP_CTRL_A); CHECK(joy==0xEE,"pad1 stays removed after reload");
  printf("%d failures\n",fails); return fails!=0;
}
