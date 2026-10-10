#include "native_assets.hpp"
#include "psarc_backend.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <list>
#include <mutex>
#include <string>
#include <stdexcept>

namespace wiicompiled::vita::assets {
namespace {
struct Entry {uint32_t kind;uint64_t key;uint32_t size;};
struct Cached {std::string path;std::vector<uint8_t> data;};
std::vector<Entry> entries;
std::list<Cached> cache;
std::mutex mutex;
size_t cacheBytes=0;
size_t cacheBudget=2*1024*1024;
constexpr size_t RecordLimit=32*1024*1024;
uint32_t word(const uint8_t* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
bool less(const Entry& a,const Entry& b) {return a.kind!=b.kind?a.kind<b.kind:a.key<b.key;}
bool pathKey(const char* path,Entry& entry) {
    if(!path)return false;
    const char* hex=nullptr;
    if(!std::strncmp(path,"native/v1/textures/",19)){entry.kind=1;hex=path+19;}
    else if(!std::strncmp(path,"native/v1/geometry/",19)){entry.kind=2;hex=path+19;}
    else return false;
    if(std::strlen(hex)!=21||std::strcmp(hex+16,".avnr"))return false;
    entry.key=0;
    for(unsigned i=0;i<16;++i) {
        const unsigned c=static_cast<unsigned char>(hex[i]);
        const int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
        if(digit<0)return false;
        entry.key=(entry.key<<4)|unsigned(digit);
    }
    return true;
}
bool shaderPath(const char* path) {
    constexpr char prefix[]="native/v1/shaders/gxm-cg-gxp-v1/";
    if(!path || std::strncmp(path,prefix,sizeof(prefix)-1))return false;
    const char* name=path+sizeof(prefix)-1;
    if(std::strlen(name)!=22 || (name[0]!='v' && name[0]!='f') ||
       name[1]!='-' || std::strcmp(name+18,".gxp"))return false;
    for(unsigned i=2;i<18;++i)
        if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return false;
    return true;
}
}
bool Initialize(size_t requestedBudget) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        entries.clear();cache.clear();cacheBytes=0;
        cacheBudget=std::min(requestedBudget,size_t(RecordLimit));
        std::vector<uint8_t> index;
        if(!psarc::ReadWhole("native/v1/index.bin",index,4*1024*1024)) {
            std::fprintf(stderr,"[native-assets] catalogue absent; original assets enabled\n");return false;
        }
        if(index.size()<16||std::memcmp(index.data(),"WICNAT01",8)||word(index.data()+8)!=1)
            throw std::runtime_error("invalid native asset catalogue header");
        const size_t count=word(index.data()+12);
        if(count>200000||index.size()!=16+count*16)throw std::runtime_error("invalid native asset catalogue extent");
        std::vector<Entry> staged;staged.reserve(count);
        for(size_t i=0;i<count;++i) {
            const auto* p=index.data()+16+i*16;
            Entry entry{word(p),uint64_t(word(p+4))|(uint64_t(word(p+8))<<32),word(p+12)};
            if((entry.kind!=1&&entry.kind!=2)||entry.size<28||entry.size>RecordLimit||
                (!staged.empty()&&!less(staged.back(),entry)))throw std::runtime_error("invalid native asset catalogue entry");
            staged.push_back(entry);
        }
        entries=std::move(staged);
        const auto textures=std::count_if(entries.begin(),entries.end(),[](auto& e){return e.kind==1;});
        std::fprintf(stderr,"[native-assets] catalogue=%u textures=%u geometry=%u cache_budget=%u\n",
                     unsigned(entries.size()),unsigned(textures),unsigned(entries.size()-size_t(textures)),unsigned(cacheBudget));
        return true;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"[native-assets] rejected: %s; original assets enabled\n",error.what());return false;
    } catch(...) {return false;}
}
bool Read(const char* path,std::vector<uint8_t>& bytes,void*) noexcept {
    try {
        const bool shader=shaderPath(path);
        Entry request{};if(!shader && !pathKey(path,request))return false;
        std::lock_guard<std::mutex> lock(mutex);
        const auto it=std::lower_bound(entries.begin(),entries.end(),request,less);
        // Misses never open a file, schedule the I/O thread, or populate an
        // unbounded negative cache for changing video/EFB pixels.
        if(!shader && (it==entries.end()||it->kind!=request.kind||it->key!=request.key))return false;
        for(auto i=cache.begin();i!=cache.end();++i)if(i->path==path) {
            bytes=i->data;cache.splice(cache.begin(),cache,i);return true;
        }
        std::vector<uint8_t> loaded;
        // ProgramBinaryCache validates the GXP header, source/stage identity,
        // extent and checksum before registration; this reader only transports
        // a bounded record from an exact archive namespace.
        const size_t maxBytes=shader?4*1024*1024+64:it->size;
        if(!psarc::ReadWhole(path,loaded,maxBytes)||
           (shader?loaded.size()<48:loaded.size()!=it->size))return false;
        if(loaded.size()<=cacheBudget) {
            while(cacheBytes+loaded.size()>cacheBudget&&!cache.empty()) {
                cacheBytes-=cache.back().data.size();cache.pop_back();
            }
            cache.push_front({path,loaded});cacheBytes+=loaded.size();
        }
        bytes=std::move(loaded);return true;
    } catch(...) {return false;}
}
void Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex);entries.clear();cache.clear();cacheBytes=0;
}
}
extern "C" bool WicReadNativeAsset(const char* path,std::vector<uint8_t>& bytes,void* context) noexcept {
    return wiicompiled::vita::assets::Read(path,bytes,context);
}
