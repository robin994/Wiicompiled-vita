#include "wiicompiled/disc.hpp"
#include "wiicompiled/disc_index.hpp"
#include "psarc_backend.hpp"
#include "port/asset_archive.h"
#include <openssl/sha.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
namespace fs=std::filesystem;
namespace archive=wiicompiled::vita::psarc;
static void check(bool okay,const std::string& reason) { if(!okay)throw std::runtime_error(reason); }
static bool read(void*,const std::string& path,uint64_t at,void* out,size_t count) {
    return archive::Read(path,at,out,count);
}
int main(int argc,char** argv) {
    if(argc!=4) { std::cerr<<"Usage: wiicompiled-psarc verify-root ARCHIVE ROOT | verify-iso ARCHIVE ISO\n";return 2; }
    try {
        const std::string mode=argv[1];
        check(mode=="verify-root" || mode=="verify-iso","Unknown command");
        check(archive::Mount(argv[2]),archive::LastError());
        char error[192]{};
        auto* catalogue=port_asset_archive_open(argv[2],error,sizeof(error));
        check(catalogue,error);
        std::unique_ptr<PortAssetArchive,decltype(&port_asset_archive_close)> owner(catalogue,port_asset_archive_close);
        const unsigned count=port_asset_archive_count(catalogue);
        std::array<uint8_t,1024*1024> a{},b{};
        uint64_t total=0;
        if(mode=="verify-root") {
            size_t sourceFiles=0;
            for(const auto& f:fs::recursive_directory_iterator(argv[3])) {
                check(!f.is_symlink(),"Source symlink");if(f.is_regular_file())++sourceFiles;
            }
            check(sourceFiles==count,"Source/archive inventory mismatch");
            for(unsigned i=0;i<count;++i) {
                const std::string name=port_asset_archive_path(catalogue,i);
                const uint64_t size=port_asset_archive_size(catalogue,i);
                const auto source=fs::path(argv[3])/name;
                check(fs::is_regular_file(source) && fs::file_size(source)==size,"Source size differs: "+name);
                std::ifstream file(source,std::ios::binary);
                for(uint64_t at=0;at<size;) {
                    const size_t n=static_cast<size_t>(std::min<uint64_t>(a.size(),size-at));
                    check(archive::Read(name,at,a.data(),n),"Archive read failed: "+name);
                    file.read(reinterpret_cast<char*>(b.data()),n);
                    check(file.good() && !std::memcmp(a.data(),b.data(),n),"Source bytes differ: "+name);
                    at+=n;
                }
                check(archive::Read(name,size,nullptr,0),"EOF empty read failed");
                check(!archive::Read(name,size,a.data(),1),"Read beyond EOF accepted");
                check(!archive::Read(name,0,nullptr,1) || size==0,"Null read accepted");
                total+=size;
            }
        } else {
            wiicompiled::Disc disc;disc.open(argv[3]);
            std::vector<uint8_t> bytes;
            check(archive::ReadWhole("disc/index.bin",bytes,8*1024*1024),"Index absent");
            check(bytes==wiicompiled::encodeDiscIndex(disc),"ISO/index extents differ");
            const auto index=wiicompiled::decodeDiscIndex(bytes.data(),bytes.size());
            size_t files=2; // index and raw header, in addition to indexed files.
            for(const auto& e:disc.entries())if(!e.directory) {
                ++files;uint64_t size=0;
                check(archive::Size("disc"+e.path,size) && size==e.size,"ISO/archive size differs: "+e.path);
                for(uint64_t at=0;at<e.size;) {
                    const size_t n=static_cast<size_t>(std::min<uint64_t>(a.size(),e.size-at));
                    wiicompiled::readDiscIndex(index,e.offset+at,a.data(),n,read,nullptr);
                    disc.readFile(e.path,at,b.data(),n);
                    check(!std::memcmp(a.data(),b.data(),n),"ISO/partition bytes differ: "+e.path);
                    at+=n;
                }
                total+=e.size;
            }
            check(files==count,"ISO/archive inventory differs");
            check(archive::ReadWhole("disc/raw/header.bin",bytes,0x50000) && bytes.size()==0x50000,"Raw header absent");
            disc.readRaw(0,a.data(),bytes.size());
            check(!std::memcmp(a.data(),bytes.data(),bytes.size()),"Raw ISO header differs");
        }
        archive::Shutdown();
        check(!archive::IsMounted(),"Shutdown failed");
        std::cout<<"{\"entries\":"<<count<<",\"compared_bytes\":"<<total
                 <<",\"native_reader\":true,\"full_source_comparison\":true}\n";
        return 0;
    } catch(const std::exception& error) {
        archive::Shutdown();std::cerr<<"psarc: "<<error.what()<<'\n';return 1;
    }
}
