/* Resolve patterns only in a private, bounded copy. Never scan live pages. */
static int safe_read(const void *source,void *destination,size_t size) {
 SIZE_T copied=0;
 return ReadProcessMemory(GetCurrentProcess(),source,destination,size,&copied)&&copied==size;
}
static int resolve_live_snapshot(unsigned char *base,RtlTargets *out,const char **reason) {
 IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
 *reason="unreadable image headers";
 if(!safe_read(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x100000)return 0;
 if(!safe_read(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return 0;
 size_t size=nt.OptionalHeader.SizeOfImage,headers=nt.OptionalHeader.SizeOfHeaders;
 if(size<4096||size>0x60000000||headers<sizeof(dos)||headers>0x100000||headers>size)return 0;
 unsigned char *copy=VirtualAlloc(NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!copy){*reason="snapshot allocation failed";return 0;}
 int result=0;ImageView view;RtlTargets candidate={0};
 if(!safe_read(base,copy,headers)||!image_view(copy,&view))goto done;
 view.live_base=base;
 for(unsigned i=0;i<view.count;i++){
  IMAGE_SECTION_HEADER section=view.sections[i];
  size_t cursor=section.VirtualAddress,end=cursor+section.Misc.VirtualSize;
  while(cursor<end){
   MEMORY_BASIC_INFORMATION m;
   if(VirtualQuery(base+cursor,&m,sizeof(m))!=sizeof(m))break;
   uintptr_t region_end=(uintptr_t)m.BaseAddress+m.RegionSize;
   if(region_end<=(uintptr_t)(base+cursor))break;
   size_t next=(size_t)(region_end-(uintptr_t)base);if(next>end)next=end;
   if(m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))){
    for(size_t at=cursor;at<next;){
     size_t chunk=next-at;if(chunk>65536)chunk=65536;
     SIZE_T copied=0;
     /* On partial/failing reads, keep the zero-filled unreadable remainder. */
     ReadProcessMemory(GetCurrentProcess(),base+at,copy+at,chunk,&copied);
     at+=chunk;
    }
   }
   cursor=next;
  }
 }
 if(!resolve_rtl(&view,&candidate,reason))goto done;
#ifdef RTL_REQUIRE_TEXT_RENDERER
 candidate.text_function=find_text_renderer(&view);
 if(!candidate.text_function){*reason="text renderer not recognized";goto done;}
 {
  unsigned char live[64];size_t rva=candidate.text_function-copy;
  if(!safe_read(base+rva,live,sizeof(live))||memcmp(live,candidate.text_function,sizeof(live))){*reason="text renderer changed";goto done;}
  candidate.text_function=base+rva;
 }
#endif
#ifdef RTL_REQUIRE_TEXT_RENDERER
 for(unsigned k=0;k<2;k++){
  unsigned char *fn=find_caption_background_renderer(&view,k),live[64];
  if(fn&&in_section(&view,fn,sizeof(live),IMAGE_SCN_MEM_EXECUTE)){
   size_t rva=fn-copy;
   if(safe_read(base+rva,live,sizeof(live))&&!memcmp(live,fn,sizeof(live)))candidate.background_function[k]=base+rva;
  }
 }
#endif
 candidate.table=base+(candidate.table-copy);
 for(int i=0;i<2;i++){
  size_t function_rva=candidate.function[i]-copy;
  size_t slot_rva=(unsigned char*)candidate.slot[i]-copy;
  unsigned char live_code[64];void *live_slot=NULL;
  if(!in_section(&view,candidate.function[i],sizeof(live_code),IMAGE_SCN_MEM_EXECUTE)||!safe_read(base+function_rva,live_code,sizeof(live_code))||memcmp(live_code,candidate.function[i],sizeof(live_code))||!safe_read(base+slot_rva,&live_slot,sizeof(live_slot))||(live_slot&&live_slot!=base+function_rva)||!access_ok(base+slot_rva,sizeof(void*),1)){
   *reason="live callback changed or unreadable";goto done;
  }
  candidate.function[i]=base+function_rva;candidate.slot[i]=(void**)(base+slot_rva);
 }
 {
  size_t flag_rva=candidate.flag-copy;unsigned char flag;
  if(!safe_read(base+flag_rva,&flag,1)||flag>1||!access_ok(base+flag_rva,1,1)){*reason="live flag changed or unreadable";goto done;}
  candidate.flag=base+flag_rva;
 }
 *out=candidate;result=1;
done:
 VirtualFree(copy,0,MEM_RELEASE);return result;
}
