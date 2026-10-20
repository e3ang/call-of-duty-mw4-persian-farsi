#include "rtl_signatures.h"
typedef struct {unsigned char *base;size_t size;IMAGE_SECTION_HEADER *sections;unsigned count;unsigned char *live_base;} ImageView;
typedef struct {unsigned char *function[2];void **slot[2];unsigned char *flag;unsigned char ids[2];unsigned char *table,*text_function,*background_function[2];} RtlTargets;
static int image_view(unsigned char *base,ImageView *v){
 if(!access_ok(base,sizeof(IMAGE_DOS_HEADER),0))return 0;
 IMAGE_DOS_HEADER *d=(void*)base;if(d->e_magic!=IMAGE_DOS_SIGNATURE||d->e_lfanew<=0||d->e_lfanew>0x100000)return 0;
 IMAGE_NT_HEADERS64 *n=(void*)(base+d->e_lfanew);
 if(!access_ok(n,sizeof(*n),0)||n->Signature!=IMAGE_NT_SIGNATURE||n->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||n->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return 0;
 unsigned count=n->FileHeader.NumberOfSections;size_t size=n->OptionalHeader.SizeOfImage;
 if(!count||count>96||size<4096||size>0x60000000)return 0;
 IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(n);if(!access_ok(s,count*sizeof(*s),0))return 0;
 for(unsigned i=0;i<count;i++)if(s[i].VirtualAddress>=size||s[i].Misc.VirtualSize>size-s[i].VirtualAddress)return 0;
 *v=(ImageView){base,size,s,count,base};return 1;
}
static int in_section(ImageView *v,const void *p,size_t size,DWORD characteristic){
 uintptr_t at=(uintptr_t)p,b=(uintptr_t)v->base;if(at<b||at-b>=v->size||size>v->size-(at-b))return 0;
 for(unsigned i=0;i<v->count;i++){IMAGE_SECTION_HEADER *s=v->sections+i;size_t off=at-b;
  if((s->Characteristics&characteristic)==characteristic&&off>=s->VirtualAddress&&off-s->VirtualAddress<=s->Misc.VirtualSize&&size<=s->Misc.VirtualSize-(off-s->VirtualAddress))return 1;
 }return 0;
}
static int masked_equal(const unsigned char *p,const unsigned char *pattern,const unsigned char *mask,size_t n){for(size_t i=0;i<n;i++)if(mask[i]&&p[i]!=pattern[i])return 0;return 1;}
/* Scan only readable committed parts of executable PE sections. No disk-byte fallback. */
static unsigned find_pattern(ImageView *v,const unsigned char *pattern,const unsigned char *mask,size_t n,unsigned char **result){
 unsigned count=0;*result=NULL;
 for(unsigned i=0;i<v->count;i++){
  IMAGE_SECTION_HEADER *s=v->sections+i;if(!(s->Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
  uintptr_t cursor=(uintptr_t)v->base+s->VirtualAddress,end=cursor+s->Misc.VirtualSize;
  while(cursor<end){MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void*)cursor,&m,sizeof(m)))break;
   uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=cursor)break;if(next>end)next=end;
   /* Include boundary-spanning matches only when the complete candidate is readable. */
   if(access_ok((void*)cursor,1,0)){
    unsigned char *p=(void*)cursor;size_t len=next-cursor;
    while(len){unsigned char *hit=memchr(p,pattern[0],len);if(!hit)break;
     if((uintptr_t)hit+n<=end&&((uintptr_t)hit+n<=next||access_ok(hit,n,0))&&masked_equal(hit,pattern,mask,n)){
      *result=hit;if(++count>1)return count;
     }size_t advance=(size_t)(hit-p)+1;p+=advance;len-=advance;
    }
   }cursor=next;
  }
 }return count;
}
static unsigned char *relative_target(unsigned char *p,unsigned displacement,unsigned instruction_size){int32_t d;memcpy(&d,p+displacement,4);return (unsigned char *)((uintptr_t)p+instruction_size+(intptr_t)d);}
static int resolve_rtl(ImageView *v,RtlTargets *out,const char **reason){
 unsigned char *normal,*cinematic,*setter,*reader,*direction;
#define UNIQUE(name,target) do{unsigned n=find_pattern(v,sig_##name##_bytes,sig_##name##_mask,sizeof(sig_##name##_bytes),&target);if(n!=1){*reason=n?"ambiguous " #name:"missing " #name;return 0;}}while(0)
 UNIQUE(normal,normal);UNIQUE(cinematic,cinematic);UNIQUE(setter,setter);UNIQUE(rtl_reader,reader);UNIQUE(direction,direction);
#undef UNIQUE
 /* Reject unverified element layouts rather than applying old field offsets. */
 static const unsigned char alignment[]={0x0f,0xbe,0x87,0x54,0x01,0,0};
 static const unsigned char type_test[]={0xf6,0x42,0x38,0x40};
 int align_found=0,type_found=0;
 if(!access_ok(normal,0x200,0)||direction<v->base+0xa0){*reason="layout range";return 0;}
 for(unsigned i=0;i+sizeof(alignment)<=0x200;i++)if(!memcmp(normal+i,alignment,sizeof(alignment)))align_found=1;
 if(access_ok(direction-0xa0,0xa0,0))for(unsigned i=0;i+sizeof(type_test)<=0xa0;i++)if(!memcmp(direction-0xa0+i,type_test,sizeof(type_test)))type_found=1;
 if(!align_found||!type_found){*reason="unsupported element layout";return 0;}
 unsigned char *table=relative_target(setter+12,3,7),*getter=relative_target(reader,1,5);
 if(!in_section(v,getter,7,IMAGE_SCN_MEM_EXECUTE)||!access_ok(getter,7,0)||getter[0]!=0x8a||getter[1]!=5||getter[6]!=0xc3){*reason="invalid RTL getter";return 0;}
 unsigned char *flag=relative_target(getter,2,6);
 if(!in_section(v,flag,1,IMAGE_SCN_MEM_WRITE)||!access_ok(flag,1,1)||*flag>1){*reason="invalid RTL flag";return 0;}
 /* Decode setup registrations: lea r8,render; mov rcx,rax; mov dl,id; call setter. */
 unsigned registrations[2]={0,0};unsigned char ids[2]={0,0};unsigned char *functions[]={normal,cinematic};
 for(unsigned k=0;k<v->count;k++){
  IMAGE_SECTION_HEADER *s=v->sections+k;if(!(s->Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
  uintptr_t cursor=(uintptr_t)v->base+s->VirtualAddress,end=cursor+s->Misc.VirtualSize;
  while(cursor<end){MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void*)cursor,&m,sizeof(m)))break;uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=cursor)break;if(next>end)next=end;
   if(access_ok((void*)cursor,1,0)){
    unsigned char *p=(void*)cursor;size_t left=next-cursor;
    while(left){unsigned char *hit=memchr(p,0x4c,left);if(!hit)break;
     if((uintptr_t)hit+17<=end&&((uintptr_t)hit+17<=next||access_ok(hit,17,0))&&hit[1]==0x8d&&hit[2]==5&&hit[7]==0x48&&hit[8]==0x89&&hit[9]==0xc1&&hit[10]==0xb2&&hit[12]==0xe8&&relative_target(hit+12,1,5)==setter){
      unsigned char *f=relative_target(hit,3,7);for(int j=0;j<2;j++)if(f==functions[j]){registrations[j]++;ids[j]=hit[11];}
     }size_t advance=(size_t)(hit-p)+1;p+=advance;left-=advance;
    }
   }cursor=next;
  }
 }
 if(registrations[0]!=1||registrations[1]!=1||ids[0]==ids[1]){*reason="missing or ambiguous setup registration";return 0;}
 RtlTargets candidate={0};candidate.flag=flag;candidate.table=table;
 for(int i=0;i<2;i++){
  void **slot=(void**)(table+(unsigned)ids[i]*16+8);
  if(!in_section(v,slot,sizeof(*slot),IMAGE_SCN_MEM_WRITE)||!access_ok(slot,sizeof(*slot),1)||(*slot&&*slot!=(void*)(v->live_base+(functions[i]-v->base)))){*reason="invalid callback slot";return 0;}
  candidate.slot[i]=slot;candidate.function[i]=functions[i];candidate.ids[i]=ids[i];
 }
 *out=candidate;*reason="validated";return 1;
}
