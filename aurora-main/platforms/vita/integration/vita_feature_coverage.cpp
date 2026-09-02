#include "vita_feature_coverage.hpp"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

namespace aurora::vita::integration {

const char* feature_class_name(FeatureClass cls) noexcept {
  switch (cls) {
    case FeatureClass::Primitive: return "primitive";
    case FeatureClass::VertexFormat: return "vertex-format";
    case FeatureClass::TextureFormat: return "texture-format";
    case FeatureClass::TevProgram: return "tev-program";
    case FeatureClass::IndirectTev: return "indirect-tev";
    case FeatureClass::TexGen: return "texgen";
    case FeatureClass::Lighting: return "lighting";
    case FeatureClass::Fog: return "fog";
    case FeatureClass::EfbCopy: return "efb-copy";
    case FeatureClass::Fallback: return "fallback";
    case FeatureClass::Unsupported: return "unsupported";
  }
  return "unknown";
}

uint64_t FeatureCoverage::composite_key(FeatureClass featureClass, uint64_t key) noexcept {
  uint64_t x = key ^ (static_cast<uint64_t>(featureClass) * 0x9e3779b97f4a7c15ull);
  x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
  x ^= x >> 27; x *= 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}
void FeatureCoverage::reset() { records_.clear(); totalObservations_ = fallbackCount_ = unsupportedCount_ = 0; }
bool FeatureCoverage::observe(FeatureClass featureClass, uint64_t key, std::string_view description) {
  ++totalObservations_;
  const uint64_t ck = composite_key(featureClass, key);
  auto [it, inserted] = records_.try_emplace(ck, FeatureRecord{featureClass,key,0,std::string(description)});
  ++it->second.count;
  if (it->second.description.empty() && !description.empty()) it->second.description.assign(description);
  return inserted;
}
void FeatureCoverage::fallback(uint64_t key, std::string_view description) { ++fallbackCount_; observe(FeatureClass::Fallback,key,description); }
void FeatureCoverage::unsupported(uint64_t key, std::string_view description) { ++unsupportedCount_; observe(FeatureClass::Unsupported,key,description); }
std::string FeatureCoverage::report() const {
  std::vector<const FeatureRecord*> sorted;
  sorted.reserve(records_.size());
  for (const auto& [_, rec] : records_) sorted.push_back(&rec);
  std::sort(sorted.begin(), sorted.end(), [](const FeatureRecord* a, const FeatureRecord* b) {
    if (a->featureClass != b->featureClass) return static_cast<unsigned>(a->featureClass) < static_cast<unsigned>(b->featureClass);
    return a->key < b->key;
  });
  std::ostringstream out;
  out << "[AURORA-VITA][COVERAGE] observations=" << totalObservations_ << " unique=" << records_.size()
      << " fallbacks=" << fallbackCount_ << " unsupported=" << unsupportedCount_ << '\n';
  for (const auto* rec : sorted) {
    out << "[AURORA-VITA][FEATURE] class=" << feature_class_name(rec->featureClass)
        << " key=0x" << std::hex << rec->key << std::dec << " count=" << rec->count;
    if (!rec->description.empty()) out << " desc=" << rec->description;
    out << '\n';
  }
  return out.str();
}
bool FeatureCoverage::write_report(const char* path) const noexcept {
  if (!path || !*path) return false;
  FILE* fp = std::fopen(path,"wb"); if (!fp) return false;
  const auto text=report(); const bool ok=std::fwrite(text.data(),1,text.size(),fp)==text.size();
  std::fclose(fp); return ok;
}

} // namespace aurora::vita::integration
