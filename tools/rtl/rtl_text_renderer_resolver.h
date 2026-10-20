static unsigned char *find_text_renderer(ImageView *v) {
 static const unsigned char bytes[]={0x48,0x8b,0x79,0x28,0xc5,0xf8,0x10,0x41,0x18,0xc5,0xf8,0x29,0x84,0x24,0xc0,0,0,0,0x4c,0x8b,0x79,0x10,0x48,0x8b,0x59,0x08,0x8b,0x29};
 static const unsigned char mask[]={255,255,255,255,255,255,255,255,255,255,255,255,255,255,0,0,0,0,255,255,255,255,255,255,255,255,255,255};
 unsigned char *hit=NULL;if(find_pattern(v,bytes,mask,sizeof(bytes),&hit)!=1)return NULL;
 IMAGE_DOS_HEADER *dos=(void*)v->base;IMAGE_NT_HEADERS64 *nt=(void*)(v->base+dos->e_lfanew);
 IMAGE_DATA_DIRECTORY directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
 if(!directory.Size||directory.Size%12||directory.VirtualAddress>=v->size||directory.Size>v->size-directory.VirtualAddress)return NULL;
 unsigned char *result=NULL;uint32_t rva=(uint32_t)(hit-v->base);
 for(unsigned off=0;off<directory.Size;off+=12){
  uint32_t range[3];memcpy(range,v->base+directory.VirtualAddress+off,12);
  if(range[0]<=rva&&rva+sizeof(bytes)<=range[1]){
   if(result||range[1]<=range[0]||range[1]-range[0]>0x10000||!in_section(v,v->base+range[0],range[1]-range[0],IMAGE_SCN_MEM_EXECUTE))return NULL;
   result=v->base+range[0];
  }
 }
 return result;
}
