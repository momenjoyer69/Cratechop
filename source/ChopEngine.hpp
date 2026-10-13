// CrateChop - sampler engine (framework independent)
#pragma once

#include "ChopParams.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace chop {

// ---------------------------------------------------------------------------------------------
// Sample data + analysis (built off the audio thread, immutable afterwards)

static constexpr int kOverviewBins = 2048;

struct SampleData {
    std::vector<float> L, R;
    uint64_t frames = 0;
    double sr = 44100.0;
    std::string path, name;

    // onset detection function (one value per hop)
    std::vector<float> odf;
    uint32_t hop = 256;
    float odfMax = 0.f;

    // waveform overview for the UI
    float ovMin[kOverviewBins], ovMax[kOverviewBins];

    inline float mono(int64_t i) const { return 0.5f * (L[size_t(i)] + R[size_t(i)]); }
};

// decode + analyse a file; returns nullptr and fills err on failure
std::shared_ptr<SampleData> loadSampleFile(const char* path, std::string& err);
void analyseSample(SampleData& s);

// ---------------------------------------------------------------------------------------------
// Engine

class Engine {
public:
    Engine();

    void setSampleRate(double sr);
    void setParam(int idx, float v);
    float getParam(int idx) const { return params[idx]; }

    // call only while the audio thread is not running render()
    void setSample(SampleData* s);
    SampleData* getSample() const { return sample; }

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff(bool hard);

    void render(float* outL, float* outR, uint32_t n);

    // slices (recomputed lazily on the audio thread)
    bool updateSlices();                // returns true if slices changed
    int numSlices = 0;
    uint64_t sliceStart[kMaxSlices + 1] = {};
    uint32_t sliceVersion = 0;

    // UI feedback
    std::atomic<uint32_t> padTrig[kNumPads];
    std::atomic<int> playPad { -1 };
    std::atomic<int64_t> playPos { -1 };

private:
    struct Voice {
        bool active = false;
        int pad = 0, note = -1;
        bool gate = false, releasing = false;
        uint32_t age = 0;
        double pos = 0, inc = 1, start = 0, end = 0;
        int dir = 1;
        float gL = 1, gR = 1;
        float env = 0, attInc = 1, relCoef = 0.99f;
        float fadeSamples = 100;
        bool filt = false;
        float fa1 = 0, fa2 = 0, fa3 = 0, fk = 1.414f;
        float ic1[2] = {0, 0}, ic2[2] = {0, 0};
    };

    static constexpr int kMaxVoices = 32;
    Voice voices[kMaxVoices];
    uint32_t ageCounter = 0;

    double sr = 44100.0;
    float params[kNumParams];
    SampleData* sample = nullptr;
    bool slicesDirty = true;

    // master section state
    float masterGain = 1.f, masterGainTarget = 1.f, smoothK = 0.001f;
    double holdPhase = 1.0, holdStep = 1.0; float held[2] = {0, 0};
    bool rateActive = false, bitsActive = false; float qLevels = 32768.f;
    float driveGain = 1.f, driveComp = 1.f, driveBlend = 0.f;
    bool toneActive = false;
    struct BQ { float b0=1,b1=0,b2=0,a1=0,a2=0; float z1[2]={0,0}, z2[2]={0,0}; } lp1, lp2;
    bool masterDirty = true;

    void updateMaster();
    void startVoice(int pad, int note, int velocity, float extraSemis);
    float readSample(const float* buf, double pos, bool vintage) const;
    uint64_t snapZeroBack(uint64_t pos, uint64_t maxBack) const;
    uint64_t snapZeroNear(uint64_t pos, uint64_t radius) const;
};

} // namespace chop
