#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#pragma comment(lib,"User32.lib")

extern "C" BOOL __stdcall BJ3HasSixthTransient(void*) noexcept;
extern "C" void* __stdcall BJ3CurrentPokerBoard() noexcept;
extern "C" volatile LONG g_BJ3ModeScopeActive;
extern "C" IMAGE_DOS_HEADER __ImageBase;
extern "C" __declspec(dllexport) volatile LONG g_BJ3DeferredQueued=0,g_BJ3DeferredReplayed=0,g_BJ3DeferredCancelled=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3DeferredState[3]={}; // Board, action, posted
extern "C" __declspec(dllexport) volatile LONG g_BJ3DeferredTestArm=0,g_BJ3DeferredTestTriggered=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3DeferredAuditWritten=0;
extern "C" __declspec(dllexport) uint32_t g_BJ3DeferredWindow=0,g_BJ3DeferredNativeWndProc=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3DeferredWindowStatus=0;
namespace {
constexpr UINT replay_message=WM_APP+0x363;
HWND window=nullptr;
WNDPROC native_window=nullptr;
uintptr_t native_button=0x006B93A0u;
// 1=Options button, 2=WM_CLOSE, 3=Escape. Close has priority.
WPARAM escape_wparam=0;LPARAM escape_lparam=0;
bool InstallWindow();
bool skip_escape_tail=false;
uint32_t test_board=0,test_native_count=0,test_has_six=0;
bool Busy(void* board) {
    // Respect native save-disabled end/transition states. Actual-write guard
    // remains independent and blocks any transient serialization attempt.
    return g_BJ3ModeScopeActive && BJ3HasSixthTransient(board) &&
        *reinterpret_cast<int32_t*>(static_cast<unsigned char*>(board)+0xD18)==0;
}
bool Ready(void* board,LONG action) {
    if(Busy(board))return false;
    if(action!=2) {
        const auto bytes=static_cast<unsigned char*>(board);
        if(*reinterpret_cast<int32_t*>(bytes+0xD18)>0 || *reinterpret_cast<int32_t*>(bytes+0xD1C)>0)return false;
    }
    return true;
}
void Audit(const char* event,int status) {
    if(!g_BJ3DeferredTestTriggered)return;
    wchar_t path[1024]={};
    if(!GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),path,1024))return;
    wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;
    wcscpy_s(slash+1,1024-(slash+1-path),L"s4_request_audit.json");
    char data[512]={};
    sprintf_s(data,"{\"event\":\"%s\",\"status\":%d,\"queued\":%ld,\"replayed\":%ld,\"cancelled\":%ld,\"board\":%lu,\"action\":%ld,\"posted\":%ld,\"trigger_board\":%lu,\"trigger_native_count\":%lu,\"trigger_has_six\":%lu}\n",event,status,g_BJ3DeferredQueued,g_BJ3DeferredReplayed,g_BJ3DeferredCancelled,static_cast<unsigned long>(g_BJ3DeferredState[0]),g_BJ3DeferredState[1],g_BJ3DeferredState[2],static_cast<unsigned long>(test_board),static_cast<unsigned long>(test_native_count),static_cast<unsigned long>(test_has_six));
    HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;
    DWORD written=0;const DWORD length=static_cast<DWORD>(strlen(data));
    if(WriteFile(f,data,length,&written,nullptr) && written==length && FlushFileBuffers(f))g_BJ3DeferredAuditWritten=1;
    CloseHandle(f);
}
void Queue(void* board,LONG action) {
    if(g_BJ3DeferredState[1] && g_BJ3DeferredState[0]==reinterpret_cast<LONG>(board)) {
        if(action==2)g_BJ3DeferredState[1]=2;
        return;
    }
    g_BJ3DeferredState[0]=reinterpret_cast<LONG>(board);
    g_BJ3DeferredState[1]=action;g_BJ3DeferredState[2]=0;
    ++g_BJ3DeferredQueued;Audit("queued",0);
}
}
extern "C" void __stdcall BJ3CancelDeferredSave() noexcept {
    skip_escape_tail=false;g_BJ3DeferredTestArm=0;
    if(g_BJ3DeferredState[1])++g_BJ3DeferredCancelled;
    g_BJ3DeferredState[0]=0;g_BJ3DeferredState[1]=0;g_BJ3DeferredState[2]=0;
}
extern "C" void __stdcall BJ3PumpDeferredSave(void* board) noexcept {
    if(g_BJ3ModeScopeActive && !native_window)InstallWindow();
    if(!g_BJ3DeferredState[1])return;
    if(g_BJ3DeferredState[0]!=reinterpret_cast<LONG>(board) || !g_BJ3ModeScopeActive){BJ3CancelDeferredSave();return;}
    if(!Ready(board,g_BJ3DeferredState[1]) || g_BJ3DeferredState[2])return;
    if(PostMessageW(window,replay_message,0,0)){g_BJ3DeferredState[2]=1;Audit("posted",0);}
}
extern "C" void __fastcall BJ3DeferredMenuButton(void* listener,void*,int id) noexcept {
    void* board=static_cast<unsigned char*>(listener)-0xB8;
    if(id==1 && board==BJ3CurrentPokerBoard() && Busy(board)){Queue(board,1);return;}
    typedef void (__thiscall *Fn)(void*,int);
    reinterpret_cast<Fn>(native_button)(listener,id);
}
static LRESULT CALLBACK DeferredWindow(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==replay_message) {
        void* board=BJ3CurrentPokerBoard();
        if(!g_BJ3DeferredState[1])return 0;
        if(!g_BJ3ModeScopeActive || reinterpret_cast<LONG>(board)!=g_BJ3DeferredState[0]){BJ3CancelDeferredSave();return 0;}
        if(!Ready(board,g_BJ3DeferredState[1])){g_BJ3DeferredState[2]=0;return 0;}
        const LONG action=g_BJ3DeferredState[1];
        g_BJ3DeferredState[0]=0;g_BJ3DeferredState[1]=0;g_BJ3DeferredState[2]=0;
        ++g_BJ3DeferredReplayed;Audit("replayed",0);
        if(action==2)return CallWindowProcW(native_window,hwnd,WM_CLOSE,0,0);
        if(action==3) {
            skip_escape_tail=false;
            PostMessageW(hwnd,WM_KEYDOWN,escape_wparam,escape_lparam);
            PostMessageW(hwnd,WM_KEYUP,escape_wparam,static_cast<LPARAM>(static_cast<uint32_t>(escape_lparam)|0xC0000000u));
            return 0;
        }
        typedef void (__thiscall *Fn)(void*,int);
        reinterpret_cast<Fn>(native_button)(static_cast<unsigned char*>(board)+0xB8,1);
        return 0;
    }
    if(message==WM_KEYDOWN && wparam!=VK_ESCAPE)skip_escape_tail=false;
    if(skip_escape_tail && ((message==WM_KEYUP && wparam==VK_ESCAPE)||(message==WM_CHAR && wparam==27)))return 0;
    if(message==WM_CLOSE || (message==WM_KEYDOWN && wparam==VK_ESCAPE)) {
        void* board=BJ3CurrentPokerBoard();
        if(board && Busy(board)) {
            if(message==WM_CLOSE)Queue(board,2);
            else {escape_wparam=wparam;escape_lparam=lparam;skip_escape_tail=true;Queue(board,3);}
            return 0;
        }
    }
    return CallWindowProcW(native_window,hwnd,message,wparam,lparam);
}
extern "C" void __stdcall BJ3OnSixthAccepted(void* board) noexcept {
    const LONG kind=InterlockedExchange(&g_BJ3DeferredTestArm,0);
    if(!kind)return;
    if(!Busy(board) || !window){g_BJ3DeferredTestTriggered=-1;return;}
    test_board=reinterpret_cast<uint32_t>(board);test_native_count=*reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(board)+0x3710);test_has_six=BJ3HasSixthTransient(board)?1:0;
    g_BJ3DeferredTestTriggered=kind;
    if(kind==1)SendMessageW(window,WM_CLOSE,0,0);
    else BJ3DeferredMenuButton(static_cast<unsigned char*>(board)+0xB8,nullptr,1);
}
extern "C" void __stdcall BJ3RecordDeferredSaveResult(int status) noexcept {Audit("save_result",status);}
static BOOL CALLBACK FindGameWindow(HWND hwnd,LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(pid==GetCurrentProcessId() && !GetWindow(hwnd,GW_OWNER) && !(GetWindowLongW(hwnd,GWL_STYLE)&WS_CHILD)) {
        if(window)return FALSE;
        window=hwnd;
    }
    return TRUE;
}
namespace {
bool InstallWindow() {
    if(native_window)return true;
    window=nullptr;EnumWindows(FindGameWindow,0);
    if(!window){g_BJ3DeferredWindowStatus=-1;return false;}
    SetLastError(0);
    native_window=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&DeferredWindow)));
    g_BJ3DeferredWindow=reinterpret_cast<uint32_t>(window);g_BJ3DeferredNativeWndProc=reinterpret_cast<uint32_t>(native_window);
    g_BJ3DeferredWindowStatus=native_window?1:-2;
    return native_window!=nullptr;
}
}
extern "C" DWORD WINAPI BJ3InstallDeferredSaveHooks(LPVOID) noexcept {
    auto slot=reinterpret_cast<uint32_t*>(0x00811C54u);
    const uint32_t replacement=reinterpret_cast<uint32_t>(&BJ3DeferredMenuButton);
    if(*slot==replacement)return 1;
    if(*slot!=native_button)return 0;
    DWORD old=0,ignored=0;if(!VirtualProtect(slot,4,PAGE_READWRITE,&old))return 0;
    *slot=replacement;
    return VirtualProtect(slot,4,old,&ignored)?1:0;
}
extern "C" __declspec(dllexport) DWORD WINAPI BJ3ArmDeferredSaveTest(LPVOID kind) noexcept {
    const uintptr_t value=reinterpret_cast<uintptr_t>(kind);
    void* board=BJ3CurrentPokerBoard();
    if((value!=1 && value!=2) || !window || !native_window || !board || !g_BJ3ModeScopeActive || Busy(board) || g_BJ3DeferredState[1])return 0;
    g_BJ3DeferredTestArm=static_cast<LONG>(value);return 1;
}
