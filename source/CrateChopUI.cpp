#include "DistrhoUI.hpp"
#include "CrateChopPlugin.hpp"

#include <chrono>
#include <cstdio>
#include <random>

START_NAMESPACE_DISTRHO

using namespace chop;

namespace {
    constexpr float W = DISTRHO_UI_DEFAULT_WIDTH;
    constexpr float H = DISTRHO_UI_DEFAULT_HEIGHT;
    constexpr float kPi = 3.14159265f;

    // ---- layout
    struct Rect { float x, y, w, h; bool hit(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; } };

    const Rect kWavePanel  { 15,  64, 1010, 200 };
    const Rect kWave       { 27, 104,  986, 150 };
    const Rect kPadsPanel  { 15, 276,  400, 432 };
    const Rect kEditPanel  {425, 276,  600, 232 };
    const Rect kKeysPanel  {425, 518,  190, 190 };
    const Rect kMasterPanel{625, 518,  400, 190 };
    const Rect kSensSlider {390,  79,  180,  14 };

    constexpr float kPadX0 = 31, kPadY0 = 300, kPadW = 88, kPadH = 92, kPadGapX = 96, kPadGapY = 100;

    // knobs: param < 0 means pad-relative (-(pp+1))
    struct KnobDef { int param; float x, y, r; bool bipolar; const char* label; };
    const KnobDef kKnobs[] = {
        { -(ppSlice + 1),   475, 345, 22, false, "SLICE"   },
        { -(ppPitch + 1),   575, 345, 22, true,  "PITCH"   },
        { -(ppGain + 1),    675, 345, 22, false, "GAIN"    },
        { -(ppPan + 1),     775, 345, 22, true,  "PAN"     },
        { -(ppNudge + 1),   875, 345, 22, true,  "NUDGE"   },
        { -(ppLength + 1),  975, 345, 22, false, "LENGTH"  },
        { -(ppAttack + 1),  475, 450, 22, false, "ATTACK"  },
        { -(ppRelease + 1), 575, 450, 22, false, "RELEASE" },
        { -(ppCutoff + 1),  675, 450, 22, false, "CUTOFF"  },
        { gRootNote,        475, 640, 22, false, "ROOT"    },
        { gVelocity,        565, 640, 22, false, "VELOCITY"},
        { gBits,            667, 640, 22, false, "BITS"    },
        { gRate,            745, 640, 22, false, "RATE"    },
        { gDrive,           823, 640, 22, false, "DRIVE"   },
        { gTone,            901, 640, 22, false, "TONE"    },
        { gVolume,          979, 640, 22, false, "VOLUME"  },
    };
    constexpr int kNumKnobs = sizeof(kKnobs) / sizeof(kKnobs[0]);

    enum BtnKind { kSet, kToggle, kAction };
    enum Actions { aLoad, aDig, aReset, aSlicesDown, aSlicesUp };
    struct ButtonDef { Rect r; int kind; int param; float value; const char* label; };
    const ButtonDef kButtons[] = {
        { {222, 14, 90, 30},  kAction, aLoad, 0, "LOAD" },
        { {840, 14, 80, 30},  kAction, aDig, 0, "DIG" },
        { {930, 14, 90, 30},  kAction, aReset, 0, "RESET" },
        { {27, 72, 96, 24},   kSet, gSliceMode, 0, "TRANSIENT" },
        { {125, 72, 60, 24},  kSet, gSliceMode, 1, "GRID" },
        { {250, 72, 24, 24},  kAction, aSlicesDown, 0, "-" },
        { {306, 72, 24, 24},  kAction, aSlicesUp, 0, "+" },
        { {735, 420, 120, 26},kToggle, -(ppReverse + 1), 0, "REVERSE" },
        { {865, 420, 72, 26}, kSet, -(ppMode + 1), 0, "ONE-SHOT" },
        { {941, 420, 72, 26}, kSet, -(ppMode + 1), 1, "GATE" },
        { {790, 455, 42, 24}, kSet, -(ppChoke + 1), 0, "OFF" },
        { {835, 455, 42, 24}, kSet, -(ppChoke + 1), 1, "1" },
        { {880, 455, 42, 24}, kSet, -(ppChoke + 1), 2, "2" },
        { {925, 455, 42, 24}, kSet, -(ppChoke + 1), 3, "3" },
        { {970, 455, 42, 24}, kSet, -(ppChoke + 1), 4, "4" },
        { {437, 550, 80, 24}, kSet, gKeysMode, 0, "PADS" },
        { {521, 550, 82, 24}, kSet, gKeysMode, 1, "CHROMA" },
        { {637, 550, 70, 24}, kSet, gInterp, 0, "CLEAN" },
        { {709, 550, 80, 24}, kSet, gInterp, 1, "VINTAGE" },
    };
    constexpr int kNumButtons = sizeof(kButtons) / sizeof(kButtons[0]);

    const char* const kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    void noteName(int n, char* buf, size_t sz) { std::snprintf(buf, sz, "%s%d", kNoteNames[((n % 12) + 12) % 12], n / 12); } // FL Studio octave numbering

    // colours
    const Color cOrange(240, 160, 60), cCream(236, 226, 205), cDim(150, 135, 115), cText(220, 208, 186);
}

class CrateChopUI : public UI
{
public:
    CrateChopUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          fRng(uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        for (int i = 0; i < kNumParams; ++i) fValues[i] = defaultValue(i);
        for (int i = 0; i < kNumPads; ++i) { fGlow[i] = 0.f; fLastTrig[i] = 0; }
        loadSharedResources();
        fPlugin = static_cast<CrateChopPlugin*>(static_cast<Plugin*>(getPluginInstancePointer()));

        fScale = float(getScaleFactor());
        if (fScale < 0.5f) fScale = 1.f;
        if (fScale != 1.f) {
            setGeometryConstraints(uint(W * fScale), uint(H * fScale), true);
            setSize(uint(W * fScale), uint(H * fScale));
        } else {
            setGeometryConstraints(uint(W), uint(H), true);
        }
        if (fPlugin) for (int i = 0; i < kNumPads; ++i) fLastTrig[i] = fPlugin->padTrigCount(i);
    }

protected:
    // =================================================================== host -> UI
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= uint32_t(kNumParams) || fValues[index] == value) return;
        fValues[index] = value;
        repaint();
    }

    void stateChanged(const char*, const char*) override { repaint(); }

    void uiIdle() override
    {
        bool dirty = false;
        if (fPlugin) {
            for (int i = 0; i < kNumPads; ++i) {
                const uint32_t t = fPlugin->padTrigCount(i);
                if (t != fLastTrig[i]) { fLastTrig[i] = t; fGlow[i] = 1.f; dirty = true; }
            }
            const int pp = fPlugin->playPad();
            const int64_t pos = fPlugin->playPos();
            if (pp != fShownPlayPad || pos != fShownPlayPos) { fShownPlayPad = pp; fShownPlayPos = pos; dirty = true; }
            std::unique_lock<std::mutex> lk(fPlugin->displayMutex(), std::try_to_lock);
            if (lk.owns_lock() && fPlugin->display().version != fShownVersion) { fShownVersion = fPlugin->display().version; dirty = true; }
        }
        for (int i = 0; i < kNumPads; ++i) {
            if (fGlow[i] > 0.01f) { fGlow[i] *= 0.82f; dirty = true; }
            else fGlow[i] = 0.f;
        }
        if (dirty) repaint();
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename != nullptr && filename[0] != '\0') setState("sample", filename);
        repaint();
    }

    // =================================================================== helpers
    int resolve(int p) const { return p >= 0 ? p : padParam(fSel, -p - 1); }
    float val(int p) const { return fValues[resolve(p)]; }

    void setParam(int idx, float v, bool gesture = true)
    {
        if (gesture) editParameter(uint32_t(idx), true);
        fValues[idx] = v;
        setParameterValue(uint32_t(idx), v);
        if (gesture) editParameter(uint32_t(idx), false);
    }

    void copyDisplay(ChopDisplay& out)
    {
        if (!fPlugin) return;
        std::lock_guard<std::mutex> lk(fPlugin->displayMutex());
        out = fPlugin->display();
    }

    // =================================================================== drawing
    void onNanoDisplay() override
    {
        ChopDisplay d;
        copyDisplay(d);

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        scale(fScale, fScale);

        beginPath(); rect(0, 0, W, H);
        fillPaint(linearGradient(0, 0, 0, H, Color(38, 32, 27), Color(20, 17, 15)));
        fill();

        // header
        fontSize(30); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(cOrange); text(20, 30, "CRATE", nullptr);
        fillColor(cCream);  text(124, 30, "CHOP", nullptr);

        fontSize(12); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        if (!d.error.empty()) { fillColor(Color(240, 90, 70)); text(328, 29, d.error.c_str(), nullptr); }
        else if (d.sample) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s   ·   %.2f s   ·   %.1f kHz", d.sample->name.c_str(),
                          double(d.sample->frames) / d.sample->sr, d.sample->sr / 1000.0);
            fillColor(cText); text(328, 29, buf, nullptr);
        } else { fillColor(cDim); text(328, 29, "no sample loaded", nullptr); }

        for (const Rect* p : { &kWavePanel, &kPadsPanel, &kEditPanel, &kKeysPanel, &kMasterPanel }) drawPanel(*p);
        panelTitle(kPadsPanel, "PADS");
        char title[64];
        const int sl = int(std::lround(fValues[padParam(fSel, ppSlice)]));
        std::snprintf(title, sizeof(title), "PAD %d  ·  SLICE %d", fSel + 1, sl + 1);
        panelTitle(kEditPanel, title);
        panelTitle(kKeysPanel, "KEYS");
        panelTitle(kMasterPanel, "MASTER  ·  SP CRUNCH");

        // toolbar text
        fontSize(10.5f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(cText);
        text(200, 84, "SLICES", nullptr);
        char nb[16]; std::snprintf(nb, sizeof(nb), "%d", int(std::lround(fValues[gSlices])));
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fontSize(13); text(290, 84, nb, nullptr);
        fontSize(10.5f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); text(348, 84, "SENS", nullptr);
        drawSlider(kSensSlider, toNorm(gSensitivity, fValues[gSensitivity]), fValues[gSliceMode] < 0.5f);
        if (d.sample) {
            char fb[32]; std::snprintf(fb, sizeof(fb), "%d found", d.numSlices);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(cOrange); fontSize(10.5f);
            text(584, 84, fb, nullptr);
        }
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE); fillColor(Color(120, 108, 92)); fontSize(10);
        text(1013, 84, "drag = adjust   shift = fine   double-click = reset   click pad = play", nullptr);

        drawWave(d);
        drawPads(d);
        for (int i = 0; i < kNumKnobs; ++i) drawKnob(kKnobs[i], i == fDragKnob, d);
        for (int i = 0; i < kNumButtons; ++i) drawButton(kButtons[i]);

        fontSize(10.5f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(cText);
        text(735, 467, "CHOKE", nullptr);
    }

    void drawPanel(const Rect& p)
    {
        beginPath(); roundedRect(p.x, p.y, p.w, p.h, 8);
        fillPaint(linearGradient(p.x, p.y, p.x, p.y + p.h, Color(52, 45, 38), Color(40, 35, 30)));
        fill();
        strokeColor(Color(70, 61, 51)); strokeWidth(1); stroke();
    }

    void panelTitle(const Rect& p, const char* t)
    {
        fontSize(11); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(cOrange);
        text(p.x + 12, p.y + 14, t, nullptr);
    }

    void drawSlider(const Rect& r, float n, bool enabled)
    {
        beginPath(); roundedRect(r.x, r.y, r.w, r.h, r.h * 0.5f);
        fillColor(Color(25, 21, 18)); fill();
        beginPath(); roundedRect(r.x, r.y, std::max(r.h, r.w * n), r.h, r.h * 0.5f);
        fillColor(enabled ? cOrange : Color(110, 90, 60)); fill();
        beginPath(); circle(r.x + std::max(r.h * 0.5f, r.w * n - r.h * 0.5f), r.y + r.h * 0.5f, r.h * 0.65f);
        fillColor(cCream); fill();
    }

    void drawWave(const ChopDisplay& d)
    {
        const Rect& w = kWave;
        beginPath(); roundedRect(w.x, w.y, w.w, w.h, 4);
        fillColor(Color(18, 15, 13)); fill();

        if (!d.sample) {
            fontSize(15); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(cDim);
            text(w.x + w.w * 0.5f, w.y + w.h * 0.5f - 8, "CLICK HERE OR PRESS LOAD", nullptr);
            fontSize(11); fillColor(Color(120, 108, 92));
            text(w.x + w.w * 0.5f, w.y + w.h * 0.5f + 14, "WAV  ·  AIFF  ·  MP3  ·  FLAC", nullptr);
            return;
        }

        const SampleData& s = *d.sample;
        const double frames = double(s.frames);
        auto fx = [&](double f) { return w.x + float(f / frames) * w.w; };
        const float mid = w.y + w.h * 0.5f, amp = w.h * 0.46f;

        // slice regions
        for (int i = 0; i < d.numSlices; ++i) {
            const float x0 = fx(double(d.sliceStart[i])), x1 = fx(double(d.sliceStart[i + 1]));
            beginPath(); rect(x0, w.y, x1 - x0, w.h);
            fillColor(i % 2 ? Color(30, 26, 22) : Color(24, 21, 18)); fill();
        }

        // selected pad's actual playback window
        const int sl = int(std::lround(fValues[padParam(fSel, ppSlice)]));
        if (sl >= 0 && sl < d.numSlices) {
            double st = double(d.sliceStart[sl]);
            const double len = double(d.sliceStart[sl + 1]) - st;
            st = std::max(0.0, std::min(frames - 1, st + fValues[padParam(fSel, ppNudge)] * 0.001 * s.sr));
            const double en = std::min(frames, st + len * fValues[padParam(fSel, ppLength)] * 0.01);
            beginPath(); rect(fx(st), w.y, fx(en) - fx(st), w.h);
            fillColor(Color(240, 160, 60, 0.13f)); fill();
            beginPath(); rect(fx(st), w.y + w.h - 3, fx(en) - fx(st), 3);
            fillColor(cOrange); fill();
        }

        // waveform
        const int cols = int(w.w);
        beginPath();
        for (int c = 0; c < cols; ++c) {
            const int b0 = c * kOverviewBins / cols, b1 = std::max(b0 + 1, (c + 1) * kOverviewBins / cols);
            float mn = 0, mx = 0;
            for (int b = b0; b < b1; ++b) { mn = std::min(mn, s.ovMin[b]); mx = std::max(mx, s.ovMax[b]); }
            const float x = w.x + c + 0.5f;
            moveTo(x, mid - std::min(1.f, mx) * amp);
            lineTo(x, mid - std::max(-1.f, mn) * amp + 0.5f);
        }
        strokeColor(Color(205, 190, 165)); strokeWidth(1); stroke();

        // slice markers
        fontSize(10); textAlign(ALIGN_LEFT | ALIGN_TOP);
        for (int i = 0; i < d.numSlices; ++i) {
            const float x = fx(double(d.sliceStart[i]));
            beginPath(); moveTo(x + 0.5f, w.y); lineTo(x + 0.5f, w.y + w.h);
            strokeColor(i == sl ? cOrange : Color(240, 160, 60, 0.55f)); strokeWidth(1); stroke();
            beginPath(); rect(x, w.y, 18, 14); fillColor(i == sl ? cOrange : Color(90, 70, 45)); fill();
            char nb[8]; std::snprintf(nb, sizeof(nb), "%d", i + 1);
            fillColor(i == sl ? Color(30, 22, 15) : cCream); text(x + 3, w.y + 2, nb, nullptr);
        }

        // playhead
        if (fShownPlayPad >= 0 && fShownPlayPos >= 0) {
            const float x = fx(double(fShownPlayPos));
            beginPath(); moveTo(x, w.y); lineTo(x, w.y + w.h);
            strokeColor(Color(255, 255, 255, 0.85f)); strokeWidth(1.5f); stroke();
        }
    }

    Rect padRect(int pad) const
    {
        const int row = 3 - pad / 4, col = pad % 4; // MPC layout: pad 1 bottom-left
        return { kPadX0 + col * kPadGapX, kPadY0 + row * kPadGapY, kPadW, kPadH };
    }

    void drawPads(const ChopDisplay& d)
    {
        const bool chroma = fValues[gKeysMode] > 0.5f;
        const int chromaPad = int(std::lround(fValues[gChromaPad]));
        const int root = int(std::lround(fValues[gRootNote]));

        for (int p = 0; p < kNumPads; ++p) {
            const Rect r = padRect(p);
            const float g = fGlow[p];
            const int sl = int(std::lround(fValues[padParam(p, ppSlice)]));
            const bool empty = !d.sample || sl >= d.numSlices;

            beginPath(); roundedRect(r.x, r.y + 3, r.w, r.h, 7); fillColor(Color(0, 0, 0, 0.35f)); fill();
            beginPath(); roundedRect(r.x, r.y, r.w, r.h, 7);
            const Color top = Color(72 + int(168 * g), 64 + int(96 * g), 56 + int(4 * g));
            const Color bot = Color(56 + int(150 * g), 50 + int(70 * g), 44);
            fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h, top, bot));
            fill();
            if (p == fSel) { strokeColor(cOrange); strokeWidth(2.5f); }
            else { strokeColor(Color(90, 80, 68)); strokeWidth(1); }
            stroke();

            const Color tc = g > 0.5f ? Color(30, 22, 15) : cCream;
            fontSize(18); textAlign(ALIGN_LEFT | ALIGN_TOP); fillColor(tc);
            char b[24]; std::snprintf(b, sizeof(b), "%d", p + 1);
            text(r.x + 8, r.y + 6, b, nullptr);

            fontSize(10.5f); textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(empty ? Color(130, 118, 100) : (g > 0.5f ? tc : cOrange));
            if (empty) std::snprintf(b, sizeof(b), "—");
            else std::snprintf(b, sizeof(b), "S%d", sl + 1);
            text(r.x + r.w - 8, r.y + 9, b, nullptr);

            // flags
            fontSize(9.5f); textAlign(ALIGN_LEFT | ALIGN_BOTTOM); fillColor(g > 0.5f ? tc : cDim);
            std::string flags;
            const float pitch = fValues[padParam(p, ppPitch)];
            if (std::fabs(pitch) > 0.01f) { char pb[12]; std::snprintf(pb, sizeof(pb), "%+.0f ", pitch); flags += pb; }
            if (fValues[padParam(p, ppReverse)] > 0.5f) flags += "REV ";
            if (fValues[padParam(p, ppChoke)] > 0.5f) { char cb[8]; std::snprintf(cb, sizeof(cb), "C%d", int(std::lround(fValues[padParam(p, ppChoke)]))); flags += cb; }
            if (!flags.empty()) text(r.x + 8, r.y + r.h - 22, flags.c_str(), nullptr);

            textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
            if (chroma) { if (p == chromaPad) { fillColor(cOrange); text(r.x + r.w - 8, r.y + r.h - 7, "KEYS", nullptr); } }
            else { noteName(root + p, b, sizeof(b)); text(r.x + r.w - 8, r.y + r.h - 7, b, nullptr); }
        }
    }

    void drawKnob(const KnobDef& k, bool active, const ChopDisplay& d)
    {
        const int idx = resolve(k.param);
        const float n = toNorm(idx, fValues[idx]);
        const float a0 = 0.75f * kPi, a1 = 2.25f * kPi, a = a0 + n * 1.5f * kPi;
        const float tr = k.r + 6;

        beginPath(); arc(k.x, k.y, tr, a0, a1, CW);
        strokeColor(Color(25, 21, 18)); strokeWidth(4); lineCap(ROUND); stroke();
        beginPath();
        if (k.bipolar) { const float m = 1.5f * kPi; if (a > m) arc(k.x, k.y, tr, m, a, CW); else arc(k.x, k.y, tr, a, m, CW); }
        else arc(k.x, k.y, tr, a0, std::max(a, a0 + 0.001f), CW);
        strokeColor(active ? Color(255, 190, 90) : cOrange); strokeWidth(4); stroke();

        beginPath(); circle(k.x, k.y + 3, k.r); fillColor(Color(0, 0, 0, 0.35f)); fill();
        beginPath(); circle(k.x, k.y, k.r);
        fillPaint(radialGradient(k.x - k.r * 0.3f, k.y - k.r * 0.4f, k.r * 0.1f, k.r * 1.2f, Color(245, 236, 216), Color(190, 176, 150)));
        fill(); strokeColor(Color(120, 108, 90)); strokeWidth(1); stroke();
        beginPath();
        moveTo(k.x + std::cos(a) * k.r * 0.25f, k.y + std::sin(a) * k.r * 0.25f);
        lineTo(k.x + std::cos(a) * k.r * 0.85f, k.y + std::sin(a) * k.r * 0.85f);
        strokeColor(Color(45, 35, 28)); strokeWidth(3); lineCap(ROUND); stroke();

        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(10.5f); fillColor(cText);
        text(k.x, k.y - tr - 11, k.label, nullptr);
        char buf[32]; formatValue(idx, fValues[idx], buf, sizeof(buf), d);
        fillColor(active ? Color(255, 200, 110) : Color(170, 155, 132));
        text(k.x, k.y + tr + 11, buf, nullptr);
    }

    void formatValue(int idx, float v, char* buf, size_t sz, const ChopDisplay& d) const
    {
        if (idx == gRootNote) { char nb[8]; noteName(int(std::lround(v)), nb, sizeof(nb)); std::snprintf(buf, sz, "%s", nb); return; }
        if (idx == gBits) { if (v >= 15.5f) std::snprintf(buf, sz, "OFF"); else std::snprintf(buf, sz, "%d bit", int(std::lround(v))); return; }
        if (idx == gRate) { if (v >= 47900.f) std::snprintf(buf, sz, "OFF"); else std::snprintf(buf, sz, "%.1f kHz", v / 1000.f); return; }
        if (idx == gTone) { if (v >= 19900.f) std::snprintf(buf, sz, "OPEN"); else std::snprintf(buf, sz, "%.1f kHz", v / 1000.f); return; }
        if (idx == gVolume) { std::snprintf(buf, sz, "%+.1f dB", v); return; }
        if (idx < kNumGlobal) { std::snprintf(buf, sz, "%.0f %%", v); return; }
        switch (ppOf(idx)) {
        case ppSlice:
            if (!d.sample || int(std::lround(v)) >= d.numSlices) std::snprintf(buf, sz, "%d (none)", int(std::lround(v)) + 1);
            else std::snprintf(buf, sz, "%d / %d", int(std::lround(v)) + 1, d.numSlices);
            break;
        case ppPitch:   std::snprintf(buf, sz, std::fabs(v - std::round(v)) < 0.005f ? "%+.0f st" : "%+.2f st", v); break;
        case ppGain:    std::snprintf(buf, sz, "%+.1f dB", v); break;
        case ppPan:     if (std::fabs(v) < 0.5f) std::snprintf(buf, sz, "C"); else std::snprintf(buf, sz, "%s %.0f", v < 0 ? "L" : "R", std::fabs(v)); break;
        case ppNudge:   std::snprintf(buf, sz, "%+.0f ms", v); break;
        case ppLength:  std::snprintf(buf, sz, "%.0f %%", v); break;
        case ppAttack:  std::snprintf(buf, sz, "%.0f ms", v); break;
        case ppRelease: std::snprintf(buf, sz, "%.0f ms", v); break;
        case ppCutoff:  if (v >= 19500.f) std::snprintf(buf, sz, "OPEN"); else if (v >= 1000.f) std::snprintf(buf, sz, "%.1f kHz", v / 1000.f); else std::snprintf(buf, sz, "%.0f Hz", v); break;
        default:        std::snprintf(buf, sz, "%.0f", v); break;
        }
    }

    void drawButton(const ButtonDef& b)
    {
        bool on = false;
        if (b.kind == kSet) on = std::lround(val(b.param)) == std::lround(b.value);
        else if (b.kind == kToggle) on = val(b.param) > 0.5f;
        else if (b.kind == kAction && b.param == aDig) on = false;

        const bool big = b.kind == kAction && (b.param == aLoad || b.param == aDig || b.param == aReset);
        const bool pressed = fPressedBtn >= 0 && &kButtons[fPressedBtn] == &b;

        beginPath(); roundedRect(b.r.x, b.r.y, b.r.w, b.r.h, 5);
        if (on || pressed) fillPaint(linearGradient(b.r.x, b.r.y, b.r.x, b.r.y + b.r.h, Color(250, 175, 75), Color(210, 130, 40)));
        else if (big)      fillPaint(linearGradient(b.r.x, b.r.y, b.r.x, b.r.y + b.r.h, Color(70, 60, 50), Color(48, 41, 35)));
        else               fillPaint(linearGradient(b.r.x, b.r.y, b.r.x, b.r.y + b.r.h, Color(34, 29, 25), Color(24, 20, 17)));
        fill();
        strokeColor(on || pressed ? Color(255, 200, 120) : (big ? cOrange : Color(80, 70, 58))); strokeWidth(1); stroke();

        fontSize(big ? 13.f : 10.5f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on || pressed ? Color(30, 22, 15) : (big ? cOrange : cText));
        text(b.r.x + b.r.w * 0.5f, b.r.y + b.r.h * 0.5f + 0.5f, b.label, nullptr);
    }

    // =================================================================== actions
    void openBrowser()
    {
        FileBrowserOptions opts;
        opts.title = "Load sample (WAV, AIFF, MP3, FLAC)";
        openFileBrowser(opts);
    }

    void dig()
    {
        ChopDisplay d; copyDisplay(d);
        const int ns = std::max(1, d.numSlices);
        static const float kSemis[] = { -12, -7, -5, -3, 0, 0, 0, 0, 2, 3, 5, 7 };
        std::uniform_int_distribution<int> sl(0, ns - 1), semi(0, int(sizeof(kSemis) / sizeof(float)) - 1);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        for (int p = 0; p < kNumPads; ++p) {
            setParam(padParam(p, ppSlice), float(sl(fRng)));
            setParam(padParam(p, ppPitch), kSemis[semi(fRng)]);
            setParam(padParam(p, ppReverse), u(fRng) < 0.15f ? 1.f : 0.f);
            setParam(padParam(p, ppNudge), 0.f);
            setParam(padParam(p, ppLength), 100.f);
        }
        repaint();
    }

    void resetPads()
    {
        for (int p = 0; p < kNumPads; ++p)
            for (int pp = 0; pp < kNumPadParams; ++pp)
                setParam(padParam(p, pp), defaultValue(padParam(p, pp)));
        repaint();
    }

    void auditionPad(int pad)
    {
        const bool chroma = fValues[gKeysMode] > 0.5f;
        int note;
        if (chroma) { setParam(gChromaPad, float(pad)); note = 60; }
        else note = int(std::lround(fValues[gRootNote])) + pad;
        if (note < 0 || note > 127) return;
        sendNote(0, uint8_t(note), 100);
        fHeldNote = note;
    }

    void releaseAudition()
    {
        if (fHeldNote >= 0) { sendNote(0, uint8_t(fHeldNote), 0); fHeldNote = -1; }
    }

    // =================================================================== input
    int hitKnob(float x, float y) const
    {
        for (int i = 0; i < kNumKnobs; ++i) {
            const float dx = x - kKnobs[i].x, dy = y - kKnobs[i].y, r = kKnobs[i].r + 8;
            if (dx * dx + dy * dy <= r * r) return i;
        }
        return -1;
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = float(ev.pos.getX()) / fScale, y = float(ev.pos.getY()) / fScale;
        if (ev.button != kMouseButtonLeft) return false;

        if (!ev.press) {
            releaseAudition();
            if (fDragKnob >= 0) { editParameter(uint32_t(resolve(kKnobs[fDragKnob].param)), false); fDragKnob = -1; }
            if (fDragSens) { editParameter(gSensitivity, false); fDragSens = false; }
            fPressedBtn = -1;
            repaint();
            return true;
        }

        // knobs
        const int k = hitKnob(x, y);
        if (k >= 0) {
            const int idx = resolve(kKnobs[k].param);
            if (k == fLastClickKnob && ev.time - fLastClickTime < 350) {
                setParam(idx, defaultValue(idx));
                fLastClickKnob = -1;
                repaint();
                return true;
            }
            fLastClickKnob = k; fLastClickTime = ev.time;
            fDragKnob = k; fDragStartY = y; fDragStartNorm = toNorm(idx, fValues[idx]);
            editParameter(uint32_t(idx), true);
            repaint();
            return true;
        }

        // sensitivity slider
        if (kSensSlider.hit(x, y) || Rect{ kSensSlider.x - 6, kSensSlider.y - 6, kSensSlider.w + 12, kSensSlider.h + 12 }.hit(x, y)) {
            fDragSens = true;
            editParameter(gSensitivity, true);
            setSensFromX(x);
            return true;
        }

        // buttons
        for (int i = 0; i < kNumButtons; ++i) {
            const ButtonDef& b = kButtons[i];
            if (!b.r.hit(x, y)) continue;
            if (b.kind == kSet) setParam(resolve(b.param), b.value);
            else if (b.kind == kToggle) setParam(resolve(b.param), val(b.param) > 0.5f ? 0.f : 1.f);
            else {
                fPressedBtn = i;
                switch (b.param) {
                case aLoad: openBrowser(); break;
                case aDig: dig(); break;
                case aReset: resetPads(); break;
                case aSlicesDown: setParam(gSlices, std::max(1.f, fValues[gSlices] - 1)); break;
                case aSlicesUp: setParam(gSlices, std::min(16.f, fValues[gSlices] + 1)); break;
                }
            }
            repaint();
            return true;
        }

        // pads
        for (int p = 0; p < kNumPads; ++p) {
            if (!padRect(p).hit(x, y)) continue;
            fSel = p;
            auditionPad(p);
            repaint();
            return true;
        }

        // waveform: load when empty, otherwise select the pad that plays the clicked slice
        if (kWave.hit(x, y)) {
            ChopDisplay d; copyDisplay(d);
            if (!d.sample) { openBrowser(); return true; }
            const double f = double((x - kWave.x) / kWave.w) * double(d.sample->frames);
            for (int i = 0; i < d.numSlices; ++i) {
                if (f >= double(d.sliceStart[i]) && f < double(d.sliceStart[i + 1])) {
                    for (int p = 0; p < kNumPads; ++p)
                        if (std::lround(fValues[padParam(p, ppSlice)]) == i) { fSel = p; auditionPad(p); break; }
                    break;
                }
            }
            repaint();
            return true;
        }
        return false;
    }

    void setSensFromX(float x)
    {
        const float n = std::min(1.f, std::max(0.f, (x - kSensSlider.x) / kSensSlider.w));
        const float v = fromNorm(gSensitivity, n);
        fValues[gSensitivity] = v;
        setParameterValue(gSensitivity, v);
        repaint();
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float x = float(ev.pos.getX()) / fScale, y = float(ev.pos.getY()) / fScale;
        if (fDragSens) { setSensFromX(x); return true; }
        if (fDragKnob < 0) return false;
        const bool fine = (ev.mod & kModifierShift) != 0;
        const int idx = resolve(kKnobs[fDragKnob].param);
        const float n = std::min(1.f, std::max(0.f, fDragStartNorm + (fDragStartY - y) / (fine ? 1000.f : 200.f)));
        float v = fromNorm(idx, n);
        if (isPadParam(idx) && ppOf(idx) == ppPitch && !fine) v = std::round(v);
        if (v != fValues[idx]) { fValues[idx] = v; setParameterValue(uint32_t(idx), v); repaint(); }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = float(ev.pos.getX()) / fScale, y = float(ev.pos.getY()) / fScale;
        const int k = hitKnob(x, y);
        if (k < 0) return false;
        const int idx = resolve(kKnobs[k].param);
        const Info& in = info(idx);
        const float dir = ev.delta.getY() > 0 ? 1.f : -1.f;
        float v;
        if (in.isInt || (isPadParam(idx) && ppOf(idx) == ppPitch)) v = std::min(in.max, std::max(in.min, std::round(fValues[idx]) + dir));
        else v = fromNorm(idx, toNorm(idx, fValues[idx]) + dir * ((ev.mod & kModifierShift) ? 0.005f : 0.025f));
        setParam(idx, v);
        repaint();
        return true;
    }

private:
    CrateChopPlugin* fPlugin = nullptr;
    float fValues[kNumParams];
    float fScale = 1.f;
    int fSel = 0;

    int fDragKnob = -1;
    float fDragStartY = 0, fDragStartNorm = 0;
    int fLastClickKnob = -1;
    uint fLastClickTime = 0;
    bool fDragSens = false;
    int fPressedBtn = -1;
    int fHeldNote = -1;

    float fGlow[kNumPads];
    uint32_t fLastTrig[kNumPads];
    int fShownPlayPad = -1;
    int64_t fShownPlayPos = -1;
    uint32_t fShownVersion = 0;

    std::mt19937 fRng;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrateChopUI)
};

UI* createUI() { return new CrateChopUI(); }

END_NAMESPACE_DISTRHO
