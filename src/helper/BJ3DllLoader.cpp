#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

static bool FindRemoteModule(DWORD pid, const wchar_t* wanted,
                             uintptr_t* base_out, DWORD* error_out,
                             DWORD* attempts_out) {
    constexpr DWORD kMaxSnapshotAttempts = 40;
    constexpr DWORD kRetryDelayMs = 50;
    *attempts_out = 0;
    for (DWORD attempt = 1; attempt <= kMaxSnapshotAttempts; ++attempt) {
        *attempts_out = attempt;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            *error_out = error;
            if (error == ERROR_BAD_LENGTH || error == ERROR_PARTIAL_COPY) {
                Sleep(kRetryDelayMs);
                continue;
            }
            return false;
        }
        MODULEENTRY32W item = {};
        item.dwSize = sizeof(item);
        bool found = false;
        if (Module32FirstW(snapshot, &item)) {
            do {
                if (_wcsicmp(item.szModule, wanted) == 0) {
                    *base_out = reinterpret_cast<uintptr_t>(item.modBaseAddr);
                    found = true;
                    break;
                }
            } while (Module32NextW(snapshot, &item));
        }
        const DWORD error = found ? ERROR_SUCCESS : GetLastError();
        CloseHandle(snapshot);
        *error_out = error;
        if (found) return true;
        if (error == ERROR_BAD_LENGTH || error == ERROR_PARTIAL_COPY) {
            Sleep(kRetryDelayMs);
            continue;
        }
        return false;
    }
    return false;
}

static int Fail(const char* stage, DWORD error) {
    fprintf(stderr, "stage=%s winerror=%lu\n", stage, static_cast<unsigned long>(error));
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return Fail("arguments", ERROR_INVALID_PARAMETER);
    wchar_t* end = nullptr;
    unsigned long parsed_pid = wcstoul(argv[1], &end, 10);
    if (!end || *end || parsed_pid == 0 || parsed_pid > 0xFFFFFFFFul)
        return Fail("parse_pid", ERROR_INVALID_PARAMETER);
    const DWORD pid = static_cast<DWORD>(parsed_pid);
    const wchar_t* dll_path = argv[2];

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!process) return Fail("OpenProcess", GetLastError());

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
    if (!load_library) {
        DWORD error = GetLastError();
        CloseHandle(process);
        return Fail("GetProcAddress_LoadLibraryW", error);
    }
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(load_library), &owner)) {
        DWORD error = GetLastError();
        CloseHandle(process);
        return Fail("GetModuleHandleExW_owner", error);
    }
    wchar_t owner_path[MAX_PATH] = {};
    DWORD owner_chars = GetModuleFileNameW(owner, owner_path, MAX_PATH);
    if (!owner_chars || owner_chars >= MAX_PATH) {
        DWORD error = GetLastError();
        CloseHandle(process);
        return Fail("GetModuleFileNameW_owner", error);
    }
    const wchar_t* owner_name = wcsrchr(owner_path, L'\\');
    owner_name = owner_name ? owner_name + 1 : owner_path;

    uintptr_t remote_owner_base = 0;
    DWORD snapshot_error = ERROR_SUCCESS;
    DWORD snapshot_attempts = 0;
    if (!FindRemoteModule(pid, owner_name, &remote_owner_base, &snapshot_error,
            &snapshot_attempts)) {
        CloseHandle(process);
        return Fail("find_remote_LoadLibraryW_owner", snapshot_error);
    }
    const uintptr_t local_owner_base = reinterpret_cast<uintptr_t>(owner);
    const uintptr_t local_load_va = reinterpret_cast<uintptr_t>(load_library);
    if (local_load_va < local_owner_base) {
        CloseHandle(process);
        return Fail("validate_local_export_rva", ERROR_INVALID_ADDRESS);
    }
    const uintptr_t load_rva = local_load_va - local_owner_base;
    const uintptr_t remote_load_va = remote_owner_base + load_rva;
    if (remote_load_va > 0x7FFFFFFFul) {
        CloseHandle(process);
        return Fail("validate_remote_x86_loader_address", ERROR_INVALID_ADDRESS);
    }
    MEMORY_BASIC_INFORMATION loader_mbi = {};
    if (!VirtualQueryEx(process, reinterpret_cast<const void*>(remote_load_va),
            &loader_mbi, sizeof(loader_mbi)) || loader_mbi.Type != MEM_IMAGE ||
        !(loader_mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        DWORD error = GetLastError();
        CloseHandle(process);
        return Fail("validate_remote_LoadLibraryW_image_page", error ? error : ERROR_INVALID_ADDRESS);
    }

    const size_t path_bytes = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, path_bytes,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!remote_path) {
        DWORD error = GetLastError();
        CloseHandle(process);
        return Fail("VirtualAllocEx_path", error);
    }
    SIZE_T bytes_written = 0;
    if (!WriteProcessMemory(process, remote_path, dll_path, path_bytes, &bytes_written) ||
        bytes_written != path_bytes) {
        DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return Fail("WriteProcessMemory_path", error);
    }

    DWORD thread_id = 0;
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_load_va), remote_path, 0, &thread_id);
    if (!thread) {
        DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return Fail("CreateRemoteThread_LoadLibraryW", error);
    }
    const DWORD wait_result = WaitForSingleObject(thread, 15000);
    if (wait_result != WAIT_OBJECT_0) {
        const DWORD wait_error = wait_result == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
        TerminateProcess(process, 0xBAADF00D);
        WaitForSingleObject(process, 5000);
        CloseHandle(thread);
        CloseHandle(process);
        return Fail("WaitForSingleObject_remote_loader_timeout_target_terminated", wait_error);
    }
    DWORD loader_exit = 0;
    if (!GetExitCodeThread(thread, &loader_exit)) {
        DWORD error = GetLastError();
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return Fail("GetExitCodeThread_remote_loader", error);
    }
    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    if (loader_exit == 0 || loader_exit >= 0x80000000ul) {
        CloseHandle(process);
        return Fail("remote_LoadLibraryW_returned_failure", loader_exit);
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    const uintptr_t callback_va = static_cast<uintptr_t>(loader_exit) + 0x1000;
    if (!VirtualQueryEx(process, reinterpret_cast<const void*>(callback_va), &mbi, sizeof(mbi)) ||
        mbi.Type != MEM_IMAGE || !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        DWORD error = GetLastError();
        CloseHandle(process);
        return Fail("VirtualQueryEx_probe_callback", error ? error : ERROR_INVALID_ADDRESS);
    }
    printf("{\"pid\":%lu,\"snapshot_attempts\":%lu,\"owner_name\":\"%ls\","
        "\"local_owner_base\":\"0x%08lX\",\"remote_owner_base\":\"0x%08lX\","
        "\"load_rva\":\"0x%08lX\",\"remote_load_va\":\"0x%08lX\","
        "\"thread_id\":%lu,\"thread_wait\":%lu,\"loader_exit_module_base\":\"0x%08lX\","
        "\"callback_va\":\"0x%08lX\",\"callback_region_type\":\"0x%08lX\","
        "\"callback_region_protect\":\"0x%08lX\"}\n",
        pid, snapshot_attempts, owner_name,
        static_cast<unsigned long>(local_owner_base), static_cast<unsigned long>(remote_owner_base),
        static_cast<unsigned long>(load_rva), static_cast<unsigned long>(remote_load_va),
        thread_id, wait_result, loader_exit, static_cast<unsigned long>(callback_va),
        static_cast<unsigned long>(mbi.Type), static_cast<unsigned long>(mbi.Protect));
    CloseHandle(process);
    return 0;
}
