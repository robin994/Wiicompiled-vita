#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace aurora::vita::integration {

enum class FeatureClass : uint8_t {
  Primitive,
  VertexFormat,
  TextureFormat,
  TevProgram,
  IndirectTev,
  TexGen,
  Lighting,
  Fog,
  EfbCopy,
  Fallback,
  Unsupported,
};

struct FeatureRecord {
  FeatureClass featureClass = FeatureClass::Unsupported;
  uint64_t key = 0;
  uint64_t count = 0;
  std::string description;
};

class FeatureCoverage {
public:
  void reset();
  bool observe(FeatureClass featureClass, uint64_t key, std::string_view description = {});
  void fallback(uint64_t key, std::string_view description);
  void unsupported(uint64_t key, std::string_view description);
  uint64_t total_observations() const noexcept { return totalObservations_; }
  uint64_t unique_features() const noexcept { return records_.size(); }
  uint64_t fallbacks() const noexcept { return fallbackCount_; }
  uint64_t unsupported_count() const noexcept { return unsupportedCount_; }
  std::string report() const;
  bool write_report(const char* path) const noexcept;
private:
  static uint64_t composite_key(FeatureClass featureClass, uint64_t key) noexcept;
  std::unordered_map<uint64_t, FeatureRecord> records_{};
  uint64_t totalObservations_ = 0;
  uint64_t fallbackCount_ = 0;
  uint64_t unsupportedCount_ = 0;
};

const char* feature_class_name(FeatureClass cls) noexcept;

} // namespace aurora::vita::integration
