#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <intrin.h>
#include <string.h>
#include "game_stats_record.h"
#include "stats_pair_io.h"
#pragma comment(lib,"User32.lib")
extern "C" uint32_t g_BJ3DeferredWindow;
#pragma comment(lib,"Advapi32.lib")
extern "C" BOOL __stdcall BJ3HasSixthTransient(void*) noexcept;
extern "C" DWORD WINAPI BJ3InstallDeferredSaveHooks(LPVOID) noexcept;
extern "C" void __stdcall BJ3RecordDeferredSaveResult(int) noexcept;
extern "C" __declspec(dllexport) volatile LONG g_BJ3TransientWritesBlocked=0;
extern "C" BOOL __stdcall BJ3BlockTransientWrite(void* board) noexcept {
    if(!BJ3HasSixthTransient(board))return FALSE;
    ++g_BJ3TransientWritesBlocked;return TRUE;
}
extern "C" BOOL __stdcall BJ3CopyGameStatistics(void*,bj3save::GameStats*) noexcept;
extern "C" BOOL __stdcall BJ3ImportGameStatistics(void*,const bj3save::GameStats*) noexcept;
extern "C" uint32_t __fastcall BJ3PokerBoard224RestoreHook(void*,void*,void*,uint32_t) noexcept;
extern "C" __declspec(dllexport) volatile LONG g_BJ3StatsSaveStatus=0,g_BJ3StatsLoadStatus=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3StatsSaveWrites=0,g_BJ3StatsLoadImports=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3StatsSavedHands=0,g_BJ3StatsLoadedHands=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3StatsSavedNativeCounter=0,g_BJ3StatsLoadedNativeCounter=0;
extern "C" __declspec(dllexport) wchar_t g_BJ3StatsNativePath[1024]={};
extern "C" __declspec(dllexport) volatile LONG g_BJ3StatsPairIssue=0;
static void DefaultPairReporter(const std::wstring& text) {
    MessageBoxW(reinterpret_cast<HWND>(g_BJ3DeferredWindow),text.c_str(),L"六张牌存档检查",MB_OK|MB_ICONERROR);
}
static void (*pair_reporter)(const std::wstring&)=DefaultPairReporter;
static void PairError(bj3persist::Issue issue,const wchar_t* path,bool fatal) noexcept {
    g_BJ3StatsPairIssue=static_cast<LONG>(issue);
    std::wstring text=bj3persist::ErrorText(issue,path?path:L"");
    if(fatal)text+=L"\n\n游戏将退出，以免异常状态覆盖存档。";
    else text+=L"\n\n本次未能完成成对保存。请保留当前文件和备份，退出后检查配套文件。";
    pair_reporter(text);
    if(fatal)ExitProcess(12);
}
// Status: 1 success; -1 path; -2 unstable snapshot; -3 native file hash;
// -4 companion I/O; -5 invalid/wrong companion; -6 restored state mismatch; -7 native write failed.
namespace {
static bj3save::GameStats pending={};
static bool pending_ready=false;
static wchar_t save_path[1024]={},load_path[1024]={};
bool NativePath(void* object,wchar_t* output) noexcept {
    if(!object)return false;
    const auto bytes=static_cast<uint8_t*>(object);
    const uint32_t length=*reinterpret_cast<uint32_t*>(bytes+0x14);
    const uint32_t capacity=*reinterpret_cast<uint32_t*>(bytes+0x18);
    const char* text=capacity<16 ? reinterpret_cast<char*>(bytes+4):*reinterpret_cast<char**>(bytes+4);
    if(!text || !length || length>=1024)return false;
    char narrow[1024]={};memcpy(narrow,text,length);
    wchar_t wide[1024]={};
    if(!MultiByteToWideChar(CP_ACP,0,narrow,-1,wide,1024))return false;
    const wchar_t* name=wcsrchr(wide,L'\\');name=name ? name+1:wide;
    if(_wcsicmp(name,L"poker.bj6")!=0)return false;
    const DWORD absolute_length=GetFullPathNameW(wide,1024,output,nullptr);
    if(!absolute_length || absolute_length>=1024)return false;
    wcscpy_s(g_BJ3StatsNativePath,output);return true;
}
bool HashFile(const wchar_t* path,std::array<uint8_t,32>* output) noexcept {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    bool ok=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE;
    if(ok)ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    uint8_t block[4096];DWORD got=0;
    while(ok){if(!ReadFile(file,block,sizeof(block),&got,nullptr)){ok=false;break;}if(!got)break;ok=CryptHashData(hash,block,got,0)!=FALSE;}
    DWORD size=32;if(ok)ok=CryptGetHashParam(hash,HP_HASHVAL,output->data(),&size,0)!=FALSE && size==32;
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);CloseHandle(file);return ok;
}
bool CompanionPath(const wchar_t* native,wchar_t* out) noexcept {
    return swprintf_s(out,1100,L"%s.bj3stats",native)>0;
}
int SaveRecord(const wchar_t* path,bj3save::GameStats state) noexcept {
    if(!HashFile(path,&state.native_save_sha256))return -3;
    bj3save::Record record={};if(bj3save::Encode(state,&record)!=bj3save::Status::Ok)return -2;
    wchar_t target[1100]={},temporary[1120]={};if(!CompanionPath(path,target))return -1;
    if(swprintf_s(temporary,L"%s.tmp",target)<0)return -1;
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return -4;
    DWORD written=0;const bool ok=WriteFile(file,record.data(),static_cast<DWORD>(record.size()),&written,nullptr) && written==record.size() && FlushFileBuffers(file);
    CloseHandle(file);if(!ok)return -4;
    return MoveFileExW(temporary,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) ? 1:-4;
}
int LoadRecord(const wchar_t* path,bj3save::GameStats* state) noexcept {
    std::array<uint8_t,32> hash={};if(!HashFile(path,&hash))return -3;
    wchar_t target[1100]={};if(!CompanionPath(path,target))return -1;
    HANDLE file=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return -4;
    bj3save::Record record={};DWORD got=0;LARGE_INTEGER size={};
    const bool ok=GetFileSizeEx(file,&size) && size.QuadPart==record.size() && ReadFile(file,record.data(),static_cast<DWORD>(record.size()),&got,nullptr) && got==record.size();
    CloseHandle(file);if(!ok)return -4;
    return bj3save::Decode(record.data(),record.size(),hash,state)==bj3save::Status::Ok ? 1:-5;
}
}
extern "C" BOOL __stdcall BJ3BeforeNativeStatsSave(void* board,void* name) noexcept {
    pending_ready=false;save_path[0]=0;
    if(!board || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu)return TRUE;
    g_BJ3StatsSaveStatus=0;
    if(!NativePath(name,save_path)){g_BJ3StatsSaveStatus=-1;return FALSE;}
    const auto previous=bj3persist::Inspect(save_path);
    auto backup=previous==bj3persist::Issue::Ok?bj3persist::BackupValidPair(save_path):
        previous==bj3persist::Issue::NoNativeSave?bj3persist::PreserveOrphan(save_path):previous;
    if(backup!=bj3persist::Issue::Ok) {
        g_BJ3StatsSaveStatus=-9;PairError(backup,save_path,false);return FALSE;
    }
    pending_ready=BJ3CopyGameStatistics(board,&pending)!=FALSE;
    if(!pending_ready){g_BJ3StatsSaveStatus=-2;return FALSE;}
    g_BJ3StatsPairIssue=0;return TRUE;
}
extern "C" void __stdcall BJ3AfterNativeStatsSave(uint32_t native_result) noexcept {
    if(!(native_result&255)){if(save_path[0])g_BJ3StatsSaveStatus=-7;pending_ready=false;return;}
    if(!pending_ready)return;
    g_BJ3StatsSaveStatus=SaveRecord(save_path,pending);
    if(g_BJ3StatsSaveStatus==1){++g_BJ3StatsSaveWrites;g_BJ3StatsSavedHands=0;for(auto count:pending.categories)g_BJ3StatsSavedHands+=count;g_BJ3StatsSavedNativeCounter=pending.native_hand_counter;}
    else PairError(bj3persist::Issue::IoError,save_path,false);
    BJ3RecordDeferredSaveResult(g_BJ3StatsSaveStatus);
    pending_ready=false;
}
static uintptr_t native_write=0x00468890u,native_read=0x004688C0u;
extern "C" __declspec(dllexport) __declspec(naked) void BJ3NativeStatsSaveGate() noexcept {
    __asm {
        pushfd
        pushad
        push ebp
        call BJ3BlockTransientWrite
        test eax,eax
        jz safe_write
        popad
        popfd
        mov g_BJ3StatsSaveStatus,-8
        xor eax,eax
        ret 8
    safe_write:
        popad
        popfd
        pushfd
        pushad
        mov edx,[esp+02Ch]
        push edx
        push ebp
        call BJ3BeforeNativeStatsSave
        test eax,eax
        jnz snapshot_ready
        popad
        popfd
        xor eax,eax
        ret 8
    snapshot_ready:
        popad
        popfd
        push dword ptr [esp+8]
        push dword ptr [esp+8]
        call dword ptr [native_write]
        pushfd
        pushad
        push eax
        call BJ3AfterNativeStatsSave
        cmp byte ptr [esp+01Ch],0
        je preserve_result
        cmp g_BJ3StatsSaveStatus,0
        jge preserve_result
        mov dword ptr [esp+01Ch],0
    preserve_result:
        popad
        popfd
        ret 8
    }
}
extern "C" void __stdcall BJ3CaptureNativeStatsLoadPath(void* name) noexcept {
    load_path[0]=0;g_BJ3StatsLoadStatus=NativePath(name,load_path) ? 0:-1;
}
extern "C" __declspec(dllexport) __declspec(naked) void BJ3NativeStatsReadPathGate() noexcept {
    __asm {
        pushfd
        pushad
        push edx
        call BJ3CaptureNativeStatsLoadPath
        popad
        popfd
        jmp dword ptr [native_read]
    }
}
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3StatsDurableRestore(void* board,void*,void* buffer,uint32_t flags) noexcept {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool durable=caller==0x007219C8u;
    bj3save::GameStats state={};
    if(durable) {
        const auto issue=load_path[0]?bj3persist::Inspect(load_path,&state):bj3persist::Issue::IoError;
        if(issue!=bj3persist::Issue::Ok){g_BJ3StatsLoadStatus=-5;PairError(issue,load_path,true);}
    }
    const uint32_t result=BJ3PokerBoard224RestoreHook(board,nullptr,buffer,flags);
    if(durable) {
        if(!(result&255)){g_BJ3StatsLoadStatus=-10;PairError(bj3persist::Issue::NativeReadFailed,load_path,true);}
        if(!BJ3ImportGameStatistics(board,&state)){g_BJ3StatsLoadStatus=-6;PairError(bj3persist::Issue::StateMismatch,load_path,true);}
        g_BJ3StatsPairIssue=0;g_BJ3StatsLoadStatus=1;++g_BJ3StatsLoadImports;
        g_BJ3StatsLoadedHands=0;for(auto count:state.categories)g_BJ3StatsLoadedHands+=count;
        g_BJ3StatsLoadedNativeCounter=state.native_hand_counter;
    }
    return result;
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3DedicatedSaveInstalled=0;
extern "C" DWORD WINAPI BJ3InstallGameStatsPersistence(LPVOID) noexcept {
    if(BJ3InstallDeferredSaveHooks(nullptr)!=1)return 0;
    // The on-disk original EXE is unchanged. Only a successfully installed mod
    // process selects the dedicated Poker filename for read, write and delete.
    static const char original_name[]="poker.sav", dedicated_name[]="poker.bj6";
    char* name=reinterpret_cast<char*>(0x007A9934u);
    if(memcmp(name,dedicated_name,sizeof(dedicated_name))) {
        if(memcmp(name,original_name,sizeof(original_name)))return 0;
        DWORD previous=0,ignored=0;
        if(!VirtualProtect(name,sizeof(dedicated_name),PAGE_READWRITE,&previous))return 0;
        memcpy(name,dedicated_name,sizeof(dedicated_name));
        if(!VirtualProtect(name,sizeof(dedicated_name),previous,&ignored))return 0;
    }
    g_BJ3DedicatedSaveInstalled=1;
    struct Patch{uint32_t address,old;uintptr_t replacement;bool slot;};
    const Patch patches[]={{0x007176CA,0x00468890,reinterpret_cast<uintptr_t>(&BJ3NativeStatsSaveGate),false},
        {0x00721924,0x004688C0,reinterpret_cast<uintptr_t>(&BJ3NativeStatsReadPathGate),false},
        {0x00811A40,0x00729380,reinterpret_cast<uintptr_t>(&BJ3StatsDurableRestore),true}};
    for(const auto& p:patches) {
        uint8_t bytes[5]={};const size_t size=p.slot ? 4:5;
        if(p.slot)memcpy(bytes,&p.replacement,4);
        else {bytes[0]=0xE8;const int32_t rel=static_cast<int32_t>(p.replacement-(p.address+5));memcpy(bytes+1,&rel,4);}
        auto at=reinterpret_cast<uint8_t*>(p.address);if(!memcmp(at,bytes,size))continue;
        if(p.slot){if(*reinterpret_cast<uint32_t*>(at)!=p.old)return 0;}
        else {if(at[0]!=0xE8 || p.address+5+*reinterpret_cast<int32_t*>(at+1)!=p.old)return 0;}
        DWORD old=0,ignored=0;if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return 0;
        memcpy(at,bytes,size);const BOOL restored=VirtualProtect(at,size,old,&ignored);FlushInstructionCache(GetCurrentProcess(),at,size);
        if(!restored)return 0;
    }
    return 1;
}
