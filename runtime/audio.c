/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the Apache License 2.0. See LICENSE.txt and NOTICE.txt;
   any copy or derived work must keep the credit to SwitchRecomp by MZ Labs. */
/* AudioRenderer sample playback. Wire offsets follow the audren service ABI.
   Decoding and mixing run on the guest audio update thread; waveOut owns a
   bounded set of copied PCM buffers, never pointers into guest memory. */
#include "rt.h"
#include <math.h>
#ifdef _WIN32
#include <mmsystem.h>
#elif !defined(__ANDROID__)
#include <dlfcn.h>   /* SDL2 audio, loaded at run time */
#endif
#define AF 240
#define AC 6
#define AM 64
#define AR 1024
#define AV 4096
static u32 au32(const u8*p){u32 v;memcpy(&v,p,4);return v;}
static i32 ai32(const u8*p){i32 v;memcpy(&v,p,4);return v;}
static u64 au64(const u8*p){u64 v;memcpy(&v,p,8);return v;}
static float af32(const u8*p){float v;memcpy(&v,p,4);return isfinite(v)?v:0;}
static i16 as16(const u8*p){i16 v;memcpy(&v,p,2);return v;}
static int avalid(u64 addr,u64 bytes){
  if(!addr || !bytes || addr>=AS_SIZE || bytes>AS_SIZE-addr)return 0;
  while(bytes){MemBlock b;mem_query(addr,&b);if(!(b.perm&PERM_R)||!b.size||addr<b.addr||addr-b.addr>=b.size)return 0;
    u64 n=b.size-(addr-b.addr);if(n>bytes)n=bytes;addr+=n;bytes-=n;}return 1;
}
typedef struct {float *pcm;u32 frames;u8 channels,loop,slot,eos;} ABuffer;
typedef struct {
  ABuffer q[8];int nq;double position,played_fraction;u64 played;u32 consumed;
  u8 submitted[4],was_playing,format_warned; i16 h1[AC],h2[AC];
  float filter[AC][2][4];
} AVoice;
typedef struct {u8 valid,used;u32 count,dest;float volume,matrix[24][24];} AMix;
typedef struct {
  AVoice *voices;AMix mix[AM];u8 resources[AR][0x70];u8 resource_valid[AR];
  float *samples;u8 sink_inputs[AC];u32 sink_count;u64 ticks;int logged_voice,logged_routes;
  float downmix[4];
} ARenderer;
static void atrace_voice(const char*event,u32 id,AVoice*v,const u8*vi,const u8*wb){
  static int checked;static FILE*f;if(!checked){checked=1;const char*p=getenv("SWR_AUDIO_VOICE_CSV");if(p&&*p){f=fopen(p,"w");if(f)fprintf(f,"ns,event,id,played,consumed,queued,state,rate,format,address,bytes,start,end,loop,eos,context,context_bytes,first16\n");}}
  if(!f)return;
  char hex[33]={0};u64 addr=wb?au64(wb):0,bytes=wb?au64(wb+8):0;
  if(wb&&bytes>=16&&avalid(addr,16))for(int k=0;k<16;k++)sprintf(hex+k*2,"%02x",((u8*)GP(addr))[k]);
  fprintf(f,"%llu,%s,%u,%llu,%u,%d,%u,%u,%u,%llx,%llu,%d,%d,%u,%u,%llx,%llu,%s\n",(unsigned long long)plat_ns(),event,id,(unsigned long long)v->played,v->consumed,v->nq,vi[0xA],au32(vi+0xC),vi[0xB],(unsigned long long)addr,(unsigned long long)bytes,wb?ai32(wb+0x10):0,wb?ai32(wb+0x14):0,wb?wb[0x18]:0,wb?wb[0x19]:0,(unsigned long long)(wb?au64(wb+0x20):0),(unsigned long long)(wb?au64(wb+0x28):0),hex);fflush(f);
}
static void apop(AVoice*v){if(!v->nq)return;free(v->q[0].pcm);memmove(v->q,v->q+1,(v->nq-1)*sizeof(v->q[0]));v->nq--;v->consumed++;}
static void aclear(AVoice*v,int reset){while(v->nq)apop(v);v->position=0;v->played_fraction=0;if(reset)v->played=0;}
/* DSP ADPCM: each 8-byte frame has one predictor/scale byte and 14 signed
   four-bit samples. Coefficients are signed Q11; histories are saturated PCM16. */
static i16 adpcm_sample(const u8*data,u32 sample,const i16*coef,i16*h1,i16*h2){
  u32 block=sample/14,sub=sample%14;u8 header=data[block*8];u32 predictor=header>>4;
  if(predictor>=8)return 0;
  u8 packed=data[block*8+1+sub/2];int nibble=(sub&1)?packed&15:packed>>4;if(nibble>=8)nibble-=16;
  i64 value=((i64)nibble*(1u<<(header&15))*2048+(i64)coef[predictor*2]*(*h1)+(i64)coef[predictor*2+1]*(*h2)+1024)>>11;
  if(value>32767)value=32767;if(value< -32768)value= -32768;*h2=*h1;*h1=(i16)value;return (i16)value;
}
static ABuffer adecode(AVoice*v,const u8*vi,const u8*wb,int slot){
  ABuffer b={0};u64 address=au64(wb),size=au64(wb+8);i32 start=ai32(wb+0x10),end=ai32(wb+0x14);
  u32 channels=au32(vi+0x18),format=vi[0xB];
  if(start<0||end<=start||end>8000000||!channels||channels>AC||!avalid(address,size))return b;
  u32 bytes=format==1?1:format==2?2:format==3?3:format==4||format==5?4:0;
  if(format==6){if(channels!=1||(u64)((end+13)/14)*8>size)return b;}
  else if(!bytes||(u64)end*channels*bytes>size)return b;
  b.frames=(u32)(end-start);b.channels=(u8)channels;b.loop=wb[0x18];b.eos=wb[0x19];b.slot=(u8)slot;
  b.pcm=malloc((size_t)b.frames*channels*sizeof(float));if(!b.pcm){b.frames=0;return b;}
  const u8*data=GP(address);
  if(format==6){i16 coef[16]={0};u64 ep=au64(vi+0x48),esz=au64(vi+0x50);
    if(esz<32||!avalid(ep,32)){free(b.pcm);b.pcm=NULL;b.frames=0;return b;}memcpy(coef,GP(ep),32);
    i16 h1=v->h1[0],h2=v->h2[0];u64 context=au64(wb+0x20),context_size=au64(wb+0x28);
    if(context_size>=6&&avalid(context,6)){h1=as16(GP(context)+2);h2=as16(GP(context)+4);}
    for(i32 n=0;n<end;n++){i16 sample=adpcm_sample(data,(u32)n,coef,&h1,&h2);if(n>=start)b.pcm[n-start]=sample/32768.0f;}
    v->h1[0]=h1;v->h2[0]=h2;
  }else for(u32 n=0;n<b.frames;n++)for(u32 ch=0;ch<channels;ch++){
    const u8*p=data+((size_t)n+start)*channels*bytes+ch*bytes;float sample=0;
    if(format==1)sample=(i8)*p/128.0f;
    else if(format==2)sample=as16(p)/32768.0f;
    else if(format==3){i32 a=(i32)((u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16));if(a&0x800000)a-=0x1000000;sample=a/8388608.0f;}
    else if(format==4)sample=(float)(ai32(p)/2147483648.0);
    else if(format==5)sample=af32(p);
    b.pcm[n*channels+ch]=sample;
  }
  return b;
}
static void abiquad_trace(const u8*p){   /* SWR_AUDIO_BIQUAD_TRACE=1: each distinct coefficient set once, with its DC gain both ways */
  static int on=-1;static u64 seen[64];static int nseen;if(on<0)on=getenv("SWR_AUDIO_BIQUAD_TRACE")!=NULL;if(!on||nseen>=64)return;
  u64 key=0;memcpy(&key,p+2,8);key^=(u64)as16(p+10)<<48;for(int i=0;i<nseen;i++)if(seen[i]==key)return;seen[nseen++]=key;
  float b0=as16(p+2)/16384.f,b1=as16(p+4)/16384.f,b2=as16(p+6)/16384.f,a1=as16(p+8)/16384.f,a2=as16(p+10)/16384.f;
  LOG("audio biquad: b %.4f %.4f %.4f  a %.4f %.4f  DC gain %.3f (feedback added)\n",b0,b1,b2,a1,a2,(b0+b1+b2)/(1-a1-a2));}
static float abiquad(AVoice*v,const u8*vi,u32 channel,float value){
  for(int k=0;k<2;k++){const u8*p=vi+0x24+k*12;if(!p[0])continue;float*h=v->filter[channel][k];abiquad_trace(p);
    /* the audio renderer stores the feedback coefficients already negated: y = b0 x + b1 x1 + b2 x2 + a1 y1 + a2 y2
       (as Ryujinx's BiquadFilterHelper runs it). Subtracting them turned the game's underwater low-pass unstable:
       the output overflowed, the guard below zeroed it, and Samus was silent underwater instead of muffled */
    float y=(as16(p+2)*value+as16(p+4)*h[0]+as16(p+6)*h[1]+as16(p+8)*h[2]+as16(p+10)*h[3])/16384.0f;
    if(!isfinite(y))y=0;h[1]=h[0];h[0]=value;h[3]=h[2];h[2]=y;value=y;}return value;
}
#ifndef SWR_AUDIO_TEST
static void aoutput(const i16*pcm){
#ifdef _WIN32
  enum {NB=32};static HWAVEOUT device;static WAVEHDR headers[NB];static i16 samples[NB][AF*2];static u8 busy[NB];
  static int attempted;static u64 writes,dropped;static SRWLOCK lock=SRWLOCK_INIT;
  static int trace_checked;static FILE*trace;static u64 previous_ns;
  if(!trace_checked){trace_checked=1;const char*path=getenv("SWR_TEST_AUDIO_STATS");if(path&&*path){trace=fopen(path,"w");if(trace)fprintf(trace,"ns,guest_frame,dt_ms,writes,drops,queued,clipped,silent,potential_empty\n");}}
  AcquireSRWLockExclusive(&lock);
  if(!attempted){attempted=1;if(!getenv("SWR_MUTE_AUDIO")){
    WAVEFORMATEX format={0};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
    MMRESULT result=waveOutOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
    if(result!=MMSYSERR_NOERROR){device=NULL;WARN("audio: waveOutOpen failed (%u)\n",result);}
    else{for(int k=0;k<NB;k++){headers[k].lpData=(LPSTR)samples[k];headers[k].dwBufferLength=sizeof samples[k];waveOutPrepareHeader(device,&headers[k],sizeof(WAVEHDR));}
      LOG("audio: Windows output opened, stereo PCM16 at 48000 Hz\n");}}}
  if(device){int slot=-1,queued=0;for(int k=0;k<NB;k++){if(busy[k]&&(headers[k].dwFlags&WHDR_DONE))busy[k]=0;if(!busy[k]&&slot<0)slot=k;if(busy[k])queued++;}
    int potential_empty=queued==0&&writes>0;
    if(slot>=0){memcpy(samples[slot],pcm,sizeof samples[slot]);MMRESULT result=waveOutWrite(device,&headers[slot],sizeof(WAVEHDR));if(result==MMSYSERR_NOERROR){busy[slot]=1;writes++;}else WARN("audio: waveOutWrite failed (%u)\n",result);}else dropped++;
    if(trace){u64 now=plat_ns();u32 clipped=0,silent=0;for(u32 n=0;n<AF*2;n++){if(pcm[n]==32767||pcm[n]==-32768)clipped++;if(pcm[n]==0)silent++;}
      fprintf(trace,"%llu,%d,%.6f,%llu,%llu,%d,%u,%u,%d\n",(unsigned long long)now,g_frames,previous_ns?(now-previous_ns)/1e6:0,(unsigned long long)writes,(unsigned long long)dropped,queued,clipped,silent,potential_empty);previous_ns=now;if(writes%200==0)fflush(trace);}
    if(writes&&writes%2000==0)LOG("audio: %llu chunks queued, %llu overflow drops\n",(unsigned long long)writes,(unsigned long long)dropped);
  }
  ReleaseSRWLockExclusive(&lock);
#elif !defined(__ANDROID__)
  /* Linux / Steam Deck: SDL2's queued audio (PipeWire, PulseAudio or ALSA, whichever the system has), loaded at run
     time like the controllers (backend_x11.c). Same format and the same cap as the Windows path: 32 chunks of
     5 ms queued at most, a chunk past that is dropped rather than letting latency grow */
  typedef struct { int freq; u16 format; u8 channels, silence; u16 samples, padding; u32 size; void* callback; void* userdata; } SdlSpec;
  static int attempted; static u32 dev; static u64 writes, dropped;
  static int (*init_sub)(u32); static u32 (*open_dev)(const char*,int,const SdlSpec*,SdlSpec*,int);
  static void (*pause_dev)(u32,int); static int (*queue)(u32,const void*,u32); static u32 (*queued)(u32);
  if(!attempted){ attempted=1;
    if(!getenv("SWR_MUTE_AUDIO")){
      void* h=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL); if(!h) h=dlopen("libSDL2.so",RTLD_NOW|RTLD_LOCAL);
      if(h){ *(void**)&init_sub=dlsym(h,"SDL_InitSubSystem"); *(void**)&open_dev=dlsym(h,"SDL_OpenAudioDevice"); *(void**)&pause_dev=dlsym(h,"SDL_PauseAudioDevice");
        *(void**)&queue=dlsym(h,"SDL_QueueAudio"); *(void**)&queued=dlsym(h,"SDL_GetQueuedAudioSize"); }
      if(init_sub && open_dev && pause_dev && queue && queued && init_sub(0x10)==0){   /* SDL_INIT_AUDIO */
        SdlSpec want={48000,0x8010,2,0,AF,0,0,NULL,NULL}, have;   /* AUDIO_S16LSB, stereo, one chunk per period */
        memset(&have,0,sizeof have);
        dev=open_dev(NULL,0,&want,&have,0);
        if(dev){ pause_dev(dev,0); LOG("audio: SDL2 output opened, stereo PCM16 at 48000 Hz\n"); }
        else WARN("audio: SDL2 could not open an output device\n"); }
      else WARN("audio: no sound (libSDL2-2.0.so.0 not found or its audio failed to start)\n"); } }
  if(dev){
    if(queued(dev) < 32u*AF*4u){ if(queue(dev,pcm,AF*4)==0) writes++; } else dropped++;
    if(writes && writes%2000==0) LOG("audio: %llu chunks queued, %llu overflow drops\n",(unsigned long long)writes,(unsigned long long)dropped); }
#else
  (void)pcm;
#endif
}
#else
static void aoutput(const i16*pcm);
#endif
static void acapture(const i16*pcm){
  static int checked;static FILE*f;static u32 bytes;static u32 limit=48000*4*12;
  if(!checked){checked=1;const char*seconds=getenv("SWR_DUMP_AUDIO_SECONDS");if(seconds){int n=atoi(seconds);if(n>0&&n<=900)limit=48000*4*(u32)n;}const char*path=getenv("SWR_DUMP_AUDIO");if(path&&*path){f=fopen(path,"wb+");if(f){u8 header[44]={0};fwrite(header,1,44,f);}}}
  if(f&&bytes<limit){fwrite(pcm,1,AF*4,f);bytes+=AF*4;u8 h[44]={0};memcpy(h,"RIFF",4);u32 n=bytes+36;memcpy(h+4,&n,4);memcpy(h+8,"WAVEfmt ",8);n=16;memcpy(h+16,&n,4);h[20]=1;h[22]=2;n=48000;memcpy(h+24,&n,4);n=192000;memcpy(h+28,&n,4);h[32]=4;h[34]=16;memcpy(h+36,"data",4);memcpy(h+40,&bytes,4);fseek(f,0,SEEK_SET);fwrite(h,1,44,f);fseek(f,0,SEEK_END);fflush(f);if(bytes>=limit){fclose(f);f=NULL;}}
}
static void aroutes(ARenderer*r,const u32*ih,const u8*input,u64 size){
  u64 off=64ull+ih[1]+ih[2];if(off+ih[4]>size)return;
  for(u32 i=0;i<ih[4]/0x70;i++){const u8*p=input+off+i*0x70;u32 id=au32(p);if(id<AR){memcpy(r->resources[id],p,0x70);r->resource_valid[id]=1;}}
  off+=ih[4]+(u64)ih[3]+ih[5]+ih[9];if(off+ih[6]>size)return;
  u32 count=ih[6]/0x930;const u8*p=input+off;
  if(ih[6]>=0x20&&(ih[6]-0x20)%0x930==0){count=au32(p+4);if(count>(ih[6]-0x20)/0x930)return;p+=0x20;}
  for(u32 i=0;i<count;i++,p+=0x930){u32 id=au32(p+0x10);if(id>=AM)continue;AMix*m=&r->mix[id];m->valid=1;m->used=p[0xC];m->count=au32(p+8);if(m->count>24)m->count=24;m->volume=af32(p);m->dest=au32(p+0x924);memcpy(m->matrix,p+0x24,sizeof m->matrix);}
  off+=ih[6];if(off+ih[7]>size)return;
  for(u32 i=0;i<ih[7]/0x140;i++){p=input+off+i*0x140;if(p[0]==1&&p[1]){u32 count=au32(p+0x120);if(count>AC)count=AC;r->sink_count=count;memcpy(r->sink_inputs,p+0x124,count);
    const float defaults[4]={1.f,.707f,.251f,.707f};
    for(int k=0;k<4;k++)r->downmix[k]=p[0x12B]?af32(p+0x12C+k*4):defaults[k];}}
}
/* Device sink channel order: FL, FR, center, LFE, rear L, rear R.
   Coefficients come from the audren device-sink ABI (or standard defaults).
   Keep source buffers untouched; other sinks/mixes may still reference them. */
static float astereo_sample(const ARenderer*r,int n,int ch,float volume){
  extern int g_opt_audio_surround_downmix;
  if(g_opt_audio_surround_downmix&&r->sink_count==6){
    const int inputs[4]={ch,2,3,4+ch};float sum=0;
    for(int k=0;k<4;k++){u32 source=r->sink_inputs[inputs[k]];
      if(source<24)sum+=r->samples[(size_t)source*AF+n]*r->downmix[k];}
    return sum*volume;
  }
  u32 source=r->sink_count?r->sink_inputs[ch<r->sink_count?ch:0]:(u32)ch;
  if(source>=24)source=ch;
  return r->samples[(size_t)source*AF+n]*volume;
}
extern int g_opt_audio_eos_sample_count;
void audio_render(SvcObj*o,const u32*ih,const u8*input,u64 size,const u8*voices,u32 count,u8*status){
  if(!o->u[4]){ARenderer*r=calloc(1,sizeof *r);if(!r)return;r->voices=calloc(AV,sizeof(AVoice));r->samples=calloc(AM*24*AF,sizeof(float));if(!r->voices||!r->samples){free(r->voices);free(r->samples);free(r);return;}o->u[4]=(u64)(size_t)r;}
  ARenderer*r=(ARenderer*)(size_t)o->u[4];aroutes(r,ih,input,size);memset(r->samples,0,AM*24*AF*sizeof(float));u32 active=0;
  for(u32 i=0;i<count;i++){const u8*vi=voices+i*0x170;u32 id=au32(vi)&(AV-1);AVoice*v=&r->voices[id];u8 in_use=vi[9],play=vi[0xA];
    if(vi[8]){atrace_voice("new",id,v,vi,NULL);aclear(v,1);memset(v,0,sizeof *v);}
    if(!in_use){if(v->nq){atrace_voice("unused",id,v,vi,NULL);aclear(v,1);}v->was_playing=0;}
    else{
      u32 head=au32(vi+0x40)&3;
      for(u32 x=0;x<4;x++){int slot=(head+x)&3;const u8*wb=vi+0x60+slot*0x38;
        if(wb[0x1A])v->submitted[slot]=0;
        else if(au64(wb)&&au64(wb+8)&&!v->submitted[slot]){v->submitted[slot]=1;atrace_voice("submit",id,v,vi,wb);ABuffer b=adecode(v,vi,wb,slot);
          if(b.pcm&&v->nq<8)v->q[v->nq++]=b;else{free(b.pcm);v->consumed++;if(!v->format_warned){v->format_warned=1;WARN("audio: voice %u buffer rejected, format=%u channels=%u start=%d end=%d bytes=%llu\n",id,vi[0xB],au32(vi+0x18),ai32(wb+0x10),ai32(wb+0x14),(unsigned long long)au64(wb+8));}}}}
      for(u8 n=vi[0x159];n&&v->nq;n--){apop(v);v->position=0;}
      if(play==1&&v->was_playing){atrace_voice("stop",id,v,vi,NULL);aclear(v,1);memset(v->filter,0,sizeof v->filter);}
      v->was_playing=play!=1;
      if(play==0&&v->nq&&o->u[3]==0){active++;u32 rate=au32(vi+0xC);float pitch=af32(vi+0x1C),volume=af32(vi+0x20);if(!rate||rate>192000)rate=48000;if(!(pitch>0)||pitch>8)pitch=1;
        double step=(double)rate*pitch/48000.0;u32 mix=au32(vi+0x58);if(mix>=AM)mix=0;u32 nc=r->mix[mix].valid?r->mix[mix].count:2;if(nc>24)nc=24;
        if(!r->logged_voice){r->logged_voice=1;LOG("audio: first voice %u, format %u, %u channels, %u Hz, volume %.3f, mix %u, %u buffers\n",id,vi[0xB],au32(vi+0x18),rate,volume,mix,v->nq);}
        for(int n=0;n<AF&&v->nq;n++){ABuffer*b=&v->q[0];u32 at=(u32)v->position;if(at>=b->frames)at=b->frames-1;u32 next=at+1;if(next>=b->frames)next=b->loop?0:at;float fraction=(float)(v->position-at);
          for(u32 ch=0;ch<b->channels;ch++){float value=b->pcm[at*b->channels+ch]+(b->pcm[next*b->channels+ch]-b->pcm[at*b->channels+ch])*fraction;value=abiquad(v,vi,ch,value)*volume;
            u32 resource=au32(vi+0x140+ch*4);const u8*res=resource<AR&&r->resource_valid[resource]?r->resources[resource]:NULL;
            for(u32 dst=0;dst<nc;dst++){float gain=res?af32(res+4+dst*4):(b->channels==1?1.f:dst==ch?1.f:0.f);if(gain)r->samples[((size_t)mix*24+dst)*AF+n]+=value*gain;}}
          v->position+=step;v->played_fraction+=step;u64 whole=(u64)v->played_fraction;v->played+=whole;v->played_fraction-=whole;
          while(v->nq&&v->position>=v->q[0].frames){u32 frames=v->q[0].frames;v->position-=frames;if(v->q[0].loop){if(v->position>=frames)v->position=fmod(v->position,frames);break;}
            /* EOS resets the reported sample position, not the PCM cursor.
               Preserve source-sample overshoot across resampling boundaries. */
            if(g_opt_audio_eos_sample_count&&v->q[0].eos){v->played=(u64)v->position;v->played_fraction=v->position-v->played;}
            apop(v);}
        }
      }
    }
    memcpy(status+i*16,&v->played,8);memcpy(status+i*16+8,&v->consumed,4);
  }
  /* Route leaves towards the final mix in dependency order. Cycles and
     invalid destinations are ignored instead of recursively feeding audio. */
  u8 done[AM]={0};done[0]=1;
  for(int pass=0;pass<AM;pass++){int progressed=0;for(u32 id=1;id<AM;id++){AMix*m=&r->mix[id];if(done[id]||!m->valid||!m->used)continue;int child=0;for(u32 k=1;k<AM;k++)if(!done[k]&&r->mix[k].valid&&r->mix[k].used&&r->mix[k].dest==id){child=1;break;}if(child)continue;done[id]=1;progressed=1;u32 dest=m->dest;if(dest>=AM||dest==id)continue;u32 dc=r->mix[dest].valid?r->mix[dest].count:2;
    for(u32 src=0;src<m->count;src++)for(u32 d=0;d<dc;d++){float gain=m->matrix[src][d]*m->volume;if(!isfinite(gain)||!gain)continue;float*s=r->samples+((size_t)id*24+src)*AF,*out=r->samples+((size_t)dest*24+d)*AF;for(int n=0;n<AF;n++)out[n]+=s[n]*gain;}}
    if(!progressed)break;}
  if(o->u[3]==0){i16 pcm[AF*2];extern int g_opt_volume;float final_volume=(r->mix[0].valid?r->mix[0].volume:1)*(float)g_opt_volume*0.01f;   /* [Audio] volume, live */float peak=0;
    {static int checked;static FILE*csv;if(!checked){checked=1;const char*e=getenv("SWR_AUDIO_MIX_CSV");if(e&&*e){csv=fopen(e,"w");if(csv)fprintf(csv,"ns,tick,active,sink_count,input,source,rms,peak\n");}}
      if(csv&&r->ticks%40==0){for(u32 ch=0;ch<r->sink_count;ch++){u32 source=r->sink_inputs[ch];if(source>=24)continue;double sum=0;float peak=0;for(int n=0;n<AF;n++){float value=r->samples[(size_t)source*AF+n]*final_volume;sum+=(double)value*value;if(fabsf(value)>peak)peak=fabsf(value);}fprintf(csv,"%llu,%llu,%u,%u,%u,%u,%.8f,%.8f\n",(unsigned long long)plat_ns(),(unsigned long long)r->ticks,active,r->sink_count,ch,source,sqrt(sum/AF),peak);}fflush(csv);}}
    for(int n=0;n<AF;n++)for(int ch=0;ch<2;ch++){float sample=astereo_sample(r,n,ch,final_volume);if(!isfinite(sample))sample=0;float absolute=fabsf(sample);if(absolute>peak)peak=absolute;if(sample>0.9999695f)sample=0.9999695f;if(sample< -1)sample= -1;pcm[n*2+ch]=(i16)lrintf(sample*32768);}
    aoutput(pcm);acapture(pcm);if(r->ticks%1000==0)LOG("audio: %u active voices, final mix volume %.3f, peak %.4f, sink channels %u\n",active,final_volume,peak,r->sink_count);
  }r->ticks++;
}
