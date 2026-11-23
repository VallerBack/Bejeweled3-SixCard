#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cwchar>
#include "game_stats_record.h"
#pragma comment(lib,"Advapi32.lib")
namespace bj3persist {
enum class Issue {Ok=0,MissingStats=1,CorruptStats=2,UnsupportedStats=3,WrongNativeSave=4,InvalidStats=5,IoError=6,NoNativeSave=7,StateMismatch=8,NativeReadFailed=9,BackupFailed=10};
inline const wchar_t* Description(Issue i) {
    switch(i){
    case Issue::MissingStats:return L"配套统计文件不存在";
    case Issue::CorruptStats:return L"配套统计文件损坏或不完整";
    case Issue::UnsupportedStats:return L"配套统计格式或版本不支持";
    case Issue::WrongNativeSave:return L"牌局与配套统计不是同一次保存";
    case Issue::InvalidStats:return L"配套统计内容不一致";
    case Issue::StateMismatch:return L"载入后的牌局与统计状态不一致";
    case Issue::NativeReadFailed:return L"游戏无法完整读取牌局文件";
    case Issue::BackupFailed:return L"无法保留有效存档备份";
    default:return L"存档文件无法读取";
    }
}
inline int Exists(const std::wstring& path) {
    const DWORD a=GetFileAttributesW(path.c_str());
    if(a!=INVALID_FILE_ATTRIBUTES)return (a&FILE_ATTRIBUTE_DIRECTORY)?-1:1;
    const DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?0:-1;
}
inline bool Hash(const std::wstring& path,std::array<uint8_t,32>* out) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    bool ok=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE;
    if(ok)ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    unsigned char bytes[4096];DWORD n=0;
    while(ok){if(!ReadFile(f,bytes,sizeof(bytes),&n,nullptr)){ok=false;break;}if(!n)break;ok=CryptHashData(hash,bytes,n,0)!=FALSE;}
    DWORD size=32;if(ok)ok=CryptGetHashParam(hash,HP_HASHVAL,out->data(),&size,0)!=FALSE && size==32;
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);CloseHandle(f);return ok;
}
inline Issue Inspect(const std::wstring& native,const std::wstring& companion,bj3save::GameStats* output) {
    const int n=Exists(native),c=Exists(companion);
    if(n<0 || c<0)return Issue::IoError;
    if(!n)return Issue::NoNativeSave;
    if(!c)return Issue::MissingStats;
    std::array<uint8_t,32> sha={};if(!Hash(native,&sha))return Issue::IoError;
    HANDLE file=CreateFileW(companion.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return Issue::IoError;
    bj3save::Record record={};LARGE_INTEGER size={};DWORD got=0;
    const bool sized=GetFileSizeEx(file,&size)!=FALSE;
    const bool exact=sized && size.QuadPart==record.size();
    const bool read=exact && ReadFile(file,record.data(),static_cast<DWORD>(record.size()),&got,nullptr) && got==record.size();
    CloseHandle(file);
    if(!sized)return Issue::IoError;if(!exact)return Issue::CorruptStats;if(!read)return Issue::IoError;
    bj3save::GameStats state={};
    switch(bj3save::Decode(record.data(),record.size(),sha,&state)) {
    case bj3save::Status::Ok:if(output)*output=state;return Issue::Ok;
    case bj3save::Status::WrongFormat:case bj3save::Status::WrongVersion:return Issue::UnsupportedStats;
    case bj3save::Status::WrongNativeSave:return Issue::WrongNativeSave;
    case bj3save::Status::UnstableState:return Issue::InvalidStats;
    default:return Issue::CorruptStats;
    }
}
inline Issue Inspect(const std::wstring& native,bj3save::GameStats* output=nullptr){return Inspect(native,native+L".bj3stats",output);}
inline std::wstring Hex(const std::array<uint8_t,32>& bytes) {
    const wchar_t digits[]=L"0123456789abcdef";std::wstring s;
    for(auto b:bytes){s+=digits[b>>4];s+=digits[b&15];}return s;
}
inline bool Same(const std::wstring& a,const std::wstring& b) {
    std::array<uint8_t,32> x={},y={};return Hash(a,&x)&&Hash(b,&y)&&x==y;
}
inline bool Directory(const std::wstring& p){return CreateDirectoryW(p.c_str(),nullptr)||GetLastError()==ERROR_ALREADY_EXISTS;}
inline Issue BackupValidPair(const std::wstring& native,std::wstring* location=nullptr) {
    bj3save::GameStats state={};Issue issue=Inspect(native,&state);if(issue!=Issue::Ok)return issue;
    const auto slash=native.find_last_of(L"\\/");if(slash==std::wstring::npos)return Issue::BackupFailed;
    std::array<uint8_t,32> companion_hash={};if(!Hash(native+L".bj3stats",&companion_hash))return Issue::BackupFailed;
    const std::wstring parent=native.substr(0,slash)+L"\\bj3_backups";
    const std::wstring folder=parent+L"\\"+Hex(state.native_save_sha256).substr(0,16)+L"_"+Hex(companion_hash).substr(0,16);
    if(!Directory(parent)||!Directory(folder))return Issue::BackupFailed;
    const std::wstring dst=folder+L"\\poker.bj6";
    for(auto suffix:{L"",L".bj3stats"}) {
        const std::wstring source=native+suffix,target=dst+suffix;
        const int exists=Exists(target);
        if(exists<0)return Issue::BackupFailed;
        if(!exists && !CopyFileW(source.c_str(),target.c_str(),TRUE))return Issue::BackupFailed;
        if(!Same(source,target))return Issue::BackupFailed;
    }
    if(Inspect(dst)!=Issue::Ok)return Issue::BackupFailed;
    if(location)*location=folder;return Issue::Ok;
}
inline Issue PreserveOrphan(const std::wstring& native) {
    const std::wstring source=native+L".bj3stats";
    const int exists=Exists(source);if(!exists)return Issue::Ok;if(exists<0)return Issue::BackupFailed;
    std::array<uint8_t,32> hash={};if(!Hash(source,&hash))return Issue::BackupFailed;
    const auto slash=native.find_last_of(L"\\/");if(slash==std::wstring::npos)return Issue::BackupFailed;
    const auto parent=native.substr(0,slash)+L"\\bj3_backups",folder=parent+L"\\orphan_"+Hex(hash).substr(0,16);
    const auto target=folder+L"\\poker.bj6.bj3stats";
    if(!Directory(parent)||!Directory(folder))return Issue::BackupFailed;
    if(Exists(target)==0 && !CopyFileW(source.c_str(),target.c_str(),TRUE))return Issue::BackupFailed;
    return Same(source,target)?Issue::Ok:Issue::BackupFailed;
}
struct CheckResult {Issue issue;std::wstring path;unsigned valid,orphans;};
inline CheckResult CheckUsers(const std::wstring& users,bool backup) {
    CheckResult result={Issue::Ok,L"",0,0};WIN32_FIND_DATAW data={};
    HANDLE find=FindFirstFileW((users+L"\\*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE){const DWORD e=GetLastError();if(e!=ERROR_FILE_NOT_FOUND&&e!=ERROR_PATH_NOT_FOUND){result.issue=Issue::IoError;result.path=users;}return result;}
    do {
        if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L".."))continue;
        const std::wstring path=users+L"\\"+data.cFileName+L"\\poker.bj6";
        Issue issue=Inspect(path);
        if(issue==Issue::NoNativeSave){
            if(Exists(path+L".bj3stats")==1)++result.orphans;
            if(!backup || PreserveOrphan(path)==Issue::Ok)continue;
            issue=Issue::BackupFailed;
        }
        if(issue==Issue::Ok && backup)issue=BackupValidPair(path);
        if(issue!=Issue::Ok){result.issue=issue;result.path=path;break;}
        ++result.valid;
    }while(FindNextFileW(find,&data));
    const DWORD error=GetLastError();FindClose(find);
    if(result.issue==Issue::Ok && error!=ERROR_NO_MORE_FILES){result.issue=Issue::IoError;result.path=users;}
    return result;
}
inline std::wstring ErrorText(Issue issue,const std::wstring& native) {
    return std::wstring(L"六张牌存档检查失败：")+Description(issue)+L"。\n\n牌局："+native+
        L"\n统计："+native+L".bj3stats\n\n未按零统计继续，现有文件未删除。请保留这两个文件，从该用户目录的 bj3_backups 或研究项目备份中成对恢复后重试。";
}
}
