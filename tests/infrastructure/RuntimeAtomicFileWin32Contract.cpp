#undef CreateFileW
#undef WriteFile
#undef FlushFileBuffers
#undef CloseHandle
#undef MoveFileExW
#undef DeleteFileW

// W006-01: compile the unchanged production implementation with test-local
// Win32 call shims. Real files/handles are used; only selected OS results fail.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace probe {
enum class Fault { none, open, collisions, write, zeroWrite, partialWrite,
                   flush, close, replace, cleanup };
Fault fault = Fault::none;
std::vector<std::string> events;
std::filesystem::path temporary;
std::filesystem::path target;
int opens=0, writes=0, flushes=0, closes=0, replaces=0, cleanups=0;
bool validOpen=true;
HANDLE WINAPI create(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,
                     DWORD disposition,DWORD attributes,HANDLE pattern) {
    events.emplace_back("open");++opens;temporary=name;
    validOpen = validOpen && access==GENERIC_WRITE && share==0 && sa==nullptr
        && disposition==CREATE_NEW && attributes==FILE_ATTRIBUTE_NORMAL
        && pattern==nullptr && temporary.parent_path()==target.parent_path();
    if (fault==Fault::open || fault==Fault::collisions) {
        SetLastError(fault==Fault::open?ERROR_ACCESS_DENIED:ERROR_FILE_EXISTS);
        return INVALID_HANDLE_VALUE;
    }
    return ::CreateFileW(name,access,share,sa,disposition,attributes,pattern);
}
BOOL WINAPI write(HANDLE h,LPCVOID bytes,DWORD count,LPDWORD done,LPOVERLAPPED ov) {
    events.emplace_back("write");++writes;
    if (fault==Fault::write || fault==Fault::zeroWrite) {
        *done=0;SetLastError(ERROR_DISK_FULL);
        return fault==Fault::zeroWrite?TRUE:FALSE;
    }
    if (fault==Fault::partialWrite) count=std::min<DWORD>(count,8191);
    return ::WriteFile(h,bytes,count,done,ov);
}
BOOL WINAPI flush(HANDLE h) {
    events.emplace_back("flush");++flushes;
    if (fault==Fault::flush) {SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return ::FlushFileBuffers(h);
}
BOOL WINAPI close(HANDLE h) {
    events.emplace_back("close");++closes;
    const BOOL actual=::CloseHandle(h);
    if (fault==Fault::close) {SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return actual;
}
BOOL WINAPI replace(LPCWSTR oldName,LPCWSTR newName,DWORD flags) {
    events.emplace_back("replace");++replaces;
    if (flags!=(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) validOpen=false;
    if (fault==Fault::replace || fault==Fault::cleanup) {
        SetLastError(ERROR_SHARING_VIOLATION);return FALSE;
    }
    return ::MoveFileExW(oldName,newName,flags);
}
BOOL WINAPI cleanup(LPCWSTR name) {
    events.emplace_back("cleanup");++cleanups;
    if (std::filesystem::path(name)!=temporary) validOpen=false;
    if (fault==Fault::cleanup) {SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return ::DeleteFileW(name);
}
}
#include "core/io/RuntimeAtomicFile.h"

namespace {
int checks=0, failures=0;
void expect(bool value,const std::string& label) {
    ++checks;
    std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';
    if (!value) ++failures;
}
void direct(const std::filesystem::path& p,const std::string& bytes) {
    std::ofstream f(p,std::ios::binary);f<<bytes;
    if (!f) throw std::runtime_error("fixture creation failed");
}
std::string read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
void reset(probe::Fault f,const std::filesystem::path& p) {
    probe::fault=f;probe::target=p;probe::events.clear();probe::temporary.clear();
    probe::opens=probe::writes=probe::flushes=probe::closes=probe::replaces=probe::cleanups=0;
    probe::validOpen=true;
}
bool precedes(const std::string& a,const std::string& b) {
    const auto ia=std::find(probe::events.begin(),probe::events.end(),a);
    const auto ib=std::find(probe::events.begin(),probe::events.end(),b);
    return ia!=probe::events.end() && ib!=probe::events.end() && ia<ib;
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=2) {std::cerr<<"An explicit new material root is required\n";return 2;}
    const std::filesystem::path root=std::filesystem::path(argv[1]);
    if (std::filesystem::exists(root)) {std::cerr<<"Retain prior round; choose a new root\n";return 2;}
    std::filesystem::create_directories(root);
    const std::string original="protected previous target";
    const std::string payload(150003,'Q');
    int index=0;
    for (const auto fault:{probe::Fault::none,probe::Fault::open,probe::Fault::collisions,
             probe::Fault::write,probe::Fault::zeroWrite,probe::Fault::partialWrite,
             probe::Fault::flush,probe::Fault::close,probe::Fault::replace,probe::Fault::cleanup}) {
        const auto target=root/("case-"+std::to_string(index++)+".dat");
        direct(target,original);reset(fault,target);
        int writers=0;std::string error="previous diagnostic";
        const bool result=runtimeWriteFileAtomically(target,[&](std::ostream& s,std::string&) {
            ++writers;probe::events.emplace_back("writer");
            expect(read(target)==original,"target unchanged inside writer");
            s.write(payload.data(),static_cast<std::streamsize>(payload.size()));return true;
        },error);
        const bool pass=fault==probe::Fault::none || fault==probe::Fault::partialWrite;
        expect(result==pass,"exact success/failure classification");
        expect(read(target)==(pass?payload:original),"complete publication or exact original retained");
        expect(probe::validOpen,"same-directory exclusive CREATE_NEW and exact replace flags");
        expect(error.size()<=runtimeAtomicFileMaximumErrorBytes,"bounded diagnostic");
        expect(pass?error.empty():!error.empty(),"diagnostic cleared only on success");
        const bool beforeWriter=fault==probe::Fault::open || fault==probe::Fault::collisions;
        expect(writers==(beforeWriter?0:1),"writer called once only after exclusive create");
        if (fault==probe::Fault::collisions)
            expect(probe::opens==128,"exact 128 collision attempts; no writer or cleanup");
        else expect(probe::opens==1,"non-collision does not retry create");
        if (pass) {
            expect(precedes("open","writer")&&precedes("writer","write")
                &&precedes("write","flush")&&precedes("flush","close")
                &&precedes("close","replace"),"write/flush/close/publication order");
            expect(probe::cleanups==0&&!std::filesystem::exists(probe::temporary),"published temp consumed");
            if (fault==probe::Fault::partialWrite) expect(probe::writes>3,"partial native writes completed");
        } else if (!beforeWriter) {
            expect(probe::closes==1&&probe::cleanups==1,"one close and invocation-owned cleanup");
            expect(precedes("close","cleanup"),"close before cleanup");
            expect(std::filesystem::exists(probe::temporary)==(fault==probe::Fault::cleanup),"cleanup failure leaves evidence, others leave no temp");
            if (fault==probe::Fault::replace || fault==probe::Fault::cleanup)
                expect(probe::replaces==4,"bounded four replacement attempts");
            else expect(probe::replaces==0,"write/flush/close failure never publishes");
            if (fault==probe::Fault::cleanup)
                expect(error.find("cleanup also failed")!=std::string::npos,"cleanup error appended to primary error");
        } else expect(probe::writes==0&&probe::closes==0&&probe::cleanups==0,"failed create never owns a cleanup target");
    }
    for (int mode=0;mode<5;++mode) {
        const auto target=root/("writer-"+std::to_string(mode)+".dat");
        direct(target,original);reset(probe::Fault::none,target);std::string error;int calls=0;
        const bool result=runtimeWriteFileAtomically(target,[&](std::ostream& s,std::string& e)->bool {
            ++calls;s<<"not published";
            if (mode==1) throw std::runtime_error("known exception");
            if (mode==2) throw 7;
            if (mode==3) {e.assign(9000,'x');return false;}
            if (mode==4) {s.setstate(std::ios::badbit);return true;}
            return false;
        },error);
        expect(!result&&calls==1&&read(target)==original,"writer reject/known/unknown/bounded/badbit preserves original");
        expect(!error.empty()&&error.size()<=2048,"writer error bounded without hiding failure");
        expect(probe::replaces==0&&probe::cleanups==1&&!std::filesystem::exists(probe::temporary),"failed writer cannot publish and cleans only its temp");
    }
    const auto unicode=root/std::filesystem::path(std::u8string(u8"原子-目录-🌲"));
    std::filesystem::create_directory(unicode);
    const auto target=unicode/std::filesystem::path(std::u8string(u8"保存-🎮.dat"));
    direct(target,original);reset(probe::Fault::none,target);std::string error;
    expect(runtimeWriteFileAtomically(target,[&](std::ostream& s,std::string&){s<<payload;return true;},error)&&read(target)==payload,"native Chinese and supplementary Unicode target");
    const auto ro=root/"readonly.dat";direct(ro,original);reset(probe::Fault::none,ro);
    expect(SetFileAttributesW(ro.c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"readonly fixture attribute set");
    expect(!runtimeWriteFileAtomically(ro,[](std::ostream& s,std::string&){s<<"replacement";return true;},error)&&read(ro)==original,"actual Windows readonly target preserves original");
    const auto locked=root/"locked.dat";direct(locked,original);reset(probe::Fault::none,locked);
    HANDLE lock=::CreateFileW(locked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    expect(lock!=INVALID_HANDLE_VALUE,"actual sharing-denial lock acquired");
    expect(!runtimeWriteFileAtomically(locked,[](std::ostream& s,std::string&){s<<"replacement";return true;},error)&&read(locked)==original,"actual Windows sharing denial preserves original");
    if(lock!=INVALID_HANDLE_VALUE) ::CloseHandle(lock);
    expect(!runtimeWriteFileAtomically(root/"missing"/"save.dat",[](std::ostream&,std::string&){return true;},error),"missing parent rejected");
    expect(!runtimeWriteFileAtomically(root/"missing-writer.dat",{},error),"missing writer rejected");
    expect(!runtimeWriteFileAtomically({},[](std::ostream&,std::string&){return true;},error),"empty target rejected");
    std::cout<<"W006 Windows production contract: checks="<<checks<<" failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}