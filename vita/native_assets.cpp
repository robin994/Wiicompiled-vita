#include "native_assets.hpp"
#include "psarc_backend.hpp"
#include "native_prewarm_plan.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <list>
#include <limits>
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
size_t warmBudget=0;
uint64_t catalogueFingerprint=0,activeScene=0;
bool manifestDirty=false;
std::string manifestRoot;
std::vector<uint32_t> sceneHits;
std::vector<prewarm::Record> pendingScene;
size_t pendingCursor=0;
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
std::string manifest_path(uint64_t scene) {
    char suffix[48]{};
    std::snprintf(suffix,sizeof(suffix),"/native_hot_%016llx.warm",
                  static_cast<unsigned long long>(scene));
    return manifestRoot+suffix;
}
std::string asset_path(const Entry& entry) {
    char filename[80]{};
    std::snprintf(filename,sizeof(filename),"native/v1/%s/%016llx.avnr",
        entry.kind==1?"textures":"geometry",
        static_cast<unsigned long long>(entry.key));
    return filename;
}
void insert_cached(std::string path,std::vector<uint8_t>&& loaded) {
    const size_t bytes=loaded.size();
    if(!cacheBudget || bytes>cacheBudget)return;
    while(cacheBytes>cacheBudget-bytes && !cache.empty()) {
        cacheBytes-=cache.back().data.size();cache.pop_back();
    }
    cache.push_front({std::move(path),std::move(loaded)});cacheBytes+=bytes;
}
bool checkpoint_locked() {
    if(!warmBudget || !manifestDirty || manifestRoot.empty() || !psarc::IsMounted())return false;
    std::vector<prewarm::Record> candidates;
    for(size_t i=0;i<sceneHits.size();++i)if(sceneHits[i])
        candidates.push_back({entries[i].kind,entries[i].key,entries[i].size,sceneHits[i]});
    const auto ranked=prewarm::rank(std::move(candidates));
    if(ranked.empty())return false;
    const auto bytes=prewarm::encode(activeScene,catalogueFingerprint,ranked);
    if(bytes.empty())return false;
    const std::string path=manifest_path(activeScene);
    const std::string temp=path+".tmp";
    FILE* file=std::fopen(temp.c_str(),"wb");
    if(!file)return false;
    const bool wrote=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
    const bool closed=std::fclose(file)==0;
    if(!wrote || !closed || std::rename(temp.c_str(),path.c_str())!=0) {
        std::remove(temp.c_str());return false;
    }
    manifestDirty=false;
    std::fprintf(stderr,"[asset-prewarm] phase=checkpoint scene=%016llx entries=%u bytes=%u\n",
        static_cast<unsigned long long>(activeScene),unsigned(ranked.size()),unsigned(bytes.size()));
    return true;
}
bool select_manifest_locked(uint64_t scene,PrewarmStats& stats,
                            std::vector<prewarm::Record>& chosen) {
    if(!warmBudget || !catalogueFingerprint || manifestRoot.empty() || !psarc::IsMounted())return false;
    const std::string path=manifest_path(scene);
    FILE* file=std::fopen(path.c_str(),"rb");
    if(!file)return false;
    std::vector<uint8_t> buffer(prewarm::HeaderBytes+prewarm::MaxRecords*prewarm::RecordBytes+9);
    const size_t bytes=std::fread(buffer.data(),1,buffer.size(),file);
    std::fclose(file);
    if(bytes==buffer.size())return false; // oversized or corrupt
    buffer.resize(bytes);
    std::vector<prewarm::Record> candidates;
    if(!prewarm::decode(buffer,scene,catalogueFingerprint,candidates))return false;
    stats.manifestLoaded=true;
    chosen=prewarm::select(candidates,warmBudget);
    stats.selected=static_cast<uint32_t>(chosen.size());
    return true;
}
PrewarmStats warm_locked(uint64_t scene) {
    PrewarmStats stats{};
    std::vector<prewarm::Record> chosen;
    if(!select_manifest_locked(scene,stats,chosen))return stats;
    // Insert least-hot first: the most frequently used assets should be MRU,
    // even when the previous scene's cache must evict cold entries.
    for(auto candidate=chosen.rbegin();candidate!=chosen.rend();++candidate) {
        Entry request{candidate->kind,candidate->key,candidate->size};
        const auto it=std::lower_bound(entries.begin(),entries.end(),request,less);
        if(it==entries.end() || it->kind!=request.kind || it->key!=request.key ||
           it->size!=request.size) {++stats.rejected;continue;}
        const std::string resource=asset_path(*it);
        const auto cached=std::find_if(cache.begin(),cache.end(),
                              [&](const Cached& c){return c.path==resource;});
        if(cached!=cache.end()) {
            cache.splice(cache.begin(),cache,cached);++stats.alreadyCached;continue;
        }
        std::vector<uint8_t> loaded;
        if(!psarc::ReadWhole(resource,loaded,it->size) || loaded.size()!=it->size) {
            ++stats.rejected;continue;
        }
        stats.bytes+=loaded.size();++stats.loaded;
        insert_cached(resource,std::move(loaded));
    }
    std::fprintf(stderr,"[asset-prewarm] phase=warm scene=%016llx manifest=1 selected=%u loaded=%u existing=%u rejected=%u bytes=%u cache=%u/%u\n",
        static_cast<unsigned long long>(scene),stats.selected,stats.loaded,
        stats.alreadyCached,stats.rejected,unsigned(stats.bytes),
        unsigned(cacheBytes),unsigned(cacheBudget));
    return stats;
}
PrewarmStats queue_scene_locked(uint64_t scene) {
    PrewarmStats stats{};
    pendingScene.clear();pendingCursor=0;
    (void)select_manifest_locked(scene,stats,pendingScene);
    pendingCursor=pendingScene.size();
    stats.remaining=static_cast<uint32_t>(pendingCursor);
    return stats;
}
PrewarmStats step_scene_locked(size_t maxBytes,unsigned maxAssets) {
    PrewarmStats stats{};
    if(!warmBudget || !pendingCursor || !maxBytes || !maxAssets)return stats;
    maxBytes=std::min(maxBytes,size_t(256*1024));
    maxAssets=std::min(maxAssets,4u);
    // The highest-ranked record is loaded last so it remains the LRU's MRU.
    // Each tick processes at most maxAssets, and transfers at most maxBytes
    // of PSARC data. Oversized records use the ordinary on-demand path.
    for(unsigned i=0;i<maxAssets && pendingCursor;++i) {
        const auto candidate=pendingScene[--pendingCursor];
        Entry request{candidate.kind,candidate.key,candidate.size};
        const auto it=std::lower_bound(entries.begin(),entries.end(),request,less);
        if(it==entries.end() || it->kind!=request.kind || it->key!=request.key ||
           it->size!=request.size) {++stats.rejected;continue;}
        const std::string resource=asset_path(*it);
        const auto cached=std::find_if(cache.begin(),cache.end(),
                             [&](const Cached& c){return c.path==resource;});
        if(cached!=cache.end()) {
            cache.splice(cache.begin(),cache,cached);++stats.alreadyCached;continue;
        }
        if(it->size>maxBytes) {
            // One oversized sidecar cannot ever fit this tick's I/O budget.
            // Leave it for on-demand loading, not repeated speculative reads.
            ++stats.skippedLarge;continue;
        }
        if(it->size>maxBytes-stats.bytes) {
            // Enough for a future tick but not for this one. The original
            // implementation dropped it permanently, so a multi-asset step
            // could never warm all budget-eligible resources.
            ++pendingCursor;
            break;
        }
        std::vector<uint8_t> loaded;
        if(!psarc::ReadWhole(resource,loaded,it->size) || loaded.size()!=it->size) {
            ++stats.rejected;continue;
        }
        stats.bytes+=loaded.size();++stats.loaded;
        insert_cached(resource,std::move(loaded));
    }
    stats.remaining=static_cast<uint32_t>(pendingCursor);
    if(!pendingCursor)pendingScene.clear();
    return stats;
}
}
bool Initialize(size_t requestedBudget) noexcept {
    return Initialize(requestedBudget,0,nullptr);
}
bool Initialize(size_t requestedBudget,size_t requestedWarmBudget,const char* root) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        entries.clear();cache.clear();cacheBytes=0;
        sceneHits.clear();activeScene=0;manifestDirty=false;catalogueFingerprint=0;
        pendingScene.clear();pendingCursor=0;
        warmBudget=0;manifestRoot.clear();
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
        catalogueFingerprint=prewarm::fingerprint(index);
        warmBudget=std::min({requestedWarmBudget,cacheBudget,size_t(8*1024*1024)});
        if(warmBudget && root && *root) {
            manifestRoot=root;sceneHits.assign(entries.size(),0);
        } else warmBudget=0;
        const auto textures=std::count_if(entries.begin(),entries.end(),[](auto& e){return e.kind==1;});
        std::fprintf(stderr,"[native-assets] catalogue=%u textures=%u geometry=%u cache_budget=%u\n",
                     unsigned(entries.size()),unsigned(textures),unsigned(entries.size()-size_t(textures)),unsigned(cacheBudget));
        if(warmBudget) {
            std::fprintf(stderr,"[asset-prewarm] phase=initialized budget=%u catalogue=%016llx\n",
                unsigned(warmBudget),static_cast<unsigned long long>(catalogueFingerprint));
        }
        return true;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"[native-assets] rejected: %s; original assets enabled\n",error.what());return false;
    } catch(...) {return false;}
}
PrewarmStats WarmBoot() noexcept {
    try {std::lock_guard<std::mutex> lock(mutex);return warm_locked(0);}
    catch(...) {return {};}
}
PrewarmStats EnterScene(uint64_t scene) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if(!warmBudget || scene==activeScene)return {};
        (void)checkpoint_locked();
        activeScene=scene;
        pendingScene.clear();pendingCursor=0;
        std::fill(sceneHits.begin(),sceneHits.end(),0);
        manifestDirty=false;
        return warm_locked(scene);
    } catch(...) {return {};}
}
PrewarmStats QueueScene(uint64_t scene) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if(!warmBudget || scene==activeScene)return {};
        (void)checkpoint_locked();
        activeScene=scene;
        std::fill(sceneHits.begin(),sceneHits.end(),0);
        manifestDirty=false;
        const auto stats=queue_scene_locked(scene);
        if(stats.manifestLoaded)
            std::fprintf(stderr,
                "[asset-prewarm] phase=scheduled scene=%016llx selected=%u pending=%u io_bytes=0\n",
                static_cast<unsigned long long>(scene),stats.selected,stats.remaining);
        return stats;
    } catch(...) {return {};}
}
PrewarmStats StepScenePrewarm(size_t maxBytes,unsigned maxAssets) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        return step_scene_locked(maxBytes,maxAssets);
    } catch(...) {return {};}
}
bool Checkpoint() noexcept {
    try {std::lock_guard<std::mutex> lock(mutex);return checkpoint_locked();}
    catch(...) {return false;}
}
bool PrewarmEnabled() noexcept {return warmBudget!=0;}
bool Read(const char* path,std::vector<uint8_t>& bytes,void*) noexcept {
    try {
        const bool shader=shaderPath(path);
        Entry request{};if(!shader && !pathKey(path,request))return false;
        std::lock_guard<std::mutex> lock(mutex);
        const auto it=std::lower_bound(entries.begin(),entries.end(),request,less);
        // Misses never open a file, schedule the I/O thread, or populate an
        // unbounded negative cache for changing video/EFB pixels.
        if(!shader && (it==entries.end()||it->kind!=request.kind||it->key!=request.key))return false;
        const auto recordHit=[&] {
            if(!shader && warmBudget && !sceneHits.empty()) {
                auto& hits=sceneHits[static_cast<size_t>(it-entries.begin())];
                // The manifest is a *set* of hot sidecars. Rewriting it on
                // every cache hit caused periodic flash writes even when no
                // new asset had been loaded since the last checkpoint.
                if(hits==0)manifestDirty=true;
                if(hits<std::numeric_limits<uint32_t>::max())++hits;
            }
        };
        for(auto i=cache.begin();i!=cache.end();++i)if(i->path==path) {
            bytes=i->data;cache.splice(cache.begin(),cache,i);recordHit();return true;
        }
        std::vector<uint8_t> loaded;
        // ProgramBinaryCache validates the GXP header, source/stage identity,
        // extent and checksum before registration; this reader only transports
        // a bounded record from an exact archive namespace.
        const size_t maxBytes=shader?4*1024*1024+64:it->size;
        if(!psarc::ReadWhole(path,loaded,maxBytes)||
           (shader?loaded.size()<48:loaded.size()!=it->size))return false;
        if(loaded.size()<=cacheBudget)insert_cached(path,std::vector<uint8_t>(loaded));
        recordHit();
        bytes=std::move(loaded);return true;
    } catch(...) {return false;}
}
void Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    (void)checkpoint_locked();
    entries.clear();cache.clear();cacheBytes=0;sceneHits.clear();
    pendingScene.clear();pendingCursor=0;
    warmBudget=0;manifestRoot.clear();catalogueFingerprint=0;activeScene=0;manifestDirty=false;
}
}
extern "C" bool WicReadNativeAsset(const char* path,std::vector<uint8_t>& bytes,void* context) noexcept {
    return wiicompiled::vita::assets::Read(path,bytes,context);
}
