/* SwitchRecomp - Copyright 2026 MZ Labs. Apache-2.0; see LICENSE.txt and NOTICE.txt. */
#ifndef SWR_PAD_REMAP_H
#define SWR_PAD_REMAP_H
#include <stdlib.h>
#include <string.h>
extern char g_opt_pad_map[128];
/* Physical Switch-position bits 0..15 -> guest bits. Empty/malformed = identity.
   Read once per poll, not per button. Never remap keyboard, replay or analog axes. */
static unsigned long long swr_pad_remap(unsigned long long buttons){
  static char previous[128]; static int map[16], ready;
  if(!g_opt_pad_map[0]) return buttons;
  if(!ready || strcmp(previous,g_opt_pad_map)){
    const char*p=g_opt_pad_map;int ok=1;
    for(int i=0;i<16;i++){
      char*end;long n=strtol(p,&end,10);
      if(end==p || n< -1 || n>15 || (i<15? *end!=',' : *end!=0)){ok=0;break;}
      map[i]=(int)n;p=end+(i<15);
    }
    if(!ok)for(int i=0;i<16;i++)map[i]=i;
    memcpy(previous,g_opt_pad_map,sizeof previous);previous[127]=0;ready=1;
  }
  unsigned long long out=buttons&~65535ull;
  for(int i=0;i<16;i++)if((buttons&(1ull<<i))&&map[i]>=0)out|=1ull<<map[i];
  return out;
}
#endif
