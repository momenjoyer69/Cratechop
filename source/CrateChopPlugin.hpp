#pragma once

#include "DistrhoPlugin.hpp"
#include "ChopEngine.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

// what the UI is allowed to look at (guarded by displayMutex, never held by the audio thread for long)
struct ChopDisplay {
    std::shared_ptr<chop::SampleData> sample;
    int numSlices = 0;
    uint64_t sliceStart[chop::kMaxSlices + 1] = {};
    uint32_t version = 0;
    std::string error;
};

class CrateChopPlugin : public Plugin
{
public:
    CrateChopPlugin();

    // ---- accessors for the UI (direct access)
    std::mutex& displayMutex() { return fDisplayMutex; }
    const ChopDisplay& display() const { return fDisplay; }
    uint32_t padTrigCount(int pad) const { return fEngine.padTrig[pad].load(); }
    int playPad() const { return fEngine.playPad.load(); }
    int64_t playPos() const { return fEngine.playPos.load(); }

protected:
    const char* getLabel() const override       { return "CrateChop"; }
    const char* getDescription() const override { return "MPC-style chop sampler with SP crunch."; }
    const char* getMaker() const override       { return "Crate Audio"; }
    const char* getHomePage() const override    { return "https://example.com/cratechop"; }
    const char* getLicense() const override     { return "ISC"; }
    uint32_t getVersion() const override        { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override        { return d_cconst('C', 'r', 'C', 'h'); }

    void initParameter(uint32_t index, Parameter& p) override;
    void initState(uint32_t index, State& state) override;
    void setState(const char* key, const char* value) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;
    void activate() override;
    void sampleRateChanged(double sr) override;
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override;

private:
    chop::Engine fEngine;
    float fValues[chop::kNumParams];

    std::mutex fAudioMutex;        // guards fEngine's sample pointer swap
    std::shared_ptr<chop::SampleData> fSample;

    std::mutex fDisplayMutex;
    ChopDisplay fDisplay;
    uint32_t fPublishedSliceVersion = 0;

    void publishSlices(bool blocking);
    void handleMidi(const MidiEvent& ev);

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrateChopPlugin)
};

END_NAMESPACE_DISTRHO
