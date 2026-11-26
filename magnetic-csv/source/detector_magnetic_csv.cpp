// Computer-only, lossless CDMAG001 export. No detector/serial/USB access.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include "sha256.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace fs=std::filesystem;
using Bytes=std::vector<uint8_t>;
static void need(bool ok,const std::string& message) { if(!ok)throw std::runtime_error(message); }
static std::string lower(std::string s) {
    for(char& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;
}
static std::string hex(const uint8_t* p,size_t n) {
    const char* digits="0123456789abcdef";std::string out;out.reserve(n*2);
    for(size_t i=0;i<n;++i){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;
}
static std::string digest(const Bytes& b) {
    sha256_state s;uint8_t d[32];sha256_start(&s);
    sha256_update(&s,b.data(),uint32_t(b.size()));sha256_finish(&s,d);return hex(d,32);
}
static Bytes read(const fs::path& path,size_t limit) {
    need(fs::is_regular_file(fs::symlink_status(path)),"Input must be a regular file: "+path.filename().u8string());
    const auto size=fs::file_size(path);need(size<=limit,"Input exceeds format limit: "+path.filename().u8string());
    std::ifstream in(path,std::ios::binary);need(bool(in),"Cannot open input");
    Bytes b(static_cast<size_t>(size));in.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));
    need(size_t(in.gcount())==b.size(),"Input truncated while reading");
    char extra;need(!in.get(extra)&&in.eof(),"Input grew or failed while reading");return b;
}
static uint64_t le(const uint8_t* p,unsigned n) {
    uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(p[i])<<(8*i);return v;
}
static uint32_t crc(const uint8_t* p,size_t n) {
    static const auto table=[](){std::array<uint32_t,256> t{};
        for(uint32_t i=0;i<256;++i){uint32_t c=i;for(int k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320U:0U);t[i]=c;}return t;}();
    uint32_t c=~0U;for(size_t i=0;i<n;++i)c=table[(c^p[i])&255]^(c>>8);return ~c;
}
static bool chunk_name(const std::string& n) {
    return n.size()==12&&n[0]=='g'&&n.substr(8)==".bin"&&
        std::all_of(n.begin()+1,n.begin()+8,[](char c){return c>='0'&&c<='9';});
}
struct Export { fs::path output;size_t files=0;uint64_t records=0; };
static Export convert(fs::path folder) {
    folder=fs::canonical(folder);need(fs::is_directory(folder),"Select a copied recording folder");
    fs::path metadata;std::vector<fs::path> chunks;
    for(const auto& e:fs::directory_iterator(folder)) {
        std::string n=lower(e.path().filename().u8string());
        if(n=="gmeta00.jsn") { need(metadata.empty(),"Ambiguous metadata filename case");metadata=e.path(); }
        if(chunk_name(n)){chunks.push_back(e.path());need(chunks.size()<=4096,"Too many magnetic chunks");}
    }
    need(!metadata.empty()&&!chunks.empty(),"Copy gmeta00.jsn and the g0000000.bin magnetic files into one folder first");
    std::sort(chunks.begin(),chunks.end(),[](const auto& a,const auto& b){return lower(a.filename().u8string())<lower(b.filename().u8string());});
    for(size_t i=1;i<chunks.size();++i)need(lower(chunks[i-1].filename().u8string())!=lower(chunks[i].filename().u8string()),"Ambiguous chunk filename case");
    Bytes meta=read(metadata,65536);need(!meta.empty(),"Empty metadata");const auto metadata_sha=digest(meta);
    Export result{folder/"magnetic_csv",chunks.size(),0};
    need(!fs::exists(fs::symlink_status(result.output)),"magnetic_csv already exists; move or rename it before another export");
    const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary=folder/(".magnetic_csv_tmp_"+std::to_string(nonce));
    need(fs::create_directory(temporary),"Cannot create new output folder");
    std::vector<fs::path> owned;
    bool destination_claimed=false;
    auto output=[&](const std::string& name){
        const auto p=temporary/name;need(p.parent_path()==temporary&&!fs::exists(fs::symlink_status(p)),"Output collision");
        owned.push_back(p);std::ofstream f(p,std::ios::binary);f.exceptions(std::ios::badbit|std::ios::failbit);return f;
    };
    try {
        std::string session;
        auto manifest=output("manifest.csv");
        manifest<<"source_file,csv_file,record_count,session_id,metadata_sha256,binary_sha256,header_hex\r\n";
        for(const auto& path:chunks) {
            Bytes b=read(path,64+16384*64);need(b.size()>=64,"Truncated magnetic header");
            need(std::memcmp(b.data(),"CDMAG001",8)==0&&le(b.data()+8,2)==1&&le(b.data()+10,2)==64,"Unknown magnetic format");
            const auto count=le(b.data()+12,4);need(count<=16384&&b.size()==64+count*64,"Magnetic record count/length mismatch");
            need(hex(b.data()+32,32)==metadata_sha,"Magnetic header does not match the exact metadata file");
            auto sid=hex(b.data()+16,16);if(session.empty())session=sid;need(sid==session,"Chunks belong to different sessions");
            const auto csv=lower(path.filename().u8string()).substr(0,8)+".csv";
            auto out=output(csv);
            out<<"record_index,epoch_id,config_id,attempt_seq,drdy_seq,drdy_tick,read_start_tick,read_end_tick,flags,quality_flags,coherence,status_before,status_after,x_raw,y_raw,z_raw,raw_record_hex\r\n";
            for(uint64_t i=0;i<count;++i) {
                const uint8_t* p=b.data()+64+i*64;const auto flags=le(p+48,4);
                need(crc(p,60)==le(p+60,4),"Magnetic record CRC mismatch: "+path.filename().u8string());
                need(!(flags&~uint64_t(0xcf7f))&&(flags>>14)<3,"Unknown magnetic flags/coherence");
                const unsigned offsets[]={54,16,24,32,40,52,53},sizes[]={6,8,8,8,8,1,1};
                for(unsigned k=0;k<7;++k)if(!(flags&(1U<<k)))
                    need(std::all_of(p+offsets[k],p+offsets[k]+sizes[k],[](uint8_t v){return v==0;}),"Noncanonical absent field");
                out<<i<<','<<le(p,4)<<','<<le(p+4,4)<<','<<le(p+8,8);
                for(unsigned k=1;k<=4;++k){out<<',';if(flags&(1U<<k))out<<le(p+offsets[k],8);}
                const char* coherence[]={"unknown","qualified","incoherent"};
                out<<','<<flags<<','<<(flags&0xf00)<<','<<coherence[flags>>14];
                for(unsigned k=5;k<=6;++k){out<<',';if(flags&(1U<<k))out<<unsigned(p[offsets[k]]);}
                for(unsigned k=0;k<3;++k){out<<',';if(flags&1){int v=int(le(p+54+2*k,2));out<<(v>=32768?v-65536:v);}}
                out<<','<<hex(p,64)<<"\r\n";
            }
            out.close();
            manifest<<path.filename().u8string()<<','<<csv<<','<<count<<','<<session<<','<<metadata_sha<<','<<digest(b)<<','<<hex(b.data(),64)<<"\r\n";
            result.records+=count;
        }
        manifest.close();
        auto copied=output("gmeta00.json");copied.write(reinterpret_cast<const char*>(meta.data()),std::streamsize(meta.size()));copied.close();
        auto guide=output("readme.txt");
        guide<<"CoughDetector magnetic CSV export\r\n"
            "Open g0000000.csv and subsequent chunks in a spreadsheet. Delimiter: comma.\r\n"
            "Import all columns as Text to preserve large counters and raw hex exactly.\r\n"
            "x_raw/y_raw/z_raw are signed sensor-native counts; absent observations are blank.\r\n"
            "An observed zero remains0. No interpolation, calibration or axis transform is applied.\r\n"
            "Nominal units, clock and sensor configuration remain in gmeta00.json (exact source bytes).\r\n"
            "For this LIS2MDL configuration, nominal magnetic scale is0.15 microtesla/count.\r\n"
            "Ticks are hardware observations, not UTC or a physical sensor-alignment guarantee.\r\n"
            "Flags retain presence bits0..6, quality bits8..11 and coherence bits14..15.\r\n"
            "Quality:256 transport error,512 overrun,1024 queue loss,2048 late; bits may combine.\r\n"
            "Coherence is the recorder's claim, not newly verified by conversion.\r\n"
            "Record indices restart for each source chunk. Gaps/epochs are not renumbered away.\r\n"
            "manifest.csv preserves each64-byte header, source hash and metadata binding.\r\n"
            "raw_record_hex preserves all64 record bytes, including CRC; conversion is reversible.\r\n"
            "This tool checks layout, counts, metadata hashes, CRC and canonical field encoding.\r\n"
            "It does not validate metadata semantics, acquisition completeness or timing accuracy.\r\n"
            "Source files are unchanged. Work on copied recordings, not a live device.\r\n";
        guide.close();need(read(metadata,65536)==meta,"Metadata changed during conversion");
#ifdef _WIN32
        need(MoveFileW(temporary.c_str(),result.output.c_str())!=0,"Cannot publish output; destination may already exist");
#else
        if(syscall(SYS_renameat2,AT_FDCWD,temporary.c_str(),AT_FDCWD,result.output.c_str(),1)!=0) {
            const int error=errno;
            need(error==EINVAL||error==ENOSYS||error==EOPNOTSUPP,
                 "Cannot publish output: "+std::string(std::strerror(error)));
            // WSL's Windows mount lacks RENAME_NOREPLACE. Atomically claim a
            // fresh empty destination first; never replace an existing export.
            need(fs::create_directory(result.output),"Output already exists");destination_claimed=true;
            fs::rename(temporary,result.output);
        }
#endif
    } catch(...) {
        // Remove only files created inside this exact fresh temporary directory.
        // Never recurse, follow a selected input, or remove an existing export.
        for(const auto& p:owned){std::error_code ec;fs::remove(p,ec);}
        std::error_code ec;fs::remove(temporary,ec);
        if(destination_claimed)fs::remove(result.output,ec); // Our empty claim only; nonempty refuses.
        throw;
    }
    return result;
}
static int command(const fs::path& folder) {
    const auto r=convert(folder);std::cout<<"Exported "<<r.records<<" records in "<<r.files<<" CSV files to "<<r.output.u8string()<<"\n";return 0;
}
#ifdef _WIN32
static std::wstring wide(const std::string& text) {
    int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n);return out;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 1;
    bool gui=argc<2||std::wcscmp(argv[1],L"--cli")!=0;int code=0;
    try {
        fs::path folder;
        if(argc==1){wchar_t path[32768]={};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=nullptr;
            d.lpstrFilter=L"Recording metadata (gmeta00.jsn)\0gmeta00.jsn\0All files\0*.*\0";d.lpstrFile=path;d.nMaxFile=32768;
            d.lpstrTitle=L"Select gmeta00.jsn in your copied recording folder";d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetOpenFileNameW(&d)){need(CommDlgExtendedError()==0,"Cannot open folder selection dialog");LocalFree(argv);return 0;}
            folder=fs::path(path).parent_path();
        }else if(argc==3&&std::wcscmp(argv[1],L"--cli")==0)folder=argv[2];
        else if(argc==2&&std::wcscmp(argv[1],L"--cli")!=0)folder=argv[1];
        else throw std::runtime_error("Drop a copied recording folder on this program, or use --cli FOLDER");
        if(gui){auto r=convert(folder);std::wstring message=L"Created "+std::to_wstring(r.files)+L" CSV files in:\n"+r.output.wstring()+L"\n\nOriginal recordings are unchanged.";MessageBoxW(nullptr,message.c_str(),L"CoughDetector magnetic CSV",MB_OK|MB_ICONINFORMATION);}
        else code=command(folder);
    }catch(const std::exception& e){code=1;if(gui)MessageBoxW(nullptr,wide(e.what()).c_str(),L"CoughDetector magnetic CSV",MB_OK|MB_ICONERROR);else std::cerr<<e.what()<<"\n";}
    LocalFree(argv);return code;
}
#else
int main(int argc,char** argv) {
    try{if(argc==2&&std::strcmp(argv[1],"--cli")!=0)return command(argv[1]);if(argc==3&&std::strcmp(argv[1],"--cli")==0)return command(argv[2]);
        throw std::runtime_error("Usage: detector-magnetic-csv FOLDER (a copied recording folder)");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
#endif
