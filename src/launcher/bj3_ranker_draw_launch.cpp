#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <shlobj.h>
#include "../game/stats_pair_io.h"
#include "profile_isolation.h"
#pragma comment(lib,"Shell32.lib")
#pragma comment(lib,"User32.lib")

#include <algorithm>
#include <array>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace {

constexpr wchar_t kTargetSha256[] =
    L"28f033f191925a02d3f329b93ec8aeda5396625c1a981218aae3d36acff2f865";
constexpr wchar_t kDllSha256[] =
    L"13b585d94d6f0b4dc1e6f398f55dbf34c1ddfa1fc590dc56d4fbfb6b83329ca1";
std::wstring loader_path;
constexpr wchar_t kLoaderSha256[] =
    L"6d80ce2305ec25265cb08549d9d9af4c2ab2aebe16455a73f1399774b6f3664b";

std::wstring Basename(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring Directoryname(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring HashFile(const std::wstring& path) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::array<BYTE, 32> digest{};
    std::wstring result;
    DWORD digest_size = static_cast<DWORD>(digest.size());

    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
            CRYPT_VERIFYCONTEXT)) return result;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto cleanup;
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) goto cleanup;

    for (;;) {
        std::array<BYTE, 64 * 1024> block{};
        DWORD read = 0;
        if (!ReadFile(file, block.data(), static_cast<DWORD>(block.size()),
                &read, nullptr)) goto cleanup;
        if (read == 0) break;
        if (!CryptHashData(hash, block.data(), read, 0)) goto cleanup;
    }
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digest_size, 0) ||
        digest_size != digest.size()) goto cleanup;

    result.reserve(64);
    for (BYTE byte : digest) {
        constexpr wchar_t hex[] = L"0123456789abcdef";
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }

cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return result;
}

uintptr_t RemoteModuleBase(DWORD pid, const std::wstring& module_name) {
    for (int attempt = 0; attempt != 40; ++attempt) {
        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_BAD_LENGTH || error == ERROR_PARTIAL_COPY) {
                Sleep(50);
                continue;
            }
            return 0;
        }

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        uintptr_t base = 0;
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(Basename(entry.szExePath).c_str(), module_name.c_str()) == 0) {
                    base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        if (base) return base;
        Sleep(50);
    }
    return 0;
}

DWORD RemoteThreadResult(HANDLE process, uintptr_t start, void* parameter,
        DWORD timeout_ms = 15000) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(start), parameter, 0, nullptr);
    if (!thread) { std::wcerr << L"RemoteThread create failed: " << GetLastError() << L"\n"; return 0; }
    const DWORD wait = WaitForSingleObject(thread, timeout_ms);
    DWORD result = 0;
    if (wait == WAIT_OBJECT_0) GetExitCodeThread(thread, &result);
    std::wcerr << L"installer thread start=0x" << std::hex << start
        << L" wait=0x" << wait << L" exit=0x" << result << std::dec << L"\n";
    CloseHandle(thread);
    return wait == WAIT_OBJECT_0 ? result : 0;
}

bool SuspendTargetThreads(DWORD pid, std::vector<HANDLE>* suspended) {
    if (!suspended) return false;
    std::vector<DWORD> seen;
    bool quiet = false;
    for (int pass = 0; pass != 8 && !quiet; ++pass) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        bool added = false;
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != pid ||
                    std::find(seen.begin(), seen.end(), entry.th32ThreadID) != seen.end())
                    continue;
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                    FALSE, entry.th32ThreadID);
                if (!thread) {
                    if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
                    CloseHandle(snapshot);
                    return false;
                }
                if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    CloseHandle(thread);
                    CloseHandle(snapshot);
                    return false;
                }
                seen.push_back(entry.th32ThreadID);
                suspended->push_back(thread);
                added = true;
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
        quiet = !added;
    }
    return quiet && !suspended->empty();
}

bool ResumeTargetThreads(std::vector<HANDLE>* suspended) {
    if (!suspended) return false;
    bool ok = true;
    for (auto it = suspended->rbegin(); it != suspended->rend(); ++it) {
        if (ResumeThread(*it) == static_cast<DWORD>(-1)) ok = false;
        CloseHandle(*it);
    }
    suspended->clear();
    return ok;
}

void CloseThreadHandles(std::vector<HANDLE>* suspended) {
    if (!suspended) return;
    for (HANDLE thread : *suspended) CloseHandle(thread);
    suspended->clear();
}

uintptr_t RemoteExport(DWORD pid, const std::wstring& dll_path,
        const char* export_name) {
    HMODULE local = LoadLibraryW(dll_path.c_str());
    if (!local) return 0;
    FARPROC function = GetProcAddress(local, export_name);
    const uintptr_t rva = function ?
        reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(local) : 0;
    const uintptr_t remote_base = RemoteModuleBase(pid, Basename(dll_path));
    FreeLibrary(local);
    return rva && remote_base ? remote_base + rva : 0;
}

bool LoadDllWithHelper(DWORD pid, const std::wstring& dll_path) {
    if (HashFile(loader_path.c_str()) != kLoaderSha256) {
        std::wcerr << L"Pinned x86 DLL loader hash mismatch.\n";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION helper{};
    std::wstring command_line = Quote(loader_path.c_str()) + L" " +
        std::to_wstring(pid) + L" " + Quote(dll_path);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    if (!CreateProcessW(loader_path.c_str(), mutable_command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, Directoryname(loader_path.c_str()).c_str(),
            &startup, &helper)) {
        std::wcerr << L"x86 loader helper launch failed: " << GetLastError() << L"\n";
        return false;
    }
    const DWORD wait = WaitForSingleObject(helper.hProcess, 20000);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(helper.hProcess, &exit_code);
    else TerminateProcess(helper.hProcess, 10);
    CloseHandle(helper.hThread);
    CloseHandle(helper.hProcess);
    if (wait != WAIT_OBJECT_0 || exit_code != 0) {
        std::wcerr << L"x86 loader helper failed: wait=" << wait
            << L" exit=" << exit_code << L"\n";
        return false;
    }
    return true;
}

bool InstallHooks(HANDLE process, DWORD pid, const std::wstring& dll_path) {
    const uintptr_t install_reset = RemoteExport(pid, dll_path,
        "_BJ3InstallPokerBoardSidecarResetHook@4");
    const uintptr_t install_count_five = RemoteExport(pid, dll_path,
        "_BJ3InstallPokerBoardCountFiveSelectionHook@4");
    const uintptr_t install_capture = RemoteExport(pid, dll_path,
        "_BJ3InstallPokerBoardSixthCardCaptureHook@4");
    const uintptr_t install_ranker = RemoteExport(pid, dll_path,
        "_BJ3InstallUpdateRankerCallsiteHook@4");
    const uintptr_t install_stats = RemoteExport(pid, dll_path,
        "_BJ3InstallPokerEndLevelStatsDrawHook@4");
    const uintptr_t install_face_draw = RemoteExport(pid, dll_path,
        "_BJ3InstallPokerBoardFaceDrawHook@4");
    const uintptr_t enable_preview = RemoteExport(pid, dll_path,
        "_BJ3SetSixCardUiPreviewEnabled@4");
    const uintptr_t preview_telemetry = RemoteExport(pid, dll_path,
        "g_BJ3SixCardUiPreviewLastCall");
    std::wcerr << L"Hook entries sidecar_reset=0x" << std::hex
        << install_reset << L" count_five=0x" << install_count_five
        << L" card_capture=0x" << install_capture
        << L" update_ranker_score=0x" << install_ranker
        << L" stats=0x" << install_stats
        << L" face_draw=0x" << install_face_draw
        << L" preview_enable=0x" << enable_preview
        << L" preview_telemetry=0x" << preview_telemetry << std::dec << L"\n";
    if (!install_reset || !install_count_five || !install_capture ||
            !install_ranker || !install_stats || !install_face_draw ||
            !enable_preview || !preview_telemetry) {
        std::wcerr << L"Could not resolve a gameplay/visual Hook or preview export.\n";
        return false;
    }

    // Install lifecycle/capture before the ranker and scoring callsites, then
    // install rendering Hooks. All target threads are still suspended here.
    const uintptr_t begin_scope=RemoteExport(pid,dll_path,"_BJ3BeginModeScope@4");
    const uintptr_t finish_scope=RemoteExport(pid,dll_path,"_BJ3FinishModeScope@4");
    if(!begin_scope || !finish_scope || RemoteThreadResult(process,begin_scope,nullptr)!=1)return false;
    const DWORD reset_result = RemoteThreadResult(process, install_reset, nullptr);
    const DWORD count_five_result = reset_result == 1 ?
        RemoteThreadResult(process, install_count_five, nullptr) : 0;
    const DWORD capture_result = count_five_result == 1 ?
        RemoteThreadResult(process, install_capture, nullptr) : 0;
    const DWORD ranker_result = capture_result == 1 ?
        RemoteThreadResult(process, install_ranker, nullptr) : 0;
    const DWORD stats_result = ranker_result == 1 ?
        RemoteThreadResult(process, install_stats, nullptr) : 0;
    const DWORD face_draw_result = stats_result == 1 ?
        RemoteThreadResult(process, install_face_draw, nullptr) : 0;
    const DWORD preview_result = face_draw_result == 1 ?
        RemoteThreadResult(process, enable_preview, nullptr) : 0;
    std::wcerr << L"installer results sidecar_reset=" << reset_result
        << L" count_five=" << count_five_result
        << L" card_capture=" << capture_result
        << L" update_ranker_score_progression=" << ranker_result
        << L" endlevel_stats=" << stats_result
        << L" face_draw=" << face_draw_result
        << L" preview_disabled_result=" << preview_result << L"\n";
    const DWORD scope_result=preview_result==1?RemoteThreadResult(process,finish_scope,nullptr):0;
    std::wcerr << L"mode_scope_result=" << scope_result << L"\n";
    return scope_result==1 && reset_result == 1 && count_five_result == 1 &&
        capture_result == 1 && ranker_result == 1 && stats_result == 1 &&
        face_draw_result == 1 && preview_result == 1;
}

void Usage() {
    std::wcerr << L"Usage: BJ3HookLaunch --exe <Bejeweled3.exe> --dll <experiment.dll> [--hidden|--show] [--desktop <WinSta0\\name>] [--wait]\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    // Read-only diagnostics never create a game or backup, even with a game open.
    if(argc==3 && !wcscmp(argv[1],L"--check-saves")) {
        const auto result=bj3persist::CheckUsers(argv[2],false);
        std::wcout<<L"issue="<<static_cast<unsigned>(result.issue)<<L" valid="<<result.valid<<L" orphans="<<result.orphans<<L"\n";
        return result.issue==bj3persist::Issue::Ok?0:12;
    }
    if(argc==3 && !wcscmp(argv[1],L"--check-pair")) {
        const auto issue=bj3persist::Inspect(argv[2]);
        std::wcout<<L"issue="<<static_cast<unsigned>(issue)<<L"\n";
        return issue==bj3persist::Issue::Ok?0:12;
    }
    std::wstring exe_path, dll_path, desktop_path;
    bool hidden = false, show = false;
    bool wait_for_exit = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--exe" && i + 1 < argc) exe_path = argv[++i];
        else if (arg == L"--dll" && i + 1 < argc) dll_path = argv[++i];
        else if (arg == L"--hidden") hidden = true;
        else if (arg == L"--show") show = true;
        else if (arg == L"--desktop" && i + 1 < argc) desktop_path = argv[++i];
        else if (arg == L"--wait") wait_for_exit = true;
        else { Usage(); return 2; }
    }
    wchar_t self_path[32768]={};
    if(!GetModuleFileNameW(nullptr,self_path,32768))return 2;
    const std::wstring package_root=Directoryname(self_path);
    if(exe_path.empty())exe_path=package_root+L"\\game\\Bejeweled3.exe";
    if(dll_path.empty())dll_path=package_root+L"\\BJ3SixCard.dll";
    loader_path=package_root+L"\\BJ3DllLoader.exe";
    if(!hidden && !show)show=true;

    if (HashFile(exe_path) != kTargetSha256) {
        std::wcerr << L"Target SHA-256 mismatch; nothing launched.\n";
        return 3;
    }
    if (HashFile(dll_path) != kDllSha256) {
        std::wcerr << L"DLL SHA-256 mismatch; nothing launched.\n";
        return 4;
    }

    if(HashFile(Directoryname(exe_path)+L"\\properties\\partner.xml")!=L"dfe24a848bbad6096ef127c405e4beee3676e1323950ddf16d75a311326baa1d") {
        std::wcerr<<L"Independent profile configuration hash mismatch; nothing launched.\n";return 15;
    }

    // A second Bejeweled instance was observed to exit during startup.
    // Refuse before creating it; never stop a pre-existing user's process.
    HANDLE instances=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(instances==INVALID_HANDLE_VALUE)return 11;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool running=false;
    if(Process32FirstW(instances,&entry))do {
        if(_wcsicmp(entry.szExeFile,Basename(exe_path).c_str())==0){running=true;break;}
    } while(Process32NextW(instances,&entry));
    CloseHandle(instances);
    if(running){std::wcerr << L"A Bejeweled3 game is already running; close it before launching this package.\n";return 11;}

    wchar_t local_app_data[MAX_PATH]={};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local_app_data)))return 12;
    const std::wstring data_parent=std::wstring(local_app_data)+L"\\PopCap Games\\";
    try {
        bj3profile::PrepareClean(data_parent+L"BJ3SixBeta");
        if(!bj3profile::PrepareRegistry())throw std::runtime_error("Cannot initialize independent registry settings.");
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<"\n";
        MessageBoxW(nullptr,L"无法准备六张牌版的独立用户资料。原版文件未移动或删除，请保留现有目录检查。",L"六张牌用户资料",MB_OK|MB_ICONERROR);
        return 13;
    }
    const auto check=bj3persist::CheckUsers(data_parent+L"BJ3SixBeta\\users",true);
    if(check.issue!=bj3persist::Issue::Ok) {
        const auto text=bj3persist::ErrorText(check.issue,check.path);
        MessageBoxW(nullptr,text.c_str(),L"六张牌存档检查",MB_OK|MB_ICONERROR);
        std::wcerr<<L"Save companion check failed: issue="<<static_cast<unsigned>(check.issue)<<L"\n";
        return 12;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!desktop_path.empty()) startup.lpDesktop = desktop_path.data();
    if (hidden) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    } else if (show) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWNORMAL;
    }
    PROCESS_INFORMATION process_info{};
    std::wstring command_line = Quote(exe_path);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    if (!CreateProcessW(exe_path.c_str(), mutable_command.data(), nullptr, nullptr,
            FALSE, CREATE_SUSPENDED, nullptr, Directoryname(exe_path).c_str(),
            &startup, &process_info)) {
        std::wcerr << L"CreateProcess failed: " << GetLastError() << L"\n";
        return 5;
    }

    if(!bj3profile::Identity(process_info.hProcess)) {
        std::wcerr<<L"Independent profile identity check failed; game not resumed.\n";
        TerminateProcess(process_info.hProcess,14);WaitForSingleObject(process_info.hProcess,5000);
        CloseHandle(process_info.hThread);CloseHandle(process_info.hProcess);return 14;
    }
    if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
        std::wcerr << L"Initial ResumeThread failed: " << GetLastError() << L"\n";
        TerminateProcess(process_info.hProcess, 6);
        WaitForSingleObject(process_info.hProcess, 5000);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 6;
    }

    if (!LoadDllWithHelper(process_info.dwProcessId, dll_path)) {
        std::wcerr << L"DLL load failed; stopping the private game process.\n";
        TerminateProcess(process_info.hProcess, 7);
        WaitForSingleObject(process_info.hProcess, 5000);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 7;
    }

    std::vector<HANDLE> suspended_threads;
    if (!SuspendTargetThreads(process_info.dwProcessId, &suspended_threads)) {
        std::wcerr << L"Could not pause the initialized target.\n";
        TerminateProcess(process_info.hProcess, 8);
        WaitForSingleObject(process_info.hProcess, 5000);
        CloseThreadHandles(&suspended_threads);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 8;
    }

    if (!InstallHooks(process_info.hProcess, process_info.dwProcessId, dll_path)) {
        std::wcerr << L"DLL load or Hook install failed; stopping the paused test process.\n";
        TerminateProcess(process_info.hProcess, 9);
        WaitForSingleObject(process_info.hProcess, 5000);
        CloseThreadHandles(&suspended_threads);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 9;
    }

    if (!ResumeTargetThreads(&suspended_threads)) {
        std::wcerr << L"Could not resume every game thread; stopping the test process.\n";
        TerminateProcess(process_info.hProcess, 10);
        WaitForSingleObject(process_info.hProcess, 5000);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 10;
    }

    std::wcout << L"Beta1 wait-for-six Hooks installed; game resumed; PID="
        << process_info.dwProcessId << L"\n";
    if (wait_for_exit) {
        WaitForSingleObject(process_info.hProcess, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(process_info.hProcess, &exit_code);
        std::wcout << L"Game exited with code " << exit_code << L"\n";
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return 0;
}
