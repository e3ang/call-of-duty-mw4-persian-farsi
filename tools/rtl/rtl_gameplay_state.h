/* Read the client UI state referenced by the validated subtitle renderer.
 * Connection state 7 is active; a nonzero key catcher suspends gameplay layout.
 * These field meanings require an in-game menu/gameplay transition test.
 */
static int gameplay_layout_enabled(const unsigned char *state) {
 unsigned keys,connection;memcpy(&keys,state+4,4);memcpy(&connection,state+8,4);
 return connection==7&&keys==0;
}
static unsigned char *find_gameplay_state(unsigned char *base,unsigned char *renderer) {
 unsigned char code[0x60];IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
 if(!safe_read(renderer,code,sizeof(code))||!safe_read(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x100000)return NULL;
 if(!safe_read(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE)return NULL;
 static const unsigned char prefix[]={0x48,0x63,0xc1,0x48,0x69,0xc0,0xc0,0x01,0,0,0x48,0x8d,0x0d};
 static const unsigned char suffix[]={0x80,0x7c,0x08,0x2b,0x01};
 unsigned char *result=NULL;
 for(unsigned i=0;i+22<=sizeof(code);i++){
  if(memcmp(code+i,prefix,sizeof(prefix))||memcmp(code+i+17,suffix,sizeof(suffix)))continue;
  int32_t displacement;memcpy(&displacement,code+i+13,4);
  uintptr_t address=(uintptr_t)renderer+i+17+(intptr_t)displacement;
  uintptr_t start=(uintptr_t)base;
  if(address<start||address-start>nt.OptionalHeader.SizeOfImage||0x40>nt.OptionalHeader.SizeOfImage-(address-start))return NULL;
  if(result)return NULL;result=(unsigned char*)address;
 }
 return result;
}
