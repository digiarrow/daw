#pragma once

#include "waveform/automation.hpp"

#include <cstdint>

namespace waveform
{

enum class EffectKind
{
    Eq,
    Compressor,
    Delay,
    Reverb,
    Limiter,
    Utility,
    Chorus,
    Saturator,
    AutoFilter,
    Phaser,
    Flanger,
    Gate,
    Glue,
    Redux,
    Spectrum
};

inline constexpr int kMaxInserts = 4;
inline constexpr int kDelayCapacity = 8192;

struct EffectSlot
{
    EffectKind kind = EffectKind::Utility;
    bool bypass = false;
    double p0 = 0.0;
    double p1 = 0.0;
    double p2 = 0.0;
    double p3 = 0.0;
    double p4 = 0.0;
    double p5 = 0.0;
};

/// Fixed storage so the audio thread never allocates. Delay time is capped at kDelayCapacity frames.
struct EffectState
{
    float delayL[kDelayCapacity] {};
    float delayR[kDelayCapacity] {};
    int write = 0;
    float env = 1.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;
    float combL[4][1024] {};
    float combR[4][1024] {};
    int combPos = 0;
    float* externalLeft = nullptr;
    float* externalRight = nullptr;
    int externalCapacity = 0;
};

[[nodiscard]] EffectSlot defaultEffect (EffectKind kind) noexcept;
[[nodiscard]] const char* effectName (EffectKind kind) noexcept;
[[nodiscard]] EffectKind effectFromName (const char* name) noexcept;

void resetEffect (EffectState& state) noexcept;

/// In-place stereo processing. Does not allocate.
/// For a compressor, `sidechainLeft`/`sidechainRight` replace the detector when both are set.
void processEffect (const EffectSlot& slot,
                    EffectState& state,
                    float* left,
                    float* right,
                    int frames,
                    double sampleRate,
                    const float* sidechainLeft = nullptr,
                    const float* sidechainRight = nullptr) noexcept;

void processInserts (const EffectSlot* slots,
                     int slotCount,
                     EffectState* states,
                     float* left,
                     float* right,
                     int frames,
                     double sampleRate,
                     const float* sidechainLeft = nullptr,
                     const float* sidechainRight = nullptr) noexcept;

void setEffectParameter (EffectSlot& slot, int parameter, double value) noexcept;

/// One sample at a time so an effect lane can change p0..p5 on every frame.
/// `lanes`/`laneCount` name parameters such as `effect.0.p0`. Other lanes are ignored.
void processAutomatedInserts (const EffectSlot* slots,
                              int slotCount,
                              EffectState* states,
                              float* left,
                              float* right,
                              int frames,
                              double sampleRate,
                              const float* sidechainLeft,
                              const float* sidechainRight,
                              const AutomationLane* lanes,
                              int laneCount,
                              double startQuarter,
                              double bpm) noexcept;

} // namespace waveform
