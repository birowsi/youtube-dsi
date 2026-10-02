// Headless melonDS runner for a real, unmodified homebrew ROM.
#include "NDS.h"
#include "DSi.h"
#include "Args.h"
#include "FreeBIOS.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>
#include <csignal>

namespace melonDS::Platform { void InitNetwork(const std::string&); void EndNetwork(); }
using namespace melonDS;

static void capture(NDS& nds,const std::string& name) {
    void* top=nullptr;void* bottom=nullptr;
    if(!nds.GPU.GetFramebuffers(&top,&bottom))return;
    FILE* f=fopen(name.c_str(),"wb");if(!f)return;
    fwrite(top,4,256*192,f);fwrite(bottom,4,256*192,f);fclose(f);
}

int main(int argc,char** argv) {
    // A closed host TCP peer must become EPIPE, not terminate the harness.
    std::signal(SIGPIPE,SIG_IGN);
    if(argc<3) { std::cerr<<"emutest ROM output-directory [frames=3600] [ds|dsi]\n";return 1; }
    std::string rompath=argv[1],out=argv[2];
    int frames=argc>3?std::stoi(argv[3]):3600;
    bool dsi=argc<5 || std::string(argv[4])!="ds";
    std::filesystem::create_directories(out);
    std::ifstream r(rompath,std::ios::binary);
    std::vector<u8> data((std::istreambuf_iterator<char>(r)),{});
    if(data.empty())return 2;
    std::unique_ptr<NDS> nds;
    if(dsi) {
        DSiArgs args;
        args.Firmware=Firmware(1);
        args.ARM9iBIOS=std::make_unique<DSiBIOSImage>(FreeBIOSGetTwlArm9());
        args.ARM7iBIOS=std::make_unique<DSiBIOSImage>(FreeBIOSGetTwlArm7());
        args.DSPHLE=true;
        nds=std::make_unique<DSi>(std::move(args));
    } else nds=std::make_unique<NDS>(NDSArgs{});
    auto cart=NDSCart::ParseROM(data.data(),data.size());
    if(!cart)return 3;
    Platform::InitNetwork(out+"/network.pcap");
    nds->SetNDSCart(std::move(cart));
    nds->Reset();
    nds->SetupDirectBoot(std::filesystem::path(rompath).filename().string());
    // Reproduce loader constraints through public emulator APIs. The core stays
    // unmodified. These synthetic profiles are hypotheses, not hardware dumps.
    std::string profile=argc>6?argv[6]:"direct";
    if(dsi && profile!="direct") {
        auto& twl=static_cast<DSi&>(*nds);
        if(profile=="legacy-mbk" || profile=="legacy-locked")
            twl.MapNWRAMRange(1,0,0x080037C0);
        if(profile=="legacy-locked" || profile=="native-blocked") {
            // Legacy DSi entrypoints can retain NWRAM while denying SDIO/NDMA.
            twl.ARM7IOWrite32(0x04004008,0x12A03000);
        }
        std::ofstream(out+"/profile.txt")<<profile<<"\n";
    }
    nds->SetKeyMask(0xFFF);
    nds->Start();
    std::filesystem::file_time_type last{};
    FILE* audio=fopen((out+"/audio.pcm").c_str(),"wb");
    std::vector<s16> pcm(8192*2);
    auto next=std::chrono::steady_clock::now();
    std::ofstream quality(out+"/quality.tsv");
    u32 statsaddr=argc>7?std::stoul(argv[7],nullptr,16):0;
    for(int i=0;i<frames;i++) {
        // Optional controller-only schedule for repeatable Wi-Fi UI checks.
        if(argc>5 && std::string(argv[5])=="auto-connect") {
            if(i==120 || i==600) nds->SetKeyMask(0xFFE); // A
            if(i==720) nds->SetKeyMask(0xFFD); // B
            if(i==140 || i==620 || i==740) nds->SetKeyMask(0xFFF);
            if(i==480) capture(*nds,out+"/wait-before.bgra");
            if(i==900) capture(*nds,out+"/wait-after.bgra");
        }
        if(argc>5 && std::string(argv[5])=="auto-test") {
            if(i==300) nds->SetKeyMask(0xBFF); // X: full-app stream test
            if(i==320) nds->SetKeyMask(0xFFF);
            if(i==720 || i==1080) capture(*nds,out+"/playing-"+std::to_string(i)+".bgra");
        }
        if(argc>5 && std::string(argv[5])=="auto-quality") {
            if(i==300)nds->SetKeyMask(0xBFF);
            if(i==320)nds->SetKeyMask(0xFFF);
            if(i==900 || i==1020)nds->SetKeyMask(0xFFE);
            if(i==910 || i==1030)nds->SetKeyMask(0xFFF);
            if(i==720 || i==1200 || i==1560 || i==1800)
                capture(*nds,out+"/quality-"+std::to_string(i)+".bgra");
        }
        if(argc>5 && std::string(argv[5])=="auto-live") {
            if(i==300 || i==1500) nds->SetKeyMask(0xFFE); // search / choose first result
            if(i==320 || i==1520) nds->SetKeyMask(0xFFF);
            std::string query="deltarune dsi demo";
            int k=(i-360)/12, sub=(i-360)%12;
            if(i>=360 && k<(int)query.size()) {
                if(sub==0) {
                    char c=query[k];int x=0,y=0;
                    std::string rows[]={"....qqwweerrttyyuuiioopp[[]]\\\\``",
                                        ".....aassddffgghhjjkkll;;''.....",
                                        "......zzxxccvvbbnnmm,,..//......"};
                    if(c==' ') { x=116;y=184; }
                    else for(int row=0;row<3;row++) {
                        auto pos=rows[row].find(c);
                        if(pos!=std::string::npos) { x=int(pos)*8+4;y=136+row*16;break; }
                    }
                    nds->TouchScreen(x,y);
                }
                if(sub==5) nds->ReleaseScreen();
            }
            if(i==600) nds->SetKeyMask(0xFF7); // START accepts query
            if(i==620) nds->SetKeyMask(0xFFF);
            if(i==590 || i==1440 || i==2400 || i==3000 || i==3600)
                capture(*nds,out+"/live-"+std::to_string(i)+".bgra");
        }
        std::string control=out+"/control.txt";
        if(std::filesystem::exists(control)) {
            auto stamp=std::filesystem::last_write_time(control);
            if(stamp!=last) {
                last=stamp;
                std::ifstream f(control);std::string line;
                while(std::getline(f,line)) {
                    std::istringstream s(line);std::string cmd;s>>cmd;
                    if(cmd=="key") { unsigned k;s>>k;nds->SetKeyMask(0xFFF^k); }
                    if(cmd=="touch") { int x,y;s>>x>>y;nds->TouchScreen(x,y); }
                    if(cmd=="release") { nds->ReleaseScreen();nds->SetKeyMask(0xFFF); }
                    if(cmd=="capture") { std::string n;s>>n;capture(*nds,out+"/"+n+".bgra"); }
                    if(cmd=="lid") { int closed;s>>closed;nds->SetLidClosed(closed!=0); }
                    if(cmd=="quit") i=frames;
                }
            }
        }
        nds->RunFrame();
        int n=nds->SPU.ReadOutput(pcm.data(),8192);
        if(n>0 && audio)fwrite(pcm.data(),4,n,audio);
        if(i%60==0) {
            if(statsaddr) {
                quality<<std::dec<<i;
                for(unsigned n=0;n<16;n++)quality<<'\t'<<nds->ARM9Read32(statsaddr+n*4);
                quality<<'\n';quality.flush();
            }
            capture(*nds,out+"/latest.bgra");
            std::ofstream(out+"/frame.txt")<<i<<"\n";
            std::ofstream state(out+"/cpu.txt");
            state<<std::hex<<"ARM9 PC "<<nds->ARM9.R[15]<<" CPSR "<<nds->ARM9.CPSR
                 <<" ARM7 PC "<<nds->ARM7.R[15]<<" CPSR "<<nds->ARM7.CPSR<<"\n";
            if(dsi) {
                auto& twl=static_cast<DSi&>(*nds);
                state<<"SCFG7 visible "<<twl.ARM7IORead32(0x04004008)
                     <<" CLK7 visible "<<twl.ARM7IORead16(0x04004004)
                     <<" SDIO status "<<twl.ARM7IORead32(0x04004A1C)<<"\n";
            }
            state<<"A7 timer0 "<<nds->ARM7IORead32(0x04000100)
                 <<" IE "<<nds->ARM7IORead32(0x04000210)
                 <<" IME "<<nds->ARM7IORead32(0x04000208)<<"\n";
            if(i%120==0) {
                FILE* mem=fopen((out+"/memory.bin").c_str(),"wb");
                if(mem) {
                    for(u32 base: {0x03000000u,0x02d00000u,0x03800000u})
                        for(u32 off=0;off<0x10000;off+=4) {
                            u32 v=nds->ARM7Read32(base+off);fwrite(&v,4,1,mem);
                        }
                    fclose(mem);
                }
            }
        }
        next+=std::chrono::microseconds(16715);
        std::this_thread::sleep_until(next);
    }
    capture(*nds,out+"/final.bgra");
    if(audio)fclose(audio);
    nds->Stop();nds.reset();Platform::EndNetwork();
    return 0;
}
