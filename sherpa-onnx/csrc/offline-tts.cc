// sherpa-onnx/csrc/offline-tts.cc
//
// Copyright (c)  2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/offline-tts.h"

#include <string>
#include <utility>

#if __ANDROID_API__ >= 9
#include "android/asset_manager.h"
#include "android/asset_manager_jni.h"
#endif

#if __OHOS__
#include "rawfile/raw_file_manager.h"
#endif

#include "sherpa-onnx/csrc/file-utils.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/offline-tts-cache-mechanism.h"
#include "sherpa-onnx/csrc/offline-tts-impl.h"
#include "sherpa-onnx/csrc/text-utils.h"

namespace sherpa_onnx {

void OfflineTtsConfig::Register(ParseOptions *po) {
  model.Register(po);

  po->Register("tts-rule-fsts", &rule_fsts,
               "It not empty, it contains a list of rule FST filenames."
               "Multiple filenames are separated by a comma and they are "
               "applied from left to right. An example value: "
               "rule1.fst,rule2.fst,rule3.fst");

  po->Register("tts-rule-fars", &rule_fars,
               "It not empty, it contains a list of rule FST archive filenames."
               "Multiple filenames are separated by a comma and they are "
               "applied from left to right. An example value: "
               "rule1.far,rule2.far,rule3.far. Note that an *.far can contain "
               "multiple *.fst files");

  po->Register(
      "tts-max-num-sentences", &max_num_sentences,
      "Maximum number of sentences that we process at a time. "
      "This is to avoid OOM for very long input text. "
      "If you set it to -1, then we process all sentences in a single batch.");
}

bool OfflineTtsConfig::Validate() const {
  if (!rule_fsts.empty()) {
    std::vector<std::string> files;
    SplitStringToVector(rule_fsts, ",", false, &files);
    for (const auto &f : files) {
      if (!FileExists(f)) {
        SHERPA_ONNX_LOGE("Rule fst '%s' does not exist. ", f.c_str());
        return false;
      }
    }
  }

  if (!rule_fars.empty()) {
    std::vector<std::string> files;
    SplitStringToVector(rule_fars, ",", false, &files);
    for (const auto &f : files) {
      if (!FileExists(f)) {
        SHERPA_ONNX_LOGE("Rule far '%s' does not exist. ", f.c_str());
        return false;
      }
    }
  }

  return model.Validate();
}

std::string OfflineTtsConfig::ToString() const {
  std::ostringstream os;

  os << "OfflineTtsConfig(";
  os << "model=" << model.ToString() << ", ";
  os << "rule_fsts=\"" << rule_fsts << "\", ";
  os << "rule_fars=\"" << rule_fars << "\", ";
  os << "max_num_sentences=" << max_num_sentences << ")";

  return os.str();
}

OfflineTts::OfflineTts(const OfflineTtsConfig &config)
    : impl_(OfflineTtsImpl::Create(config)) {}

OfflineTts::OfflineTts(const OfflineTtsConfig &config,
                        const OfflineTtsCacheMechanismConfig &cache_config)
    : impl_(OfflineTtsImpl::Create(config)) {
     cache_mechanism_ = std::make_unique<OfflineTtsCacheMechanism>(cache_config);
}

template <typename Manager>
OfflineTts::OfflineTts(Manager *mgr, const OfflineTtsConfig &config)
    : impl_(OfflineTtsImpl::Create(mgr, config)) {}

template <typename Manager>
OfflineTts::OfflineTts(Manager *mgr, const OfflineTtsConfig &config,
                        const OfflineTtsCacheMechanismConfig &cache_config)
    : impl_(OfflineTtsImpl::Create(mgr, config)) {
     cache_mechanism_ = std::make_unique<OfflineTtsCacheMechanism>(cache_config);
}

OfflineTts::~OfflineTts() = default;

GeneratedAudio OfflineTts::Generate(
    const std::string &text, int64_t sid /*=0*/, float speed /*= 1.0*/,
    GeneratedAudioCallback callback /*= nullptr*/) const {
  // Generate a hash for the text
  std::hash<std::string> hasher;
  std::size_t text_hash = hasher(text);

  // Check if the cache mechanism is active and if the audio is already cached
  if (cache_mechanism_) {
    int32_t sample_rate;
    std::vector<float> samples
      = cache_mechanism_->GetWavFile(text_hash, &sample_rate);

    if (!samples.empty()) {
      SHERPA_ONNX_LOGE("Returning cached audio for hash: %zu", text_hash);

      // If a callback is provided, call it with the cached audio
      if (callback) {
        int32_t result
          = callback(samples.data(), samples.size(), 1.0f /* progress */);
        if (result == 0) {
          // If the callback returns 0, stop further processing
          SHERPA_ONNX_LOGE("Callback requested to stop processing.");
          return {samples, sample_rate};
        }
      }

      // Return the cached audio
      return {samples, sample_rate};
    }
  }

  // Generate the audio if not cached
  GeneratedAudio audio = impl_->Generate(text, sid, speed, std::move(callback));

  // Cache the generated audio if the cache mechanism is active
  if (cache_mechanism_) {
    cache_mechanism_->AddWavFile(text_hash, audio.samples, audio.sample_rate);
  }

  return audio;
}

int32_t OfflineTts::SampleRate() const { return impl_->SampleRate(); }

int32_t OfflineTts::NumSpeakers() const { return impl_->NumSpeakers(); }

#if __ANDROID_API__ >= 9
template OfflineTts::OfflineTts(AAssetManager *mgr,
                                const OfflineTtsConfig &config);
template OfflineTts::OfflineTts(AAssetManager *mgr,
                        const OfflineTtsConfig &config,
                        const OfflineTtsCacheMechanismConfig &cache_config);
#endif

#if __OHOS__
template OfflineTts::OfflineTts(NativeResourceManager *mgr,
                                const OfflineTtsConfig &config);
template OfflineTts::OfflineTts(NativeResourceManager *mgr,
                        const OfflineTtsConfig &config,
                        const OfflineTtsCacheMechanismConfig &cache_config);
#endif

}  // namespace sherpa_onnx
