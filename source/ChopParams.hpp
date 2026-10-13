// CrateChop - parameter layout (shared by DSP and UI)
#pragma once
#include <cmath>
#include <algorithm>

namespace chop {

static constexpr int kNumPads = 16;
static constexpr int kMaxSlices = 16;

enum GlobalParam {
    gSliceMode = 0, gSlices, gSensitivity, gKeysMode, gChromaPad, gRootNote,
    gInterp, gBits, gRate, gDrive, gTone, gVelocity, gVolume,
    kNumGlobal
};

enum PadParam {
    ppSlice = 0, ppPitch, ppGain, ppPan, ppNudge, ppLength,
    ppAttack, ppRelease, ppCutoff, ppReverse, ppMode, ppChoke,
    kNumPadParams
};

static constexpr int kNumParams = kNumGlobal + kNumPads * kNumPadParams;

inline int padParam(int pad, int pp) { return kNumGlobal + pad * kNumPadParams + pp; }
inline bool isPadParam(int idx) { return idx >= kNumGlobal; }
inline int padOf(int idx) { return (idx - kNumGlobal) / kNumPadParams; }
inline int ppOf(int idx) { return (idx - kNumGlobal) % kNumPadParams; }

struct Info {
    const char* name; const char* symbol; const char* unit;
    float min, max, def; bool isInt, isLog, isBool;
};

static const Info kGlobalInfo[kNumGlobal] = {
    { "Slice Mode",  "slicemode", "",     0,     1,     0,     true,  false, false },
    { "Slices",      "slices",    "",     1,    16,    16,     true,  false, false },
    { "Sensitivity", "sens",      "%",    0,   100,    50,     false, false, false },
    { "Keys Mode",   "keysmode",  "",     0,     1,     0,     true,  false, false },
    { "Chroma Pad",  "chromapad", "",     0,    15,     0,     true,  false, false },
    { "Root Note",   "root",      "",     0,   112,    36,     true,  false, false },
    { "Interp",      "interp",    "",     0,     1,     0,     true,  false, false },
    { "Bits",        "bits",      "bit",  4,    16,    16,     true,  false, false },
    { "Rate",        "rate",      "Hz", 4000, 48000, 48000,    false, true,  false },
    { "Drive",       "drive",     "%",    0,   100,     0,     false, false, false },
    { "Tone",        "tone",      "Hz", 1000, 20000, 20000,    false, true,  false },
    { "Velocity",    "velocity",  "%",    0,   100,    70,     false, false, false },
    { "Volume",      "volume",    "dB", -24,    12,     0,     false, false, false },
};

static const Info kPadInfo[kNumPadParams] = {
    { "Slice",   "slice",   "",     0,    15,     0,    true,  false, false },
    { "Pitch",   "pitch",   "st", -24,    24,     0,    false, false, false },
    { "Gain",    "gain",    "dB", -24,    12,     0,    false, false, false },
    { "Pan",     "pan",     "%", -100,   100,     0,    false, false, false },
    { "Nudge",   "nudge",   "ms", -200,  200,     0,    false, false, false },
    { "Length",  "length",  "%",    1,   200,   100,    false, false, false },
    { "Attack",  "attack",  "ms",   0,   200,     0,    false, false, false },
    { "Release", "release", "ms",   5,  2000,   150,    false, true,  false },
    { "Cutoff",  "cutoff",  "Hz", 200, 20000, 20000,    false, true,  false },
    { "Reverse", "reverse", "",     0,     1,     0,    true,  false, true  },
    { "Mode",    "mode",    "",     0,     1,     0,    true,  false, false },
    { "Choke",   "choke",   "",     0,     4,     0,    true,  false, false },
};

inline const Info& info(int idx) {
    return idx < kNumGlobal ? kGlobalInfo[idx] : kPadInfo[ppOf(idx)];
}

inline float defaultValue(int idx) {
    if (isPadParam(idx) && ppOf(idx) == ppSlice) return float(padOf(idx));
    return info(idx).def;
}

inline float toNorm(int idx, float v) {
    const Info& p = info(idx);
    v = std::min(p.max, std::max(p.min, v));
    if (p.isLog) return std::log(v / p.min) / std::log(p.max / p.min);
    return (v - p.min) / (p.max - p.min);
}

inline float fromNorm(int idx, float n) {
    const Info& p = info(idx);
    n = std::min(1.f, std::max(0.f, n));
    float v = p.isLog ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
    if (p.isInt) v = std::round(v);
    return v;
}

} // namespace chop
