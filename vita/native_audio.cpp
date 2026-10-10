#include "native_audio.hpp"
#include "psarc_backend.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wiicompiled::vita::assets {
namespace {
constexpr size_t Budget=2*1024*1024;
uint32_t word(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
int16_t sample(const uint8_t* p){return static_cast<int16_t>(uint16_t(p[0])|(uint16_t(p[1])<<8));}
uint64_t hash(const uint8_t* p,size_t n){uint64_t h=14695981039346656037ull;for(size_t i=0;i<n;++i)h=(h^p[i])*1099511628211ull;return h;}
// Keep the catalogue compact: tens of thousands of full path strings would
// retain several extra MiB before any sample was used. Expand only cold hits.
struct Entry {uint64_t key;uint32_t size;std::array<uint8_t,32> digest;};
std::string pathFor(const Entry& entry) {
    constexpr char digits[]="0123456789abcdef";
    std::string path="native/v1/audio/";path.reserve(84);
    for(auto byte:entry.digest){path+=digits[byte>>4];path+=digits[byte&15];}
    return path+".awpc";
}
struct Record {std::vector<uint8_t> bytes;size_t sourceBytes=0,frames=0;};
struct Binding {uintptr_t base=0;std::shared_ptr<Record> record;};
struct Cached {std::string path;std::shared_ptr<Record> record;};
std::vector<Entry> entries;
std::list<Cached> records;
std::vector<Binding> bindings;
std::mutex audioMutex;
size_t used=0;
uint64_t hits=0,misses=0,rejected=0;
std::shared_ptr<Record> load(const Entry& entry) {
    const auto path=pathFor(entry);
    for(auto it=records.begin();it!=records.end();++it)if(it->path==path) {
        auto record=it->record;records.splice(records.begin(),records,it);return record;
    }
    auto record=std::make_shared<Record>();auto& b=record->bytes;
    if(entry.size>256*1024||!psarc::ReadWhole(path,b,entry.size)||b.size()!=entry.size||b.size()<60||std::memcmp(b.data(),"WICPCM01",8))return {};
    record->sourceBytes=word(b.data()+8);record->frames=(uint64_t(word(b.data()+12))+13)/14;
    if(!record->frames||record->sourceBytes!=record->frames*8||b.size()!=52+record->sourceBytes+record->frames*32+8)return {};
    const auto* tail=b.data()+b.size()-8;
    if(hash(b.data(),b.size()-8)!=(uint64_t(word(tail))|(uint64_t(word(tail+4))<<32))){++rejected;return {};}
    while(used+b.size()>Budget&&!records.empty()) {
        const auto evicted=records.back().record;
        bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](auto& binding){return binding.record==evicted;}),bindings.end());
        used-=evicted->bytes.size();records.pop_back();
    }
    records.push_front({path,record});used+=b.size();return record;
}
unsigned match(const Binding& binding,const uint8_t* source,const int16_t* coefficients,int16_t y1,int16_t y2,
               unsigned offset,unsigned pred,int16_t* pcm,uint8_t* nibbles) {
    const auto address=reinterpret_cast<uintptr_t>(source);
    const auto& record=*binding.record;const auto& b=record.bytes;
    if(address<binding.base||address-binding.base>=record.sourceBytes||(address-binding.base)%8||offset>=14)return 0;
    const size_t frame=(address-binding.base)/8;
    const auto* raw=b.data()+52+frame*8;const auto* decoded=b.data()+52+record.sourceBytes+frame*32;
    if(std::memcmp(source,raw,8)||(raw[0]&0x7f)!=pred)return 0;
    for(unsigned i=0;i<16;++i)if(coefficients[i]!=sample(b.data()+16+i*2))return 0;
    const int16_t expected1=offset?sample(decoded+4+(offset-1)*2):sample(decoded);
    const int16_t expected2=offset>=2?sample(decoded+4+(offset-2)*2):offset?sample(decoded):sample(decoded+2);
    if(y1!=expected1||y2!=expected2)return 0;
    const unsigned count=14-offset;
    for(unsigned i=0;i<count;++i) {
        const unsigned n=i+offset;pcm[i]=sample(decoded+4+n*2);
        nibbles[i]=(n&1)?raw[1+n/2]&15:raw[1+n/2]>>4;
    }
    ++hits;return count;
}
}
bool InitializeAudio() noexcept {
    try {
        std::lock_guard<std::mutex> lock(audioMutex);entries.clear();records.clear();bindings.clear();used=0;hits=misses=rejected=0;
        std::vector<uint8_t> b;
        if(!psarc::ReadWhole("native/v1/audio-index.bin",b,4*1024*1024))return false;
        if(b.size()<16||std::memcmp(b.data(),"WICAIDX1",8)||word(b.data()+8)!=1)return false;
        const size_t count=word(b.data()+12);
        if(count>90000||b.size()!=16+count*44)return false;
        std::vector<Entry> staged;staged.reserve(count);
        for(size_t i=0;i<count;++i) {
            const auto* p=b.data()+16+i*44;Entry entry{uint64_t(word(p))|(uint64_t(word(p+4))<<32),word(p+8),{}};
            if(entry.size<60||entry.size>256*1024||(!staged.empty()&&entry.key<staged.back().key))return false;
            std::copy_n(p+12,32,entry.digest.begin());staged.push_back(entry);
        }
        entries=std::move(staged);
        std::fprintf(stderr,"[native-audio] indexed_blocks=%zu catalogue_bytes=%zu pcm=LE16 cache_budget=%zu exact_frame_validation=1\n",entries.size(),entries.size()*sizeof(Entry),Budget);return true;
    } catch(...) {return false;}
}
void ShutdownAudio() noexcept {
    std::lock_guard<std::mutex> lock(audioMutex);
    std::fprintf(stderr,"[native-audio] frame_hits=%llu misses=%llu rejected=%llu\n",
        (unsigned long long)hits,(unsigned long long)misses,(unsigned long long)rejected);
    entries.clear();records.clear();bindings.clear();used=0;
}
void ReportAudio(uint64_t frame) noexcept {
    std::lock_guard<std::mutex> lock(audioMutex);
    std::fprintf(stderr,"[native-audio] frame=%llu hits_total=%llu misses_total=%llu rejected_total=%llu cache_bytes=%zu bindings=%zu\n",
        (unsigned long long)frame,(unsigned long long)hits,(unsigned long long)misses,
        (unsigned long long)rejected,used,bindings.size());
}
unsigned AudioFrame(const uint8_t* source,const int16_t* coefficients,int16_t y1,int16_t y2,
                    unsigned offset,unsigned pred,int16_t* pcm,uint8_t* nibbles) noexcept {
    if(!source||!coefficients||!pcm||!nibbles||offset>=14)return 0;
    try {
        std::lock_guard<std::mutex> lock(audioMutex);
        if(entries.empty())return 0;
        for(const auto& binding:bindings)if(const auto count=match(binding,source,coefficients,y1,y2,offset,pred,pcm,nibbles))return count;
        // A new block is identified only at its first DSP frame. Subsequent
        // frames/partial AX tasks use the validated pointer binding. An unknown
        // bank, changed buffer, changed predictor or loop history falls back.
        if(offset){++misses;return 0;}
        uint8_t key[44];std::memcpy(key,source,8);
        for(unsigned i=0;i<16;++i){key[8+i*2]=uint16_t(coefficients[i])&255;key[9+i*2]=uint16_t(coefficients[i])>>8;}
        key[40]=uint16_t(y1)&255;key[41]=uint16_t(y1)>>8;key[42]=uint16_t(y2)&255;key[43]=uint16_t(y2)>>8;
        const auto lookup=hash(key,sizeof key);
        auto it=std::lower_bound(entries.begin(),entries.end(),lookup,[](auto& entry,uint64_t value){return entry.key<value;});
        // Prefix collisions remain candidates only. The per-frame source,
        // coefficients and both histories determine the exact PCM result.
        for(unsigned candidates=0;it!=entries.end()&&it->key==lookup&&candidates<8;++it,++candidates) {
            auto record=load(*it);if(!record)continue;
            Binding binding{reinterpret_cast<uintptr_t>(source),std::move(record)};
            const auto count=match(binding,source,coefficients,y1,y2,offset,pred,pcm,nibbles);
            if(!count)continue;
            if(bindings.size()>=128)bindings.erase(bindings.begin());
            bindings.push_back(std::move(binding));return count;
        }
        ++misses;return 0;
    } catch(...) {return 0;}
}
}
extern "C" unsigned WicNativeAudioFrame(const uint8_t* source,const int16_t* coefficients,
    int16_t y1,int16_t y2,unsigned offset,unsigned pred,int16_t* pcm,uint8_t* nibbles) noexcept {
    return wiicompiled::vita::assets::AudioFrame(source,coefficients,y1,y2,offset,pred,pcm,nibbles);
}
extern "C" void WicReportNativeAudio(uint64_t frame) noexcept {
    wiicompiled::vita::assets::ReportAudio(frame);
}
