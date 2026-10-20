/* Runtime-observed subtitle rows contain separate body/name text elements and
 * tight background children. Reposition whole subtrees using those measured
 * bounds, leaving font metrics, colors, opacity and vertical positions intact.
 */
static volatile LONG gameplay_active;
typedef void (*TextRenderer)(void *);
static TextRenderer original_text_renderer;
static LONG geometry_changes;
typedef struct {unsigned char *pointer;unsigned char bytes[0x2a8];} ElementView;
static int element_view(unsigned char *p,ElementView *e){
 if(!p||!access_ok(p,sizeof(e->bytes),1)||!safe_read(p,e->bytes,sizeof(e->bytes)))return 0;
 e->pointer=p;return 1;
}
static unsigned char *element_link(const ElementView *e,unsigned offset){unsigned char *p;memcpy(&p,e->bytes+offset,8);return p;}
static unsigned long long element_flags(const ElementView *e){unsigned long long f;memcpy(&f,e->bytes+0x38,8);return f;}
static void element_bounds(const ElementView *e,float *r){memcpy(r,e->bytes+0x1a8,16);}
static int bounds_valid(const float *r){for(int i=0;i<4;i++)if(!isfinite(r[i])||fabsf(r[i])>32768)return 0;return r[2]>r[0]&&r[3]>r[1];}
typedef struct {unsigned char *pointer;float delta;} GeometryMove;
static int gather_geometry(unsigned char *p,float delta,GeometryMove *moves,unsigned *count,unsigned depth){
 ElementView e;if(depth>5||*count>=64||!element_view(p,&e))return 0;
 for(unsigned i=0;i<*count;i++)if(moves[i].pointer==p)return 0;
 float r[4];element_bounds(&e,r);for(int i=0;i<4;i++)if(!isfinite(r[i]))return 0;
 moves[(*count)++]=(GeometryMove){p,delta};
 unsigned char *child=element_link(&e,0x18);unsigned limit=0;
 while(child){
  ElementView c;if(++limit>24||!element_view(child,&c)||element_link(&c,0)!=p)return 0;
  if(!gather_geometry(child,delta,moves,count,depth+1))return 0;
  child=element_link(&c,0x10);
 }
 return 1;
}
static int subtitle_geometry(unsigned char *element){
 ElementView current,parent,body={0},name={0},root;
 if(!element_view(element,&current)||!(element_flags(&current)&0x40)||!element_view(element_link(&current,0),&parent))return 0;
 root=parent;
 for(unsigned i=0;i<32;i++){
  unsigned char *up=element_link(&root,0);if(!up)break;
  ElementView next;if(!element_view(up,&next))return 0;root=next;
  if(i==31)return 0;
 }
 float viewport[4],row[4];element_bounds(&root,viewport);element_bounds(&parent,row);
 if(!bounds_valid(viewport)||!bounds_valid(row))return 0;
 float screen_width=viewport[2]-viewport[0],screen_height=viewport[3]-viewport[1];
 if(screen_width<640||screen_height<360||row[1]<viewport[1]+screen_height*0.55f||row[3]>viewport[3]-screen_height*0.01f)return 0;
 unsigned char *siblings[24];unsigned siblings_count=0;unsigned char *p=element_link(&parent,0x18);
 while(p){
  ElementView s;if(siblings_count>=24||!element_view(p,&s)||element_link(&s,0)!=parent.pointer)return 0;
  siblings[siblings_count++]=p;
  float r[4];element_bounds(&s,r);
  if((element_flags(&s)&0x40)&&bounds_valid(r)&&fabsf(r[1]-row[1])<3.0f){
   unsigned alignment=s.bytes[0x154]&3;
   float width=r[2]-r[0];
   if(alignment==0&&width>screen_width*0.20f&&width<screen_width*0.65f&&element_link(&s,0x18)){
    if(body.pointer)return 0;body=s;
   }
   if(alignment==2&&width>screen_width*0.04f&&width<screen_width*0.25f&&element_link(&s,0x18)){
    if(name.pointer)return 0;name=s;
   }
  }
  p=element_link(&s,0x10);
 }
 if(siblings_count<5||!body.pointer||!name.pointer)return 0;
 float br[4],nr[4];element_bounds(&body,br);element_bounds(&name,nr);
 float bw=br[2]-br[0],nw=nr[2]-nr[0];
 if(fabsf(bw/nw-3.0f)>0.12f||fabsf(br[1]-nr[1])>3||fabsf(br[3]-nr[3])>3)return 0;
 ElementView body_background,name_background;
 if(!element_view(element_link(&body,0x18),&body_background)||!element_view(element_link(&name,0x18),&name_background))return 0;
 float bg[4],ng[4];element_bounds(&body_background,bg);element_bounds(&name_background,ng);
 if(!bounds_valid(bg)||!bounds_valid(ng)||fabsf(bg[1]-ng[1])>3)return 0;
 float text_width=bg[2]-bg[0],name_width=ng[2]-ng[0];
 if(text_width>bw+64||name_width>nw+64||name_width<1||text_width<1)return 0;
 float gap=(nr[3]-nr[1])*0.25f;
 float left=(viewport[0]+viewport[2]-text_width-name_width-gap)*0.5f;
 float body_delta=left-bg[0],name_delta=left+text_width+gap-ng[0];
 if(fabsf(body_delta)<0.25f&&fabsf(name_delta)<0.25f)return 0;
 GeometryMove moves[64];unsigned count=0;
 if(!gather_geometry(body.pointer,body_delta,moves,&count,0))return 0;
 for(unsigned i=0;i<siblings_count;i++){
  ElementView sibling;if(!element_view(siblings[i],&sibling))return 0;
  float r[4];element_bounds(&sibling,r);
  if((element_flags(&sibling)&0x40)&&(sibling.bytes[0x154]&3)==2&&fabsf((r[2]-r[0])-nw)<2&&fabsf(r[1]-nr[1])<2){
   if(!gather_geometry(sibling.pointer,name_delta,moves,&count,0))return 0;
  }
 }
 for(unsigned i=0;i<count;i++){
  unsigned char *e=moves[i].pointer;float x[4];memcpy(x,e+0x1a8,16);x[0]+=moves[i].delta;x[2]+=moves[i].delta;memcpy(e+0x1a8,x,16);
  /* Invalidate only render commands; retain the layout measurement cache. */
  e[0x27f]&=0x7f;
 }
 for(unsigned i=0;i<32;i++){
  parent.pointer[0x27f]&=0x7f;
  p=element_link(&parent,0);if(!p||!element_view(p,&parent))break;
 }
 LONG n=InterlockedIncrement(&geometry_changes);
 if(n<=12)log_message("SUBTITLE_GEOMETRY body=%p name=%p text_width=%.2f name_width=%.2f body_dx=%.2f name_dx=%.2f moved=%u\n",body.pointer,name.pointer,text_width,name_width,body_delta,name_delta,count);
 return 1;
}
static void subtitle_text_renderer(void *context){
 unsigned char *element=NULL;
 if(InterlockedCompareExchange(&gameplay_active,0,0)&&safe_read((unsigned char*)context+8,&element,sizeof(element)))subtitle_geometry(element);
 original_text_renderer(context);
}
