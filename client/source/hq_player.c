#ifdef QUALITY_STREAM
#include <nds.h>
#include <maxmod9.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include "hq_player.h"
#include "hq_adpcm.h"
#include "tjpgd.h"

#pragma GCC target ("arm")
#pragma GCC optimize ("O3")

#define WIDTH 256
#define HEIGHT 192
#define FPS 16
#define RATE 32000
#define SAMPLES 2000
#define SLOTS 96
#define JPEG_MAX 32768
#define AUDIO_SIZE (1u << 20)
#define PCM_BYTES (SAMPLES * 4)
// The installed lwIP port's monotonic millisecond clock uses Wi-Fi timer2.
extern uint32_t sys_now(void);

typedef struct { unsigned length, quality; uint8_t jpeg[JPEG_MAX]; } VideoPacket;
static VideoPacket packets[SLOTS] __attribute__((section(".twl_bss"),aligned(32)));
static uint8_t audio_ring[AUDIO_SIZE] __attribute__((section(".twl_bss"),aligned(32)));
static uint8_t jpeg_work[96*1024] __attribute__((section(".twl_bss"),aligned(32)));
static volatile unsigned received, released, audio_count, audio_out, played_samples;
static volatile unsigned stop_reader, reader_done, eof, failed, audio_active;
static unsigned audio_in, starvation_count;
static unsigned total_bytes;
static char *error_text;
static unsigned error_size;
static int socket_fd, video_bg;
static volatile int front_page, pending_page=-1;
// Retained telemetry for reproducible emulator/hardware playback checks.
volatile uint32_t hq_stats[16];

void hq_video_reset(void) {
    pending_page=-1;
    dmaFillHalfWords(0x8000, (void*)0x06000000, 384*1024);
    front_page=0;
    bgSetMapBase(video_bg,0);
}
static void present_frame(void) {
    if(pending_page>=0) {
        bgSetMapBase(video_bg,pending_page*8);
        front_page=pending_page;pending_page=-1;
    }
}
void hq_video_init(void) {
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    // Bank C belongs to the bottom console/keyboard. D is the third video page.
    vramSetBankD(VRAM_D_MAIN_BG_0x06040000);
    video_bg=bgInit(3,BgType_Bmp16,BgSize_B16_256x256,0,0);
    bgSetScale(video_bg,256,256);bgUpdate();
    irqSet(IRQ_VBLANK,present_frame);
    hq_video_reset();
}

static int read_exact(void *destination,unsigned size) {
    unsigned done=0, last=sys_now();
    while(done<size && !stop_reader) {
        int n=recv(socket_fd,(uint8_t*)destination+done,size-done,0);
        if(n>0) { done+=n;total_bytes+=n;last=sys_now();continue; }
        if(!n)return done ? -1 : 0;
        if(errno!=EAGAIN && errno!=EWOULDBLOCK)return -1;
        fd_set readable;FD_ZERO(&readable);FD_SET(socket_fd,&readable);
        struct timeval timeout={.tv_sec=0,.tv_usec=100000};
        // A semaphore-based socket wait wakes on packets, not just VBlank.
        int ready=select(socket_fd+1,&readable,NULL,NULL,&timeout);
        if(ready<0 && errno!=EINTR)return -1;
        if(sys_now()-last>30000)return -1;
    }
    return done==size ? 1 : -1;
}

static void put_audio(const int16_t *pcm) {
    unsigned first=AUDIO_SIZE-audio_in;
    if(first>PCM_BYTES)first=PCM_BYTES;
    memcpy(audio_ring+audio_in,pcm,first);
    memcpy(audio_ring,(const uint8_t*)pcm+first,PCM_BYTES-first);
    int ime=enterCriticalSection();
    audio_in=(audio_in+PCM_BYTES)&(AUDIO_SIZE-1);
    audio_count+=PCM_BYTES;
    leaveCriticalSection(ime);
}
static int receiver(void *unused) {
    (void)unused;
    uint8_t encoded[SAMPLES+8];
    int16_t pcm[SAMPLES*2];
    while(!stop_reader) {
        while(!stop_reader && (received-released>=SLOTS || audio_count>AUDIO_SIZE-PCM_BYTES))
            cothread_yield_irq(IRQ_VBLANK);
        if(stop_reader)break;
        uint32_t chunk[4];
        int result=read_exact(chunk,sizeof(chunk));
        if(!result) { eof=1;break; }
        if(result<0) { if(!stop_reader)failed=1;break; }
        if(chunk[0]!=received || !chunk[1] || chunk[1]>JPEG_MAX ||
           chunk[2]!=sizeof(encoded) || chunk[3]>100) { failed=1;break; }
        VideoPacket *packet=&packets[received%SLOTS];
        if(read_exact(packet->jpeg,chunk[1])<1 || read_exact(encoded,sizeof(encoded))<1) {
            if(!stop_reader)failed=1;
            break;
        }
        if(hq_decode_ima(encoded,sizeof(encoded),pcm,SAMPLES)!=SAMPLES) { failed=1;break; }
        packet->length=chunk[1];packet->quality=chunk[3];
        put_audio(pcm);
        __asm__ volatile("" ::: "memory");
        received++;
        // Let the renderer and lwIP run even during a large buffered burst.
        cothread_yield();
    }
    reader_done=1;
    return 0;
}

static mm_word fill_audio(mm_word length,mm_addr dest,mm_stream_formats format) {
    (void)format;
    unsigned wanted=length*4, take=audio_active ? audio_count : 0;
    if(take>wanted)take=wanted;
    take&=~3u;
    if(audio_active && take<wanted && !eof) {
        audio_active=0;starvation_count++;take=0;
    }
    unsigned first=AUDIO_SIZE-audio_out;
    if(first>take)first=take;
    memcpy(dest,audio_ring+audio_out,first);
    memcpy((uint8_t*)dest+first,audio_ring,take-first);
    if(take<wanted)memset((uint8_t*)dest+take,0,wanted-take);
    audio_out=(audio_out+take)&(AUDIO_SIZE-1);
    audio_count-=take;played_samples+=take/4;
    return length;
}

typedef struct { const uint8_t *data;unsigned size,pos,blocks;uint16_t *pixels; } JpegSource;
static size_t jpeg_input(JDEC *decoder,uint8_t *buffer,size_t size) {
    JpegSource *source=decoder->device;
    if(size>source->size-source->pos)size=source->size-source->pos;
    if(buffer)memcpy(buffer,source->data+source->pos,size);
    source->pos+=size;return size;
}
static int __attribute__((section(".itcm"))) jpeg_output(JDEC *decoder,void *bitmap,JRECT *rect) {
    JpegSource *source=decoder->device;
    const uint16_t *input=bitmap;
    for(unsigned y=rect->top;y<=rect->bottom;y++) {
        uint16_t *out=source->pixels+y*WIDTH+rect->left;
        for(unsigned x=rect->left;x<=rect->right;x++) {
            unsigned p=*input++;
            // JPEG RGB565 -> DS RGB555, opaque. VRAM requires halfword stores.
            *out++=0x8000|((p>>11)&31)|((p>>1)&0x3e0)|((p&31)<<10);
        }
    }
    // NTR TCP ACK/RX processing runs cooperatively. Service it every ~9 ms
    // instead of making the sender wait for an entire JPEG (~50 ms).
    if(++source->blocks%32==0)cothread_yield();
    return !stop_reader;
}
static int draw_packet(VideoPacket *packet, unsigned *decode_ms) {
    JDEC decoder;
    // Three pages let the next JPEG decode while the last completed image
    // awaits VBlank. No displayed or pending page is ever overwritten.
    unsigned back=0;
    int ime=enterCriticalSection();
    while((int)back==front_page || (int)back==pending_page)back++;
    leaveCriticalSection(ime);
    JpegSource source={.data=packet->jpeg,.size=packet->length,
        .pixels=(uint16_t*)(0x06000000+back*128*1024)};
    unsigned before=sys_now();
    if(jd_prepare(&decoder,jpeg_input,jpeg_work,sizeof(jpeg_work),&source)!=JDR_OK ||
       decoder.width!=WIDTH || decoder.height!=HEIGHT)return 0;
    if(jd_decomp(&decoder,jpeg_output,0)!=JDR_OK)return 0;
    *decode_ms=sys_now()-before;
    pending_page=back;
    return 1;
}

int hq_playback(int fd,char *error,unsigned size) {
    socket_fd=fd;error_text=error;error_size=size;
    stop_reader=reader_done=eof=failed=audio_active=0;
    received=released=audio_count=audio_out=audio_in=played_samples=0;
    starvation_count=total_bytes=0;
    memset((void*)hq_stats,0,sizeof(hq_stats));hq_stats[0]=0x48513231;
    uint32_t header[9];
    if(read_exact(header,sizeof(header))!=1 || memcmp(header,"YDS2",4) ||
       header[1]!=WIDTH || header[2]!=HEIGHT || header[3]!=FPS || header[4]!=RATE ||
       header[5]!=SAMPLES || header[6]!=2 || header[7]!=1 || header[8]!=1) {
        snprintf(error_text,error_size,"HQ relay/version mismatch");return 0;
    }
    if(cothread_create(receiver,NULL,24*1024,COTHREAD_DETACHED)<0) {
        snprintf(error_text,error_size,"Can't start stream receiver");return 0;
    }
    mm_stream stream={.sampling_rate=RATE,.buffer_length=2048,.callback=fill_audio,
        .format=MM_STREAM_16BIT_STEREO,.timer=MM_TIMER0,.manual=false};
    int started=0,paused=0,rebuffer=0;
    unsigned shown=~0u,decoded=0,dropped=0,rebuffer_count=0,decode_ms=0,decode_max=0;
    unsigned last_ui=0,last_feedback=0,last_bytes=0,net_rate=0,tail_start=0;
    char feedback[40];unsigned feedback_size=0,feedback_sent=0;
    consoleClear();printf("Buffering...\n256x192 / 16fps\n32kHz stereo\n\nA: pause/resume\nB: return\n");
    hq_video_reset();
    while(1) {
        int drew=0;
        scanKeys();unsigned now=sys_now();
        if(keysDown()&KEY_B)break;
        if((keysDown()&KEY_A) && started) {
            paused=!paused;
            if(paused)audio_active=0;
            else if(audio_count>=RATE*4/3 || (eof && audio_count)) {
                audio_active=1;rebuffer=0;
            } else { rebuffer=1;rebuffer_count++; }
        }
        if(failed)break;
        if(started && !paused && !audio_active && !rebuffer) { rebuffer=1;rebuffer_count++; }
        if(started && !paused && audio_active && !eof && audio_count<RATE*4/3) {
            audio_active=0;rebuffer=1;rebuffer_count++;
        }
        unsigned target=started ? RATE*4*5/2 : RATE*4*3;
        if(!paused && !audio_active && (audio_count>=target || (eof && audio_count))) {
            audio_active=1;rebuffer=0;
            if(!started) { started=1;mmStreamOpen(&stream); }
        }
        if(started && !paused && audio_active && received) {
            unsigned current=(played_samples>stream.buffer_length ?
                played_samples-stream.buffer_length : 0)/SAMPLES;
            if(eof && !audio_count)current=received-1;
            if(current>=received)current=received-1;
            if(current!=shown) {
                if(!draw_packet(&packets[current%SLOTS], &decode_ms)) { failed=1;hq_stats[14]++;break; }
                if(decode_ms>decode_max)decode_max=decode_ms;
                if(shown!=~0u && current>shown+1)dropped+=current-shown-1;
                shown=current;decoded++;released=current+1;
                drew=1;
                hq_stats[11]=packets[current%SLOTS].quality;
            }
        }
        if(eof && !received) { failed=1;break; }
        if(eof && !audio_count && !paused) {
            if(!tail_start)tail_start=now;
            if(now-tail_start>200)break;
        }
        unsigned buffer_ms=audio_count*1000/(RATE*4);
        unsigned mode=!started ? 0 : paused ? 3 : rebuffer ? 2 : 1;
        if(!eof && now-last_feedback>=1000 && feedback_sent==feedback_size) {
            feedback_size=snprintf(feedback,sizeof(feedback),"BUF %u %u %u\n",buffer_ms,mode,decode_ms);
            feedback_sent=0;last_feedback=now;
        }
        if(!eof && feedback_sent<feedback_size) {
            int n=send(fd,feedback+feedback_sent,feedback_size-feedback_sent,0);
            if(n>0)feedback_sent+=n;
        }
        if(now-last_ui>=500) {
            net_rate=(total_bytes-last_bytes)*1000/(now-last_ui)/1024;
            last_bytes=total_bytes;last_ui=now;
            printf("\x1b[0;0H%-28s\n256x192 / 16fps\n32kHz stereo\n\nA: pause/resume\nB: return\n\n%u:%02u  Buffer:%u.%us\nJPEG:%u  Net:%u KiB/s\nDecode:%ums  Skip:%u\nRebuffer:%u  Gaps:%u\n",
                mode==0?"Buffering...":mode==2?"Buffering...":mode==3?"Paused":"Playing",
                played_samples/(RATE*60),(played_samples/RATE)%60,
                buffer_ms/1000,(buffer_ms%1000)/100,(unsigned)hq_stats[11],net_rate,decode_ms,dropped,
                rebuffer_count,starvation_count);
        }
        hq_stats[1]=received;hq_stats[2]=played_samples;hq_stats[3]=decoded;
        hq_stats[4]=buffer_ms;hq_stats[5]=starvation_count;hq_stats[6]=rebuffer_count;
        hq_stats[7]=decode_ms;hq_stats[8]=decode_max;hq_stats[9]=dropped;
        hq_stats[10]=total_bytes;hq_stats[12]=mode;hq_stats[13]=net_rate;
        hq_stats[15]=shown;
        if(drew || (started && !paused && audio_active))cothread_yield();
        else cothread_yield_irq(IRQ_VBLANK);
    }
    audio_active=0;
    if(started)mmStreamClose();
    stop_reader=1;
    while(!reader_done)cothread_yield_irq(IRQ_VBLANK);
    hq_video_reset();
    if(failed) { snprintf(error_text,error_size,"HQ stream interrupted. Check relay log.");return 0; }
    return 1;
}
#endif
