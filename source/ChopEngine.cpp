#include "ChopEngine.hpp"

#include <cstdio>
#include <cmath>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace chop {

// =============================================================================================
// file loading

static bool readWholeFile(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = nullptr;
#ifdef _WIN32
    // paths from the UI are UTF-8 -> use the wide API so names with diacritics work
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen > 0) {
        std::vector<wchar_t> wpath(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), wlen);
        f = _wfopen(wpath.data(), L"rb");
    }
#else
    f = std::fopen(path, "rb");
#endif
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1024L * 1024L * 1024L) { std::fclose(f); return false; }
    out.resize(size_t(size));
    const size_t got = std::fread(out.data(), 1, size_t(size), f);
    std::fclose(f);
    return got == size_t(size);
}

static std::string baseName(const char* path)
{
    std::string p(path);
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::shared_ptr<SampleData> loadSampleFile(const char* path, std::string& err)
{
    std::vector<uint8_t> bytes;
    if (!readWholeFile(path, bytes)) { err = "cannot open file"; return nullptr; }

    float* pcm = nullptr;
    unsigned int channels = 0, rate = 0;
    uint64_t frames = 0;
    int which = 0;

    {
        drwav_uint64 n = 0;
        pcm = drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &rate, &n, nullptr);
        if (pcm) { frames = n; which = 1; }
    }
    if (!pcm) {
        drflac_uint64 n = 0;
        pcm = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &rate, &n, nullptr);
        if (pcm) { frames = n; which = 2; }
    }
    if (!pcm) {
        drmp3_config cfg = {};
        drmp3_uint64 n = 0;
        pcm = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &cfg, &n, nullptr);
        if (pcm) { frames = n; channels = cfg.channels; rate = cfg.sampleRate; which = 3; }
    }
    if (!pcm || channels == 0 || rate == 0 || frames < 64) {
        if (pcm) { if (which == 1) drwav_free(pcm, nullptr); else if (which == 2) drflac_free(pcm, nullptr); else drmp3_free(pcm, nullptr); }
        err = "unsupported or empty audio file";
        return nullptr;
    }

    const uint64_t maxFrames = uint64_t(rate) * 600; // 10 minutes is plenty for sampling
    if (frames > maxFrames) frames = maxFrames;

    auto s = std::make_shared<SampleData>();
    s->frames = frames;
    s->sr = rate;
    s->path = path;
    s->name = baseName(path);
    s->L.resize(size_t(frames));
    s->R.resize(size_t(frames));
    for (uint64_t i = 0; i < frames; ++i) {
        const float* fr = pcm + i * channels;
        s->L[size_t(i)] = fr[0];
        s->R[size_t(i)] = channels > 1 ? fr[1] : fr[0];
    }
    if (which == 1) drwav_free(pcm, nullptr); else if (which == 2) drflac_free(pcm, nullptr); else drmp3_free(pcm, nullptr);

    analyseSample(*s);
    return s;
}

void analyseSample(SampleData& s)
{
    // overview
    for (int b = 0; b < kOverviewBins; ++b) {
        const uint64_t a = s.frames * uint64_t(b) / kOverviewBins;
        uint64_t e = s.frames * uint64_t(b + 1) / kOverviewBins;
        if (e <= a) e = a + 1;
        float mn = 0.f, mx = 0.f;
        // stride for very long files keeps this fast
        const uint64_t step = std::max<uint64_t>(1, (e - a) / 512);
        for (uint64_t i = a; i < e && i < s.frames; i += step) {
            const float l = s.L[size_t(i)], r = s.R[size_t(i)];
            mn = std::min(mn, std::min(l, r));
            mx = std::max(mx, std::max(l, r));
        }
        s.ovMin[b] = mn; s.ovMax[b] = mx;
    }

    // onset detection function: rise of short-term energy (dB), weighted by loudness
    s.hop = std::max<uint32_t>(64, uint32_t(s.sr * 0.005));
    const uint32_t hop = s.hop, win = hop * 2;
    const size_t M = size_t(s.frames / hop);
    std::vector<float> rms(M, 0.f);
    for (size_t k = 0; k < M; ++k) {
        double e = 0.0; uint32_t cnt = 0;
        for (uint64_t i = uint64_t(k) * hop; i < uint64_t(k) * hop + win && i < s.frames; ++i, ++cnt) {
            const float m = s.mono(int64_t(i)); e += double(m) * m;
        }
        rms[k] = float(std::sqrt(e / std::max<uint32_t>(1, cnt)));
    }
    // onset strength = linear rise in level, scaled by how sharp the relative jump is
    // (so a snare on top of a kick tail counts, but slow swells in loud material don't)
    s.odf.assign(M, 0.f);
    s.odfMax = 0.f;
    for (size_t k = 2; k < M; ++k) {
        const float prev = std::max(rms[k - 1], rms[k - 2]);
        const float rise = rms[k] - prev;
        if (rise <= 0.f) continue;
        const float jumpDb = 20.f * std::log10((rms[k] + 1e-6f) / (prev + 1e-6f));
        const float sharp = std::min(1.f, jumpDb / 6.f);
        s.odf[k] = rise * sharp;
        s.odfMax = std::max(s.odfMax, s.odf[k]);
    }
}

// =============================================================================================
// engine

Engine::Engine()
{
    for (int i = 0; i < kNumParams; ++i) params[i] = defaultValue(i);
    for (int i = 0; i < kNumPads; ++i) padTrig[i] = 0;
    setSampleRate(44100.0);
}

void Engine::setSampleRate(double s)
{
    sr = s;
    smoothK = float(1.0 - std::exp(-1.0 / (0.02 * sr)));
    masterDirty = true;
    updateMaster();
    masterGain = masterGainTarget;
    allNotesOff(true);
}

void Engine::setParam(int idx, float v)
{
    if (idx < 0 || idx >= kNumParams) return;
    params[idx] = v;
    if (idx == gSliceMode || idx == gSlices || idx == gSensitivity) slicesDirty = true;
    if (idx == gBits || idx == gRate || idx == gDrive || idx == gTone || idx == gVolume) masterDirty = true;
}

void Engine::setSample(SampleData* s)
{
    allNotesOff(true);
    sample = s;
    slicesDirty = true;
}

// ---------------------------------------------------------------------------------- slicing

uint64_t Engine::snapZeroBack(uint64_t pos, uint64_t maxBack) const
{
    if (!sample || pos == 0) return pos;
    const uint64_t lo = pos > maxBack ? pos - maxBack : 1;
    for (uint64_t i = pos; i >= lo; --i) {
        const float a = sample->mono(int64_t(i - 1)), b = sample->mono(int64_t(i));
        if ((a <= 0.f && b >= 0.f) || (a >= 0.f && b <= 0.f)) return i;
        if (i == lo) break;
    }
    return pos;
}

uint64_t Engine::snapZeroNear(uint64_t pos, uint64_t radius) const
{
    if (!sample || pos == 0) return pos;
    for (uint64_t d = 0; d <= radius; ++d) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const int64_t i = int64_t(pos) + sgn * int64_t(d);
            if (i < 1 || uint64_t(i) >= sample->frames) continue;
            const float a = sample->mono(i - 1), b = sample->mono(i);
            if ((a <= 0.f && b >= 0.f) || (a >= 0.f && b <= 0.f)) return uint64_t(i);
        }
    }
    return pos;
}

bool Engine::updateSlices()
{
    if (!slicesDirty) return false;
    slicesDirty = false;
    ++sliceVersion;

    if (!sample) { numSlices = 0; return true; }

    const SampleData& s = *sample;
    const int N = std::max(1, std::min(kMaxSlices, int(std::lround(params[gSlices]))));
    const bool grid = params[gSliceMode] > 0.5f;
    uint64_t pos[kMaxSlices];
    int count = 0;

    if (grid || s.odf.size() < 4 || s.odfMax <= 0.f) {
        for (int i = 0; i < N; ++i)
            pos[count++] = i == 0 ? 0 : snapZeroNear(s.frames * uint64_t(i) / uint64_t(N), uint64_t(s.sr * 0.001));
    } else {
        const float sens = params[gSensitivity] * 0.01f;
        const float thr = s.odfMax * (0.5f - 0.47f * sens);
        const size_t M = s.odf.size();
        const size_t minDist = std::max<size_t>(1, size_t(0.08 * s.sr / s.hop));
        size_t picks[kMaxSlices];
        int np = 1; picks[0] = 0;

        while (np < N) {
            long best = -1; float bestVal = thr;
            for (size_t k = 1; k + 1 < M; ++k) {
                const float v = s.odf[k];
                if (v <= bestVal || v < s.odf[k - 1] || v < s.odf[k + 1]) continue;
                bool ok = true;
                for (int j = 0; j < np; ++j) {
                    const size_t d = k > picks[j] ? k - picks[j] : picks[j] - k;
                    if (d < minDist) { ok = false; break; }
                }
                if (ok) { best = long(k); bestVal = v; }
            }
            if (best < 0) break;
            picks[np++] = size_t(best);
        }
        std::sort(picks, picks + np);

        for (int j = 0; j < np; ++j) {
            if (picks[j] == 0) { pos[count++] = 0; continue; }
            // refine: find the actual attack inside the analysis window
            const uint64_t a = uint64_t(picks[j]) * s.hop;
            const uint64_t b = std::min<uint64_t>(s.frames, a + uint64_t(s.hop) * 3);
            float peak = 0.f;
            for (uint64_t i = a; i < b; ++i) peak = std::max(peak, std::fabs(s.mono(int64_t(i))));
            uint64_t onset = a;
            for (uint64_t i = a; i < b; ++i) if (std::fabs(s.mono(int64_t(i))) >= 0.3f * peak) { onset = i; break; }
            const uint64_t back = uint64_t(s.sr * 0.002);
            onset = onset > back ? onset - back : 0;
            onset = snapZeroBack(onset, uint64_t(s.sr * 0.003));
            if (count > 0 && onset <= pos[count - 1]) continue;
            pos[count++] = onset;
        }
    }

    numSlices = count;
    for (int i = 0; i < count; ++i) sliceStart[i] = pos[i];
    sliceStart[count] = s.frames;
    return true;
}

// ---------------------------------------------------------------------------------- notes

void Engine::startVoice(int pad, int note, int velocity, float extraSemis)
{
    if (!sample) return;
    updateSlices();

    const float* pp = &params[padParam(pad, 0)];
    const int sl = int(std::lround(pp[ppSlice]));
    if (sl < 0 || sl >= numSlices) return;

    const SampleData& s = *sample;
    const double fsr = s.sr;
    double st = double(sliceStart[sl]);
    const double len = double(sliceStart[sl + 1]) - st;
    st += pp[ppNudge] * 0.001 * fsr;
    st = std::max(0.0, std::min(double(s.frames - 1), st));
    double en = std::min(double(s.frames), st + len * pp[ppLength] * 0.01);
    if (en - st < 32) return;

    // find a voice: free one, otherwise steal the oldest
    int vi = -1;
    for (int i = 0; i < kMaxVoices; ++i) if (!voices[i].active) { vi = i; break; }
    if (vi < 0) {
        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < kMaxVoices; ++i) if (voices[i].age < oldest) { oldest = voices[i].age; vi = i; }
    }
    Voice& v = voices[vi];
    v = Voice();
    v.active = true;
    v.pad = pad; v.note = note;
    v.age = ++ageCounter;
    v.gate = pp[ppMode] > 0.5f;
    v.start = st; v.end = en;
    v.dir = pp[ppReverse] > 0.5f ? -1 : 1;
    v.pos = v.dir > 0 ? st : en - 1.0;
    v.inc = (fsr / sr) * std::pow(2.0, (pp[ppPitch] + extraSemis) / 12.0);

    const float velAmt = params[gVelocity] * 0.01f;
    const float velGain = (1.f - velAmt) + velAmt * (float(velocity) / 127.f);
    const float g = std::pow(10.f, pp[ppGain] / 20.f) * velGain;
    const float ang = (pp[ppPan] * 0.01f + 1.f) * 0.25f * 3.14159265f;
    v.gL = g * std::cos(ang) * 1.41421356f;
    v.gR = g * std::sin(ang) * 1.41421356f;

    const float attMs = std::max(0.7f, pp[ppAttack]);
    v.attInc = float(1.0 / (attMs * 0.001 * sr));
    v.relCoef = float(std::exp(-6.9 / (pp[ppRelease] * 0.001 * sr)));
    v.fadeSamples = float(0.002 * sr);

    const float fc = pp[ppCutoff];
    v.filt = fc < 19500.f;
    if (v.filt) {
        const double f = std::min(double(fc), sr * 0.45);
        const float gg = float(std::tan(3.14159265358979 * f / sr));
        v.fk = 1.41421356f;
        v.fa1 = 1.f / (1.f + gg * (gg + v.fk));
        v.fa2 = gg * v.fa1;
        v.fa3 = gg * v.fa2;
    }

    padTrig[pad].fetch_add(1);
    playPad = pad;
}

void Engine::noteOn(int note, int velocity)
{
    if (velocity <= 0) { noteOff(note); return; }
    const float fastRel = float(std::exp(-6.9 / (0.006 * sr)));
    const bool chroma = params[gKeysMode] > 0.5f;

    if (chroma) {
        const int pad = std::max(0, std::min(kNumPads - 1, int(std::lround(params[gChromaPad]))));
        for (Voice& v : voices)
            if (v.active && v.note == note && !v.releasing) { v.releasing = true; v.relCoef = fastRel; }
        startVoice(pad, note, velocity, float(note - 60));
        return;
    }

    const int pad = note - int(std::lround(params[gRootNote]));
    if (pad < 0 || pad >= kNumPads) return;
    const int choke = int(std::lround(params[padParam(pad, ppChoke)]));

    for (Voice& v : voices) {
        if (!v.active) continue;
        const int vc = int(std::lround(params[padParam(v.pad, ppChoke)]));
        if (v.pad == pad || (choke > 0 && vc == choke)) { v.releasing = true; v.relCoef = fastRel; }
    }
    startVoice(pad, note, velocity, 0.f);
}

void Engine::noteOff(int note)
{
    for (Voice& v : voices)
        if (v.active && v.note == note && v.gate && !v.releasing) v.releasing = true;
}

void Engine::allNotesOff(bool hard)
{
    for (Voice& v : voices) {
        if (hard) v.active = false;
        else if (v.active) v.releasing = true;
    }
    if (hard) { playPad = -1; playPos = -1; }
}

// ---------------------------------------------------------------------------------- render

float Engine::readSample(const float* b, double pos, bool vintage) const
{
    const int64_t n = int64_t(sample->frames);
    int64_t i = int64_t(pos);
    if (vintage) return b[std::max<int64_t>(0, std::min(n - 1, i))];
    const float t = float(pos - double(i));
    auto at = [&](int64_t k) { return b[std::max<int64_t>(0, std::min(n - 1, k))]; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    // 4-point Hermite
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void Engine::updateMaster()
{
    masterDirty = false;
    const int bits = int(std::lround(params[gBits]));
    bitsActive = bits < 16;
    qLevels = float(1 << (bits - 1));
    rateActive = params[gRate] < 47900.f && params[gRate] < sr * 0.999;
    holdStep = std::min(1.0, double(params[gRate]) / sr);

    const float d01 = params[gDrive] * 0.01f;
    driveGain = std::pow(10.f, d01 * 24.f / 20.f);
    driveComp = std::pow(10.f, -d01 * 24.f * 0.55f / 20.f);
    driveBlend = std::min(1.f, d01 * 4.f);

    toneActive = params[gTone] < 19900.f && params[gTone] < sr * 0.45;
    if (toneActive) {
        auto setLP = [&](BQ& q, double f, double Q) {
            const double w = 2 * 3.14159265358979 * f / sr, c = std::cos(w), al = std::sin(w) / (2 * Q);
            const double a0 = 1 + al;
            q.b0 = float((1 - c) / 2 / a0); q.b1 = float((1 - c) / a0); q.b2 = q.b0;
            q.a1 = float(-2 * c / a0); q.a2 = float((1 - al) / a0);
        };
        const double f = std::min(double(params[gTone]), sr * 0.45);
        setLP(lp1, f, 0.54); setLP(lp2, f, 1.31);
    }
    masterGainTarget = std::pow(10.f, params[gVolume] / 20.f);
}

void Engine::render(float* outL, float* outR, uint32_t n)
{
    std::memset(outL, 0, sizeof(float) * n);
    std::memset(outR, 0, sizeof(float) * n);
    if (masterDirty) updateMaster();
    updateSlices();

    const bool vintage = params[gInterp] > 0.5f;
    int newestPad = -1; uint32_t newestAge = 0; double newestPos = -1;

    if (sample) {
        const float* bL = sample->L.data();
        const float* bR = sample->R.data();

        for (Voice& v : voices) {
            if (!v.active) continue;
            for (uint32_t i = 0; i < n; ++i) {
                // envelope
                if (v.releasing) {
                    v.env *= v.relCoef;
                    if (v.env < 0.0005f) { v.active = false; break; }
                } else if (v.env < 1.f) {
                    v.env = std::min(1.f, v.env + v.attInc);
                }
                // boundary fade
                const double remain = v.dir > 0 ? (v.end - v.pos) : (v.pos - v.start);
                if (remain <= 0.0) { v.active = false; break; }
                const float edge = std::min(1.f, float(remain / v.inc) / v.fadeSamples);

                float l = readSample(bL, v.pos, vintage);
                float r = readSample(bR, v.pos, vintage);

                if (v.filt) {
                    float* io[2] = { &l, &r };
                    for (int c = 0; c < 2; ++c) {
                        const float x = *io[c];
                        const float v3 = x - v.ic2[c];
                        const float v1 = v.fa1 * v.ic1[c] + v.fa2 * v3;
                        const float v2 = v.ic2[c] + v.fa2 * v.ic1[c] + v.fa3 * v3;
                        v.ic1[c] = 2.f * v1 - v.ic1[c];
                        v.ic2[c] = 2.f * v2 - v.ic2[c];
                        *io[c] = v2;
                    }
                }

                const float a = v.env * edge;
                outL[i] += l * a * v.gL;
                outR[i] += r * a * v.gR;
                v.pos += v.inc * v.dir;
            }
            if (v.active && v.age > newestAge) { newestAge = v.age; newestPad = v.pad; newestPos = v.pos; }
        }
    }

    playPad = newestPad;
    playPos = newestPad >= 0 ? int64_t(newestPos) : -1;

    // master: crunch, drive, tone, volume
    for (uint32_t i = 0; i < n; ++i) {
        float l = outL[i], r = outR[i];

        if (rateActive) {
            holdPhase += holdStep;
            if (holdPhase >= 1.0) {
                holdPhase -= 1.0;
                held[0] = bitsActive ? std::round(l * qLevels) / qLevels : l;
                held[1] = bitsActive ? std::round(r * qLevels) / qLevels : r;
            }
            l = held[0]; r = held[1];
        } else if (bitsActive) {
            l = std::round(l * qLevels) / qLevels; r = std::round(r * qLevels) / qLevels;
        }

        if (driveBlend > 0.f) {
            l += driveBlend * (std::tanh(l * driveGain) * driveComp - l);
            r += driveBlend * (std::tanh(r * driveGain) * driveComp - r);
        }

        if (toneActive) {
            auto run = [](BQ& q, float x, int c) {
                const float y = q.b0 * x + q.z1[c];
                q.z1[c] = q.b1 * x - q.a1 * y + q.z2[c];
                q.z2[c] = q.b2 * x - q.a2 * y;
                return y;
            };
            l = run(lp2, run(lp1, l, 0), 0);
            r = run(lp2, run(lp1, r, 1), 1);
        }

        masterGain += (masterGainTarget - masterGain) * smoothK;
        outL[i] = l * masterGain;
        outR[i] = r * masterGain;
    }
}

} // namespace chop
