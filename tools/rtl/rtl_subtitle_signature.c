/* Persian subtitle layout with signature resolution on private memory snapshots.
 * Gameplay layout is enabled only while the active client has no UI key catcher.
 * Subtitle name/background subtrees are placed beside the measured dialogue and
 * the complete row is centered. Menu layout remains in the original direction.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
typedef void (*Render)(int,void *);
static HMODULE self;
static Render original[2];
static volatile LONG enabled=1,hits[2];
static FILE *logfile;
#include "rtl_optional_log.h"
static int access_ok(const void *p,size_t n,int write) {
 MEMORY_BASIC_INFORMATION m;
 if(VirtualQuery(p,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return 0;
 uintptr_t end=(uintptr_t)m.BaseAddress+m.RegionSize;
 if((uintptr_t)p>end||n>end-(uintptr_t)p)return 0;
 DWORD bits=write?(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY):(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY);
 return (m.Protect&bits)!=0;
}
#include "rtl_signature_resolver.h"
#define RTL_REQUIRE_TEXT_RENDERER
#include "rtl_text_renderer_resolver.h"
#include "rtl_safe_snapshot.h"
#include "rtl_gameplay_state.h"
#include "rtl_subtitle_geometry.h"
static int align_element(void *element,int apply,unsigned char *saved_alignment,unsigned short *saved_direction) {
 unsigned char *e=element;
 if(!access_ok(e,0x1a2,1))return 0;
 if(!(*(uint64_t *)(e+0x38)&0x40))return 0;
 *saved_alignment=e[0x154];memcpy(saved_direction,e+0x1a0,2);
 if(apply){e[0x154]=(e[0x154]&0xfc)|2;unsigned short v=*saved_direction&~0x40;memcpy(e+0x1a0,&v,2);}
 return 1;
}
static void dispatch(int kind,int client,void *element) {
 unsigned char alignment=0;unsigned short direction=0;
 int applied=align_element(element,InterlockedCompareExchange(&enabled,0,0)!=0,&alignment,&direction);
 LONG count=InterlockedIncrement(&hits[kind]);
 if(logfile&&count<=3){log_message("render kind=%s count=%ld element=%p alignment_before=%u applied=%d\n",kind?"cinematic":"subtitle",count,element,alignment,applied);}
 original[kind](client,element);
 /* Keep changes local to this rendering call, including the original element state. */
 if(applied&&access_ok(element,0x1a2,1)) {
   unsigned char *e=element;e[0x154]=alignment;memcpy(e+0x1a0,&direction,2);
 }
}
static void normal_render(int client,void *element){dispatch(0,client,element);}
static void cinematic_render(int client,void *element){dispatch(1,client,element);}
#ifndef RTL_UNIT_TEST
__declspec(dllexport) void RtlCompanionInit(void){}
static DWORD WINAPI worker(LPVOID unused) {
 (void)unused;
 wchar_t path[32768];GetModuleFileNameW(NULL,path,32768);wchar_t *slash=wcsrchr(path,L'\\');
 if(!slash||_wcsicmp(slash+1,L"sp26-cod.exe"))return 1;
 open_optional_log(self);
 log_message("SUBTITLE_GEOMETRY_V8_TRIAL safe_snapshot=1 automatic=1 fixed_address_fallback=0 hotkeys=0\n");
 RtlTargets targets={0};const char *reason="initializing",*last_reason=NULL;
 unsigned char *base=(unsigned char *)GetModuleHandleW(NULL);
 int resolved=0;
 for(unsigned attempt=0;attempt<90;attempt++){
  if(resolve_live_snapshot(base,&targets,&reason)){resolved=1;break;}
  if(reason!=last_reason){log_message("waiting reason=%s\n",reason);last_reason=reason;}
  Sleep(2000);
 }
 if(!resolved){log_message("DISABLED reason=%s no_game_data_changed=1\n",reason);return 4;}
 *targets.flag=0;original_text_renderer=(TextRenderer)targets.text_function;log_message("TEXT_RENDERER_RESOLVED rva=0x%llx\n",(unsigned long long)(targets.text_function-base));
 Render replacements[]={normal_render,cinematic_render};
 for(int i=0;i<2;i++)original[i]=(Render)targets.function[i];
 log_message("READY global_layout=0 normal_rva=0x%llx cinematic_rva=0x%llx normal_id=%u cinematic_id=%u normal_slot_rva=0x%llx cinematic_slot_rva=0x%llx flag_rva=0x%llx\n",
  (unsigned long long)(targets.function[0]-base),(unsigned long long)(targets.function[1]-base),targets.ids[0],targets.ids[1],
  (unsigned long long)((unsigned char*)targets.slot[0]-base),(unsigned long long)((unsigned char*)targets.slot[1]-base),(unsigned long long)(targets.flag-base));
 DWORD ticks=0;
 unsigned char *gameplay_state=find_gameplay_state(base,targets.function[0]);
 unsigned char last_state[0x40];memset(last_state,0xff,sizeof(last_state));
 if(!gameplay_state){log_message("GAMEPLAY_LAYOUT_DISABLED state signature not recognized\n");}
 else log_message("GAMEPLAY_STATE rva=0x%llx\n",(unsigned long long)(gameplay_state-base));
 for(;;){
  if(gameplay_state){
   unsigned char state[0x40];
   if(safe_read(gameplay_state,state,sizeof(state))){
    int desired=gameplay_layout_enabled(state);InterlockedExchange(&gameplay_active,desired);
    if(access_ok(targets.flag,1,1))*targets.flag=(unsigned char)desired;
    if(memcmp(state,last_state,sizeof(state))){
     unsigned keys,connection;memcpy(&keys,state+4,4);memcpy(&connection,state+8,4);
     log_message("GAMEPLAY_STATE rtl=%d connection=%u key_catchers=0x%x bytes=",desired,connection,keys);
     for(unsigned n=0;n<sizeof(state);n++)log_message("%02x",state[n]);
     log_message("\n");memcpy(last_state,state,sizeof(state));
    }
   }else if(access_ok(targets.flag,1,1))*targets.flag=0;
  }
  unsigned char callbacks[101*16];
  if(safe_read(targets.table,callbacks,sizeof(callbacks))){
   for(unsigned id=0;id<=100;id++){
    void *fn;memcpy(&fn,callbacks+id*16+8,8);
    if(callbacks[id*16]==1&&fn==(void*)original_text_renderer){
     void **slot=(void**)(targets.table+id*16+8);
     if(access_ok(slot,8,1)&&InterlockedCompareExchangePointer(slot,(void*)subtitle_text_renderer,fn)==fn)log_message("TEXT_RENDERER_ATTACHED id=%u function=%p\n",id,fn);
    }
   }
  }
  for(int k=0;k<2;k++){
   void **slot=targets.slot[k];
   if(access_ok(slot,sizeof(*slot),1)&&*slot==(void *)original[k]){
    void *before=InterlockedCompareExchangePointer(slot,(void *)replacements[k],(void *)original[k]);
    if(before==(void *)original[k]){log_message("callback_attached kind=%s\n",k?"cinematic":"subtitle");}
   }
  }
  if(++ticks%200==0){log_message("status global_layout=%u subtitle_calls=%ld cinematic_calls=%ld\n",*targets.flag,hits[0],hits[1]);}
  Sleep(50);
 }
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved){
 (void)reserved;if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);HANDLE h=CreateThread(NULL,0,worker,NULL,0,NULL);if(!h)return FALSE;CloseHandle(h);}return TRUE;
}
#endif
