#include "CrateChopPlugin.hpp"
#include <cstdio>
#include <cstdlib>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

START_NAMESPACE_DISTRHO

using namespace chop;

CrateChopPlugin::CrateChopPlugin()
    : Plugin(kNumParams, 0, 1)
{
    for (int i = 0; i < kNumParams; ++i) fValues[i] = defaultValue(i);
    fEngine.setSampleRate(getSampleRate());
#ifdef CRATECHOP_PREVIEW
    if (const char* p = std::getenv("CRATECHOP_PREVIEW")) setState("sample", p);
#endif
}

static ParameterEnumerationValue* makeEnum(std::initializer_list<const char*> labels)
{
    ParameterEnumerationValue* v = new ParameterEnumerationValue[labels.size()];
    int i = 0;
    for (const char* l : labels) { v[i].value = float(i); v[i].label = l; ++i; }
    return v;
}

void CrateChopPlugin::initParameter(uint32_t index, Parameter& p)
{
    const Info& in = info(int(index));
    char name[64], sym[64];
    if (isPadParam(int(index))) {
        std::snprintf(name, sizeof(name), "Pad %d %s", padOf(int(index)) + 1, in.name);
        std::snprintf(sym, sizeof(sym), "p%d_%s", padOf(int(index)) + 1, in.symbol);
    } else {
        std::snprintf(name, sizeof(name), "%s", in.name);
        std::snprintf(sym, sizeof(sym), "%s", in.symbol);
    }
    p.name = name;
    p.symbol = sym;
    p.unit = in.unit;
    p.ranges.min = in.min;
    p.ranges.max = in.max;
    p.ranges.def = defaultValue(int(index));
    p.hints = kParameterIsAutomatable;
    if (in.isInt) p.hints |= kParameterIsInteger;
    if (in.isLog) p.hints |= kParameterIsLogarithmic;
    if (in.isBool) p.hints |= kParameterIsBoolean;

    auto setEnum = [&](std::initializer_list<const char*> l) {
        p.enumValues.count = uint8_t(l.size());
        p.enumValues.restrictedMode = true;
        p.enumValues.values = makeEnum(l);
    };
    if (index == gSliceMode) setEnum({ "Transient", "Grid" });
    else if (index == gKeysMode) setEnum({ "Pads", "Chromatic" });
    else if (index == gInterp) setEnum({ "Clean", "Vintage" });
    else if (isPadParam(int(index)) && ppOf(int(index)) == ppMode) setEnum({ "One-shot", "Gate" });
    else if (isPadParam(int(index)) && ppOf(int(index)) == ppChoke) setEnum({ "Off", "1", "2", "3", "4" });
}

void CrateChopPlugin::initState(uint32_t index, State& state)
{
    if (index != 0) return;
    state.key = "sample";
    state.label = "Sample";
    state.defaultValue = "";
    state.hints = kStateIsFilenamePath;
}

void CrateChopPlugin::setState(const char* key, const char* value)
{
    if (std::strcmp(key, "sample") != 0) return;

    std::shared_ptr<SampleData> fresh;
    std::string err;
    if (value != nullptr && value[0] != '\0') {
        fresh = loadSampleFile(value, err);
        if (!fresh) {
            std::lock_guard<std::mutex> lk(fDisplayMutex);
            fDisplay.error = std::string("Could not load: ") + err;
            ++fDisplay.version;
            return; // keep the current sample
        }
    }

    {
        std::lock_guard<std::mutex> lk(fAudioMutex);
        fEngine.setSample(fresh.get());
        fSample.swap(fresh);
        fEngine.updateSlices();
    }
    {
        std::lock_guard<std::mutex> lk(fDisplayMutex);
        fDisplay.sample = fSample;
        fDisplay.error.clear();
    }
    publishSlices(true);
    // old sample (now in `fresh`) is released here, outside of any lock
}

void CrateChopPlugin::publishSlices(bool blocking)
{
    std::unique_lock<std::mutex> lk(fDisplayMutex, std::defer_lock);
    if (blocking) lk.lock();
    else if (!lk.try_lock()) return;
    fDisplay.numSlices = fEngine.numSlices;
    for (int i = 0; i <= fEngine.numSlices; ++i) fDisplay.sliceStart[i] = fEngine.sliceStart[i];
    ++fDisplay.version;
    fPublishedSliceVersion = fEngine.sliceVersion;
}

float CrateChopPlugin::getParameterValue(uint32_t index) const
{
    return index < uint32_t(kNumParams) ? fValues[index] : 0.f;
}

void CrateChopPlugin::setParameterValue(uint32_t index, float value)
{
    if (index >= uint32_t(kNumParams)) return;
    fValues[index] = value;
    fEngine.setParam(int(index), value);
}

void CrateChopPlugin::activate()
{
    fEngine.allNotesOff(true);
}

void CrateChopPlugin::sampleRateChanged(double sr)
{
    std::lock_guard<std::mutex> lk(fAudioMutex);
    fEngine.setSampleRate(sr);
}

void CrateChopPlugin::handleMidi(const MidiEvent& ev)
{
    if (ev.size > 3 || ev.size < 2) return;
    const uint8_t st = ev.data[0] & 0xF0;
    const uint8_t d1 = ev.data[1], d2 = ev.size > 2 ? ev.data[2] : 0;
    if (st == 0x90) fEngine.noteOn(d1, d2);
    else if (st == 0x80) fEngine.noteOff(d1);
    else if (st == 0xB0 && (d1 == 120 || d1 == 123)) fEngine.allNotesOff(d1 == 120);
}

void CrateChopPlugin::run(const float**, float** outputs, uint32_t frames,
                          const MidiEvent* midiEvents, uint32_t midiEventCount)
{
    float* outL = outputs[0];
    float* outR = outputs[1];

    if (!fAudioMutex.try_lock()) {
        // a new sample is being swapped in right now: output silence for this block
        std::memset(outL, 0, sizeof(float) * frames);
        std::memset(outR, 0, sizeof(float) * frames);
        return;
    }

#if defined(__SSE__) || defined(__x86_64__)
    const unsigned int csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040);
#endif

    uint32_t pos = 0, ev = 0;
    while (pos < frames) {
        while (ev < midiEventCount && midiEvents[ev].frame <= pos) handleMidi(midiEvents[ev++]);
        uint32_t next = frames;
        if (ev < midiEventCount) next = std::min(frames, std::max(pos + 1, midiEvents[ev].frame));
        fEngine.render(outL + pos, outR + pos, next - pos);
        pos = next;
    }
    while (ev < midiEventCount) handleMidi(midiEvents[ev++]);

#if defined(__SSE__) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif

    if (fEngine.sliceVersion != fPublishedSliceVersion) publishSlices(false);
    fAudioMutex.unlock();
}

Plugin* createPlugin() { return new CrateChopPlugin(); }

END_NAMESPACE_DISTRHO
