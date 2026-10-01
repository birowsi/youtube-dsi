#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "hq_adpcm.h"
#include "tjpgd.h"
typedef struct {FILE *input;uint16_t *pixels;} Device;
static size_t in(JDEC *j,uint8_t *buffer,size_t n) {
    Device *d=j->device;
    if(buffer)return fread(buffer,1,n,d->input);
    return fseek(d->input,n,SEEK_CUR)==0 ? n : 0;
}
static int out(JDEC *j,void *bitmap,JRECT *rect) {
    Device *d=j->device;uint16_t *p=bitmap;
    for(unsigned y=rect->top;y<=rect->bottom;y++)
        for(unsigned x=rect->left;x<=rect->right;x++) {
            unsigned v=*p++;
            d->pixels[y*256+x]=0x8000|((v>>11)&31)|((v>>1)&0x3e0)|((v&31)<<10);
        }
    return 1;
}
int main(int argc,char **argv) {
    if(argc!=5)return 2;
    uint8_t data[2008];int16_t pcm[4000];
    FILE *f=fopen(argv[1],"rb");if(!f)return 3;
    size_t n=fread(data,1,sizeof(data),f);fclose(f);
    int decoded=hq_decode_ima(data,n,pcm,2000);
    if(decoded!=2000)return 4;
    f=fopen(argv[2],"wb");fwrite(pcm,4,decoded,f);fclose(f);
    data[2]=89;if(hq_decode_ima(data,n,pcm,2000)!=-1)return 5;
    data[2]=0;if(hq_decode_ima(data,n,pcm,1999)!=-1)return 6;
    uint8_t work[96*1024];uint16_t pixels[256*192];JDEC j;
    f=fopen(argv[3],"rb");if(!f)return 7;
    Device d={f,pixels};
    if(jd_prepare(&j,in,work,sizeof(work),&d)!=JDR_OK || j.width!=256 || j.height!=192)return 8;
    if(jd_decomp(&j,out,0)!=JDR_OK)return 9;
    fclose(f);f=fopen(argv[4],"wb");fwrite(pixels,2,256*192,f);fclose(f);
    puts("ADPCM and JPEG decoded; invalid state/capacity rejected.");return 0;
}
