// Minimal non-GUI platform for the official melonDS core. GPL-3.0-or-later.
#include "Platform.h"
#include "net/Net_Slirp.h"
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <cstring>
#include <filesystem>
#include <dlfcn.h>

namespace melonDS::Platform {
struct FileHandle { FILE* file; };
std::string GetLocalFilePath(const std::string& p) { return p; }
FileHandle* OpenFile(const std::string& p, FileMode m) {
    const char* mode = "rb";
    if (m & Write) mode = (m & Append) ? "ab+" : (m & Preserve) ? "rb+" : "wb+";
    FILE* f = fopen(p.c_str(), mode);
    if (!f && (m & Write) && !(m & NoCreate)) f = fopen(p.c_str(), "wb+");
    return f ? new FileHandle{f} : nullptr;
}
FileHandle* OpenLocalFile(const std::string& p, FileMode m) { return OpenFile(p,m); }
bool FileExists(const std::string& p) { return std::filesystem::exists(p); }
bool LocalFileExists(const std::string& p) { return FileExists(p); }
bool CheckFileWritable(const std::string& p) { auto f=OpenFile(p,FileMode(ReadWrite|Preserve)); if(!f)return false; fclose(f->file);delete f;return true; }
bool CheckLocalFileWritable(const std::string& p) { return CheckFileWritable(p); }
bool CloseFile(FileHandle* f) { if(!f)return false; bool r=fclose(f->file)==0;delete f;return r; }
bool IsEndOfFile(FileHandle* f) { return feof(f->file); }
bool FileReadLine(char* b,int n,FileHandle* f) { return fgets(b,n,f->file)!=nullptr; }
u64 FilePosition(FileHandle* f) { return ftello(f->file); }
bool FileSeek(FileHandle* f,s64 o,FileSeekOrigin w) { return fseeko(f->file,o,w==FileSeekOrigin::Start?SEEK_SET:w==FileSeekOrigin::Current?SEEK_CUR:SEEK_END)==0; }
void FileRewind(FileHandle* f) { rewind(f->file); }
u64 FileRead(void* b,u64 s,u64 n,FileHandle* f) { return fread(b,s,n,f->file); }
u64 FileWrite(const void* b,u64 s,u64 n,FileHandle* f) { return fwrite(b,s,n,f->file); }
bool FileFlush(FileHandle* f) { return fflush(f->file)==0; }
u64 FileWriteFormatted(FileHandle* f,const char* fmt,...) { va_list a;va_start(a,fmt);int n=vfprintf(f->file,fmt,a);va_end(a);return n; }
u64 FileLength(FileHandle* f) { auto p=ftello(f->file);fseeko(f->file,0,SEEK_END);auto n=ftello(f->file);fseeko(f->file,p,SEEK_SET);return n; }
void Log(LogLevel l,const char* fmt,...) { if(l==Debug)return; va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);fflush(stderr); }
void SignalStop(StopReason r,void*) { fprintf(stderr,"STOP: %d\n",int(r)); }
struct Thread { std::thread thread; };
Thread* Thread_Create(std::function<void()> f) { return new Thread{std::thread(f)}; }
void Thread_Wait(Thread* t) { if(t->thread.joinable())t->thread.join(); }
void Thread_Free(Thread* t) { Thread_Wait(t);delete t; }
struct Mutex { std::mutex mutex; };
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* m) { delete m; }
void Mutex_Lock(Mutex* m) { m->mutex.lock(); }
void Mutex_Unlock(Mutex* m) { m->mutex.unlock(); }
bool Mutex_TryLock(Mutex* m) { return m->mutex.try_lock(); }
struct Semaphore { std::mutex mutex;std::condition_variable cond;int count=0; };
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore* s) { std::lock_guard<std::mutex> l(s->mutex);s->count=0; }
void Semaphore_Wait(Semaphore* s) { std::unique_lock<std::mutex> l(s->mutex);s->cond.wait(l,[s]{return s->count>0;});s->count--; }
bool Semaphore_TryWait(Semaphore* s,int ms) { std::unique_lock<std::mutex> l(s->mutex);if(!s->cond.wait_for(l,std::chrono::milliseconds(ms),[s]{return s->count>0;}))return false;s->count--;return true; }
void Semaphore_Post(Semaphore* s,int n) { std::lock_guard<std::mutex> l(s->mutex);s->count+=n;s->cond.notify_all(); }
void Sleep(u64 us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
u64 GetUSCount() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
u64 GetMSCount() { return GetUSCount()/1000; }
void WriteNDSSave(const u8*,u32,u32,u32,void*) {}
void WriteGBASave(const u8*,u32,u32,u32,void*) {}
void WriteFirmware(const Firmware&,u32,u32,void*) {}
void WriteDateTime(int,int,int,int,int,int,void*) {}
void MP_Begin(void*) {} void MP_End(void*) {}
int MP_SendPacket(u8*,int,u64,void*) { return 0; }
int MP_RecvPacket(u8*,u64*,void*) { return 0; }
int MP_SendCmd(u8*,int,u64,void*) { return 0; }
int MP_SendReply(u8*,int,u64,u16,void*) { return 0; }
int MP_SendAck(u8*,int,u64,void*) { return 0; }
int MP_RecvHostPacket(u8*,u64*,void*) { return 0; }
u16 MP_RecvReplies(u8*,u64,u16,void*) { return 0; }
static std::deque<std::vector<u8>> incoming;
static std::unique_ptr<Net_Slirp> network;
static FILE* trace=nullptr;
static void TracePacket(const u8* b,int n) {
    if(!trace)return;
    auto us=GetUSCount();
    u32 record[]={u32(us/1000000),u32(us%1000000),u32(n),u32(n)};
    fwrite(record,4,4,trace);fwrite(b,1,n,trace);fflush(trace);
}
void InitNetwork(const std::string& path) {
    trace=fopen(path.c_str(),"wb");
    u32 header[]={0xa1b2c3d4,0x00040002,0,0,65535,1};
    if(trace)fwrite(header,4,6,trace);
    network=std::make_unique<Net_Slirp>([](const u8* b,int n){ TracePacket(b,n);incoming.emplace_back(b,b+n); });
}
void EndNetwork() { network.reset();if(trace)fclose(trace);trace=nullptr; }
int Net_SendPacket(u8* b,int n,void*) { TracePacket(b,n);return network?network->SendPacket(b,n):0; }
int Net_RecvPacket(u8* b,void*) { if(!network)return 0;network->RecvCheck();if(incoming.empty())return 0;auto p=std::move(incoming.front());incoming.pop_front();memcpy(b,p.data(),p.size());return p.size(); }
void Camera_Start(int,void*) {} void Camera_Stop(int,void*) {}
void Camera_CaptureFrame(int,u32* f,int w,int h,bool,void*) { memset(f,0,w*h*4); }
void Mic_Start(void*) {} void Mic_Stop(void*) {}
int Mic_ReadInput(s16*,int,void*) { return 0; }
struct AACDecoder {}; AACDecoder* AAC_Init() { return nullptr; } void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*,int,int) { return false; }
bool AAC_DecodeFrame(AACDecoder*,const void*,int,void*,int) { return false; }
bool Addon_KeyDown(KeyType,void*) { return false; }
void Addon_RumbleStart(u32,void*) {} void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType,void*) { return 0; }
struct DynamicLibrary { void* handle; };
DynamicLibrary* DynamicLibrary_Load(const char* p) { auto h=dlopen(p,RTLD_LAZY);return h?new DynamicLibrary{h}:nullptr; }
void DynamicLibrary_Unload(DynamicLibrary* l) { dlclose(l->handle);delete l; }
void* DynamicLibrary_LoadFunction(DynamicLibrary* l,const char* n) { return dlsym(l->handle,n); }
}
