#pragma once
#include <filesystem>
#include <stdexcept>
namespace bj3profile {
namespace fs=std::filesystem;
// Fresh beta namespace; never copy original or development users/settings.
inline void PrepareClean(const fs::path& destination) {
    if(fs::exists(destination) && !fs::is_directory(destination))
        throw std::runtime_error("Beta data root is not a directory.");
    fs::create_directories(destination/L"users");
}
inline bool PrepareRegistry() {
    HKEY key=nullptr;
    const LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\PopCap\\BJ3SixBeta",0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&key,nullptr);
    if(result==ERROR_SUCCESS)RegCloseKey(key);
    return result==ERROR_SUCCESS;
}
struct IdentityChange {uintptr_t address;const char* before;const char* after;size_t size;};
inline bool ApplyIdentity(HANDLE process,const IdentityChange* changes,size_t count) {
    for(size_t i=0;i<count;++i){const auto& change=changes[i];char bytes[32]={};SIZE_T got=0;if(change.size>sizeof(bytes)||!ReadProcessMemory(process,reinterpret_cast<void*>(change.address),bytes,change.size,&got)||got!=change.size||memcmp(bytes,change.before,change.size))return false;}
    for(size_t i=0;i<count;++i) {
        const auto& change=changes[i];auto at=reinterpret_cast<void*>(change.address);DWORD old=0,ignored=0;SIZE_T done=0;
        if(!VirtualProtectEx(process,at,change.size,PAGE_READWRITE,&old))return false;
        const bool written=WriteProcessMemory(process,at,change.after,change.size,&done)&&done==change.size;
        const bool restored=VirtualProtectEx(process,at,change.size,old,&ignored)!=FALSE;
        char bytes[32]={};SIZE_T got=0;
        if(!written||!restored||!ReadProcessMemory(process,at,bytes,change.size,&got)||got!=change.size||memcmp(bytes,change.after,change.size))return false;
    }
    return true;
}
inline bool Identity(HANDLE process) {
    // Keep the signed partner.xml intact. Its original two properties must not
    // overwrite our constructor defaults; native GetString uses the defaults
    // for these absent, mod-specific property names. No signature code changes.
    const IdentityChange changes[]={{0x00808CC4,"Bejeweled3","BJ3SixBeta",11},
        {0x00808CD0,"PopCap\\Bejeweled3","PopCap\\BJ3SixBeta",18},
        {0x00818600,"ProdName","BJ6_Name",9},
        {0x00819C28,"RegistryKey","BJ6_Reg_Key",12}};
    return ApplyIdentity(process,changes,sizeof(changes)/sizeof(changes[0]));
}
}
