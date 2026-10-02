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
#include "ui.h"

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
// The HQ2 ARM7 core (tools/prepare_hq2_arm7.py) answers with the DSi volume, 0-31.
#define FIFO_VOLUME FIFO_USER_08
static int volume_level=-1;
static unsigned volume_asked;
static void volume_poll(unsigned now) {
    while(fifoCheckValue32(FIFO_VOLUME)) {
        u32 value=fifoGetValue32(FIFO_VOLUME);
        volume_level=value<=31?(int)value:-1;
    }
    if(now-volume_asked>=250) { volume_asked=now;fifoSendValue32(FIFO_VOLUME,0); }
}
// Receiver stack: a static block painted with a pattern, so the deepest use can be
// measured on hardware, with an untouched guard zone below it to catch an overflow.
#define RX_STACK (64*1024)
#define RX_GUARD 1024
#define RX_PAINT 0xA5A5A5A5u
static uint32_t rx_stack[(RX_GUARD+RX_STACK)/4] __attribute__((aligned(8)));
static unsigned rx_peak, rx_guard_word;
static uint32_t rx_guard_value;
static void rx_stack_check(void) {
    unsigned guard=RX_GUARD/4,i;
    for(i=0;i<guard;i++) if(rx_stack[i]!=RX_PAINT) { rx_guard_word=i+1;rx_guard_value=rx_stack[i];break; }
    for(i=guard;i<sizeof(rx_stack)/4 && rx_stack[i]==RX_PAINT;i++);
    unsigned used=sizeof(rx_stack)-i*4;
    if(used>rx_peak)rx_peak=used;
}
// Watchdog (diagnostic): the VBlank IRQ checks that the playback loop still runs.
// A hang with interrupts alive (sound fades to silence) shows where each thread was.
static volatile unsigned main_beat, main_phase, rx_beat, rx_phase, rx_sub;
static volatile int watch_on;
static unsigned watch_last, watch_count, watch_stage;
static unsigned snap[12];
static void watchdog_report(int arm7_answered) {
    static char lines[10][48];
    const char *list[10];
    int n=0;
    snprintf(lines[n++],48,"HQ2 stalled - please photograph");
    snprintf(lines[n++],48,"main phase %u beat %u battery %u",snap[0],snap[1],snap[2]);
    snprintf(lines[n++],48,"rx phase %u.%u beat %u",snap[3],snap[4],snap[5]);
    snprintf(lines[n++],48,"received %u released %u",snap[6],snap[7]);
    snprintf(lines[n++],48,"audio %u active %u eof %u fail %u",snap[8],snap[9],snap[10],snap[11]);
    snprintf(lines[n++],48,"ARM7 answers: %s",arm7_answered<0?"checking...":arm7_answered?"yes":"NO");
    snprintf(lines[n++],48,"rx stack peak %u",rx_peak);
    for(int i=0;i<n;i++)list[i]=lines[i];
    ui_crash(list,n);
}
static void watchdog(void) {
    if(!watch_on || watch_stage>=2)return;
    if(main_beat!=watch_last) { watch_last=main_beat;watch_count=0;watch_stage=0;return; }
    watch_count++;
    if(watch_count==180) {
        extern volatile int ui_battery_wait;
        unsigned values[12]={main_phase,main_beat,(unsigned)ui_battery_wait,rx_phase,rx_sub,rx_beat,
                             received,released,audio_count,audio_active,eof,failed};
        memcpy(snap,values,sizeof(snap));
        // Ask the ARM7 something it answers from its FIFO handler (the volume).
        while(fifoCheckValue32(FIFO_VOLUME))fifoGetValue32(FIFO_VOLUME);
        fifoSendValue32(FIFO_VOLUME,0);
        watchdog_report(-1);
        watch_stage=1;
    } else if(watch_stage==1 && watch_count==240) {
        watchdog_report(fifoCheckValue32(FIFO_VOLUME));
        watch_stage=2;
    }
}
// Crash screen: no stdio, no heap, no FIFO (the heap may be the damaged part).
static char crash_lines[17][48];
static int crash_count;
static void crash_dump(const char *name, uint32_t address) {
    if(address<0x02000000 || address>=0x03000000) {
        snprintf(crash_lines[crash_count++],48,"%s %08lX (not RAM)",name,(unsigned long)address);return;
    }
    const uint32_t *p=(const uint32_t *)((address&~3u)-16);
    snprintf(crash_lines[crash_count++],48,"%s:",name);
    for(int row=0;row<4;row++,p+=3)
        snprintf(crash_lines[crash_count++],48,"%08lX %08lX %08lX %08lX",(unsigned long)(uintptr_t)p,
                 (unsigned long)p[0],(unsigned long)p[1],(unsigned long)p[2]);
}
void hq_crash_handler(void) {
    const uint32_t *r=(const uint32_t *)exceptionRegisters;
    // Show something at once, before formatting anything.
    { static const char *const first[]={"HQ2 crash..."}; ui_crash(first,1); }
    crash_count=0;
    snprintf(crash_lines[crash_count++],48,"HQ2 crash - please photograph");
    snprintf(crash_lines[crash_count++],48,"pc %08lX lr %08lX sp %08lX",
             (unsigned long)r[15],(unsigned long)r[14],(unsigned long)r[13]);
    for(int i=0;i<8;i+=4)
        snprintf(crash_lines[crash_count++],48,"r%d-%d %08lX %08lX %08lX %08lX",i,i+3,
                 (unsigned long)r[i],(unsigned long)r[i+1],(unsigned long)r[i+2],(unsigned long)r[i+3]);
    snprintf(crash_lines[crash_count++],48,"rx peak %u guard %u %08lX",rx_peak,rx_guard_word,(unsigned long)rx_guard_value);
    crash_dump("r4",r[4]);
    crash_dump("r0",r[0]);
    const char *lines[17];
    for(int i=0;i<crash_count;i++)lines[i]=crash_lines[i];
    ui_crash(lines,crash_count);
    while(1);
}
// Retained telemetry for reproducible emulator/hardware playback checks.
volatile uint32_t hq_stats[16];

int hq_lid_update(void) {
    static int closed;
    int lid=(keysHeld()&KEY_LID)!=0;
    if(lid!=closed) {
        closed=lid;
        if(lid)powerOff(PM_BACKLIGHT_TOP|PM_BACKLIGHT_BOTTOM);
        else powerOn(PM_BACKLIGHT_TOP|PM_BACKLIGHT_BOTTOM);
    }
    return closed;
}
void hq_video_reset(void) {
    pending_page=-1;
    dmaFillHalfWords(0x8000, (void*)0x06000000, 384*1024);
    front_page=0;
    bgSetMapBase(video_bg,0);
}
static void watchdog(void);
static void present_frame(void) {
    watchdog();
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
        rx_sub=1;
        int n=recv(socket_fd,(uint8_t*)destination+done,size-done,0);
        if(n>0) { done+=n;total_bytes+=n;last=sys_now();continue; }
        if(!n)return done ? -1 : 0;
        if(errno!=EAGAIN && errno!=EWOULDBLOCK)return -1;
        fd_set readable;FD_ZERO(&readable);FD_SET(socket_fd,&readable);
        struct timeval timeout={.tv_sec=0,.tv_usec=100000};
        // A semaphore-based socket wait wakes on packets, not just VBlank.
        rx_sub=2;
        int ready=select(socket_fd+1,&readable,NULL,NULL,&timeout);
        rx_sub=3;
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
    // Static, not on this thread's stack: lwIP runs its receive path on the caller's
    // stack, and on hardware a 10 KB local frame overflowed it into the cothread list.
    static uint8_t encoded[SAMPLES+8];
    static int16_t pcm[SAMPLES*2];
    while(!stop_reader) {
        rx_beat++;rx_phase=1;
        while(!stop_reader && (received-released>=SLOTS || audio_count>AUDIO_SIZE-PCM_BYTES))
            cothread_yield_irq(IRQ_VBLANK);
        if(stop_reader)break;
        uint32_t chunk[4];
        rx_phase=2;
        int result=read_exact(chunk,sizeof(chunk));
        if(!result) { eof=1;break; }
        if(result<0) { if(!stop_reader)failed=1;break; }
        // YDS3: jpeg_length 0 repeats the previous picture (steady-quality mode).
        if(chunk[0]!=received || (!chunk[1] && !received) || chunk[1]>JPEG_MAX ||
           chunk[2]!=sizeof(encoded) || chunk[3]>100) { failed=1;break; }
        VideoPacket *packet=&packets[received%SLOTS];
        rx_phase=3;
        if((chunk[1] && read_exact(packet->jpeg,chunk[1])<1) || (rx_phase=4,read_exact(encoded,sizeof(encoded))<1)) {
            if(!stop_reader)failed=1;
            break;
        }
        rx_phase=5;
        if(hq_decode_ima(encoded,sizeof(encoded),pcm,SAMPLES)!=SAMPLES) { failed=1;break; }
        packet->length=chunk[1];packet->quality=chunk[3];
        rx_phase=6;
        put_audio(pcm);
        __asm__ volatile("" ::: "memory");
        received++;
        // Let the renderer and lwIP run even during a large buffered burst.
        rx_phase=7;
        cothread_yield();
    }
    rx_phase=8;
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

int hq_playback(int fd,char *error,unsigned size,const uint8_t *title_bitmap,const char *title_text,
                int start,int duration,int *seek_to,int resumed) {
    *seek_to=-1;
    int seek_target=-1,scrubbing=0;unsigned last_check=0;
    unsigned arrow_since=0,arrow_last=0;
    socket_fd=fd;error_text=error;error_size=size;
    stop_reader=reader_done=eof=failed=audio_active=0;
    received=released=audio_count=audio_out=audio_in=played_samples=0;
    starvation_count=total_bytes=0;
    memset((void*)hq_stats,0,sizeof(hq_stats));hq_stats[0]=0x48513231;
    uint32_t header[9];
    if(read_exact(header,sizeof(header))!=1 || memcmp(header,"YDS3",4) ||
       header[1]!=WIDTH || header[2]!=HEIGHT || header[3]!=FPS || header[4]!=RATE ||
       header[5]!=SAMPLES || header[6]!=2 || header[7]!=1 || header[8]!=1) {
        snprintf(error_text,error_size,"HQ relay/version mismatch");return 0;
    }
    for(unsigned i=0;i<sizeof(rx_stack)/4;i++)rx_stack[i]=RX_PAINT;
    rx_guard_word=0;
    if(cothread_create_manual(receiver,NULL,rx_stack+RX_GUARD/4,RX_STACK,COTHREAD_DETACHED)<0) {
        snprintf(error_text,error_size,"Can't start stream receiver");return 0;
    }
    mm_stream stream={.sampling_rate=RATE,.buffer_length=2048,.callback=fill_audio,
        .format=MM_STREAM_16BIT_STEREO,.timer=MM_TIMER0,.manual=false};
    int started=0,paused=0,rebuffer=0;
    unsigned shown=~0u,decoded=0,dropped=0,rebuffer_count=0,decode_ms=0,decode_max=0;
    unsigned last_ui=0,last_feedback=0,last_bytes=0,net_rate=0,tail_start=0;
    unsigned late_reported=0,decode_sum=0,decode_n=0,decode_avg=0,pictures_mark=0,picture_fps=0;
    char feedback[48];unsigned feedback_size=0,feedback_sent=0;
    PlayerView view={.title_bitmap=title_bitmap,.title_text=title_text,.volume=-1,
                     .duration=duration,.seek_target=-1,.seconds=(unsigned)start};
    static int show_stats;
    int ui_mode=-1,ui_pressed=0,toggle=0;
    if(!resumed)hq_video_reset();
    watch_last=main_beat;watch_count=watch_stage=0;watch_on=1;
    while(1) {
        int drew=0;
        main_beat++;main_phase=1;
        scanKeys();unsigned now=sys_now();
        // With the lid closed, keep the sound going and skip picture decoding.
        int lid=hq_lid_update();
        volume_poll(now);
#ifdef HQ2_CRASH_TEST
        if(keysDown()&KEY_SELECT)__builtin_trap();   // emulator check of the crash screen
#endif
#ifdef HQ2_HANG_TEST
        if(keysDown()&KEY_SELECT)for(;;);   // emulator check of the watchdog
#endif
        if(now-last_check>=500) {
            last_check=now;rx_stack_check();
            if(rx_guard_word) {
                snprintf(error_text,error_size,"Receiver stack overflow: guard word %u = %08lX (peak %u bytes)",
                         rx_guard_word,(unsigned long)rx_guard_value,rx_peak);
                failed=1;break;
            }
        }
        unsigned down=keysDown(),held=keysHeld();
        touchPosition t;touchRead(&t);
        int hit=0;
        if(down&KEY_TOUCH) {
            hit=ui_player_hit(t.px,t.py);
            if(hit==1 || hit==2)ui_pressed=hit;
            if(hit==3) { show_stats=!show_stats;last_ui=0; }
            if(hit==4 && duration>0 && started)scrubbing=1;
        }
        // Touch: drag on the bar to pick a position, release to jump there.
        if(scrubbing) {
            if(held&KEY_TOUCH) {
                int at=ui_player_bar_seconds(t.px,duration);
                if(at!=seek_target) { seek_target=at;last_ui=0; }
            } else {
                scrubbing=0;
                if(seek_target>=0) { *seek_to=seek_target;break; }
            }
        }
        if(ui_pressed && !(held&KEY_TOUCH)) {
            int fire=ui_pressed;ui_pressed=0;last_ui=0;
            if(fire==2)break;
            if(fire==1)toggle=1;
        }
        // Buttons: Left/Right move a marker while the video keeps playing; A jumps, B cancels.
        if(down&KEY_B) {
            if(seek_target<0)break;
            seek_target=-1;scrubbing=0;last_ui=0;
        }
        // This loop runs several times per frame, so key repeat is timed in ms:
        // one step on press, then every 120 ms after 400 ms; 5 s steps, 15 s after 2 s held.
        unsigned arrow=held&(KEY_LEFT|KEY_RIGHT),step=0;
        if(down&(KEY_LEFT|KEY_RIGHT)) { arrow_since=arrow_last=now;step=1; }
        else if(arrow && now-arrow_since>=400 && now-arrow_last>=120) { arrow_last=now;step=1; }
        if(step && duration>0 && started && !scrubbing) {
            if(seek_target<0)seek_target=start+(int)(played_samples/RATE);
            int amount=now-arrow_since>=2000?15:5;
            seek_target+=(arrow&KEY_RIGHT)?amount:-amount;
            if(seek_target>duration-2)seek_target=duration-2;
            if(seek_target<0)seek_target=0;
            last_ui=0;
        }
        if((down&KEY_A) && seek_target>=0 && !scrubbing) { *seek_to=seek_target;break; }
        if(((down&KEY_A) || toggle) && started) {
            toggle=0;
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
        // After a seek, start with 1.5 s buffered instead of 3 s.
        unsigned target=started ? RATE*4*5/2 : resumed ? RATE*4*3/2 : RATE*4*3;
        if(!paused && !audio_active && (audio_count>=target || (eof && audio_count))) {
            audio_active=1;rebuffer=0;
            if(!started) { started=1;main_phase=2;mmStreamOpen(&stream); }
        }
        if(started && !paused && audio_active && received) {
            unsigned current=(played_samples>stream.buffer_length ?
                played_samples-stream.buffer_length : 0)/SAMPLES;
            if(eof && !audio_count)current=received-1;
            if(current>=received)current=received-1;
            if(current!=shown && lid) {
                shown=current;released=current+1;
            } else if(current!=shown) {
                // Newest real picture at or before `current`; 0-byte packets repeat.
                unsigned target=current;
                while(target!=shown && target && !packets[target%SLOTS].length)target--;
                if(packets[target%SLOTS].length && target!=shown) {
                    main_phase=3;
                    if(!draw_packet(&packets[target%SLOTS], &decode_ms)) { failed=1;hq_stats[14]++;break; }
                    if(decode_ms>decode_max)decode_max=decode_ms;
                    decode_sum+=decode_ms;decode_n++;
                    for(unsigned i=(shown==~0u?0:shown+1);i<target;i++)
                        if(packets[i%SLOTS].length)dropped++;
                    decoded++;drew=1;
                    hq_stats[11]=packets[target%SLOTS].quality;
                }
                shown=current;released=current+1;
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
            decode_avg=decode_n ? decode_sum/decode_n : decode_ms;
            unsigned late=dropped-late_reported;late_reported=dropped;
            picture_fps=last_feedback ? (decoded-pictures_mark)*1000/(now-last_feedback) : 0;
            pictures_mark=decoded;decode_sum=decode_n=0;
            // A trailing 1 asks the relay to stop sending pictures while the lid is closed.
            feedback_size=snprintf(feedback,sizeof(feedback),lid?"BUF %u %u %u %u 1\n":"BUF %u %u %u %u\n",
                                   buffer_ms,mode,decode_avg,late);
            feedback_sent=0;last_feedback=now;
        }
        if(!eof && feedback_sent<feedback_size) {
            main_phase=4;
            int n=send(fd,feedback+feedback_sent,feedback_size-feedback_sent,0);
            if(n>0)feedback_sent+=n;
        }
        if(!lid && (now-last_ui>=500 || (int)mode!=ui_mode || ui_pressed!=view.pressed || volume_level!=view.volume)) {
            if(now-last_ui>=500 && last_ui) {
                net_rate=(total_bytes-last_bytes)*1000/(now-last_ui)/1024;
                last_bytes=total_bytes;
            }
            last_ui=now;ui_mode=mode;
            view.mode=mode;view.seconds=start+played_samples/RATE;view.buffer_ms=buffer_ms;
            view.duration=duration;view.seek_target=seek_target;
            view.quality=hq_stats[11];view.picture_fps=picture_fps;view.net_kib=net_rate;
            view.decode_ms=decode_avg;view.late=dropped;view.rebuffers=rebuffer_count;
            view.gaps=starvation_count;view.pressed=ui_pressed;view.show_stats=show_stats;
            view.volume=volume_level;view.stack_kib=(rx_peak+1023)/1024;
            main_phase=5;
            ui_player(&view);
        }
        hq_stats[1]=received;hq_stats[2]=played_samples;hq_stats[3]=decoded;
        hq_stats[4]=buffer_ms;hq_stats[5]=starvation_count;hq_stats[6]=rebuffer_count;
        hq_stats[7]=decode_ms;hq_stats[8]=decode_max;hq_stats[9]=dropped;
        hq_stats[10]=total_bytes;hq_stats[12]=mode;hq_stats[13]=net_rate;
        hq_stats[15]=shown;
        main_phase=6;
        if(drew || (started && !paused && audio_active))cothread_yield();
        else cothread_yield_irq(IRQ_VBLANK);
    }
    watch_on=0;
    audio_active=0;
    if(started)mmStreamClose();
    stop_reader=1;
    while(!reader_done)cothread_yield_irq(IRQ_VBLANK);
    if(*seek_to<0)hq_video_reset();
    if(failed && !rx_guard_word) { snprintf(error_text,error_size,"HQ stream interrupted. Check relay log.");return 0; }
    if(failed)return 0;
    return 1;
}
#endif
