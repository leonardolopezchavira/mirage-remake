// The built-in editor (an addition): every program and wavesample parameter of the selected
// half on one page, envelope graphs and the wavesample with its loop. It talks to the
// emulated Mirage the way an external editor would, through the MASOS_BE SysEx protocol
// (src/engine/MasosLink.*), so it needs MASOS_BE booted; it reads the sample memory directly
// to draw the waveform. Drawn below the panel, in place of the on-screen keyboard, in its own coordinates (kEdW x kEdH).
// MIT licence.
#pragma once

#include "NanoVG.hpp"
#include "../engine/MasosLink.h"
#include "../engine/MirageEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

class EditorView {
    using Color = DGL_NAMESPACE::Color;

public:
    static constexpr float kEdW = 1200, kEdH = 440;

    EditorView(mirage::MasosLink& link, mirage::MirageEngine& engine) : link_(link), engine_(engine) { layout(); }

    // ------------------------------------------------------------------ drawing
    void draw(NanoVG& g)
    {
        const bool on = link_.connected();
        const int h = link_.currentHalf();

        g.beginPath(); g.rect(0, 0, kEdW, kEdH);
        g.fillPaint(g.linearGradient(0, 0, 0, kEdH, Color(30, 32, 36), Color(22, 23, 26))); g.fill();
        g.beginPath(); g.rect(0, 0, kEdW, 2); g.fillColor(Color(214, 34, 30)); g.fill();

        // header: half, program and wavesample selection
        label(g, 20, 24, "EDITOR", 12, Color(232, 232, 232), NanoVG::ALIGN_LEFT, 2.0f);
        for (const auto& t : tabs_) {
            bool sel = false;
            if (t.kind == TabHalf) sel = t.index == h;
            if (t.kind == TabProgram) sel = t.index == link_.half(h).program;
            if (t.kind == TabWave) sel = t.index == link_.half(h).wavesample;
            pill(g, t.x, t.y, t.w, t.h, t.text, sel, on);
        }
        label(g, 302, 23, "PROGRAM", 9, kDim, NanoVG::ALIGN_RIGHT, 1.5f);
        label(g, 532, 23, "WAVESAMPLE", 9, kDim, NanoVG::ALIGN_RIGHT, 1.5f);
        pill(g, kRefresh.x, kRefresh.y, kRefresh.w, kRefresh.h, "Read again", false, true);
        label(g, 1072, 24, on ? "Connected to MASOS" : "Not connected", 10,
              on ? Color(120, 220, 120) : Color(230, 140, 90), NanoVG::ALIGN_RIGHT);

        // sections
        section(g, 20, 52, 320, "PROGRAM");
        section(g, 360, 52, 400, "FILTER ENVELOPE");
        section(g, 360, 248, 400, "AMPLIFIER ENVELOPE");
        section(g, 780, 52, 400, "WAVESAMPLE");
        envelope(g, 360, 64, 400, 70, 40, kYellow);
        envelope(g, 360, 260, 400, 70, 50, kYellow);
        waveform(g, 780, 64, 400, 150);

        for (const auto& s : sliders_) slider(g, s, on);

        if (!on) {
            g.beginPath(); g.rect(0, 40, kEdW, kEdH - 40); g.fillColor(Color(0, 0, 0, 0.6f)); g.fill();
            label(g, kEdW / 2, 200, "Editing needs the MASOS_BE operating system.", 15, Color(240, 240, 240), NanoVG::ALIGN_CENTER);
            label(g, kEdW / 2, 228, "Open\xE2\x80\xA6 MASOS_BE-v1.0.0.hfe (from BoxedEar), then click POWER so the Mirage boots from it.", 11,
                  Color(200, 200, 200), NanoVG::ALIGN_CENTER);
            label(g, kEdW / 2, 250, "The editor connects by itself once it is running.", 11, Color(200, 200, 200), NanoVG::ALIGN_CENTER);
        }
    }

    // ------------------------------------------------------------------ input
    bool mouseDown(float x, float y)
    {
        const int h = link_.currentHalf();
        if (hit(kRefresh, x, y)) { link_.requestAll(); return true; }
        if (!link_.connected()) return y > 0;
        for (const auto& t : tabs_) {
            if (!hit(t, x, y)) continue;
            if (t.kind == TabHalf) link_.selectWavesample(t.index, link_.half(t.index).wavesample);
            if (t.kind == TabProgram) link_.selectProgram(h, t.index);
            if (t.kind == TabWave) link_.selectWavesample(h, t.index);
            return true;
        }
        for (size_t i = 0; i < sliders_.size(); ++i) {
            const auto& s = sliders_[i];
            if (x < s.x || x > s.x + s.w || y < s.y - 2 || y > s.y + kRowH - 2) continue;
            const mirage::MasosParam* p = mirage::masosParam(s.param);
            if (p->display == mirage::ParamDisplay::OnOff) {
                link_.setParam(h, s.param, link_.value(h, s.param) ? 0 : 1);
                return true;
            }
            if (x < s.x + s.labelW) return true;
            dragSlider_ = int(i);
            dragTo(x);
            return true;
        }
        // loop markers on the waveform
        if (x >= kWave.x && x <= kWave.x + kWave.w && y >= kWave.y && y <= kWave.y + kWave.h) {
            const float ls = pageX(link_.value(h, 62)), le = pageX(link_.value(h, 63) + 1);
            dragMarker_ = std::fabs(x - ls) < std::fabs(x - le) ? 62 : 63;
            dragMarkerTo(x);
            return true;
        }
        return y > 0;
    }

    bool mouseMove(float x, float)
    {
        if (dragSlider_ >= 0) { dragTo(x); return true; }
        if (dragMarker_) { dragMarkerTo(x); return true; }
        return false;
    }

    void mouseUp() { dragSlider_ = -1; dragMarker_ = 0; }
    bool dragging() const { return dragSlider_ >= 0 || dragMarker_ != 0; }

    bool scroll(float x, float y, float dy)
    {
        if (!link_.connected()) return false;
        const int h = link_.currentHalf();
        for (const auto& s : sliders_) {
            if (x < s.x || x > s.x + s.w || y < s.y - 2 || y > s.y + kRowH - 2) continue;
            const mirage::MasosParam* p = mirage::masosParam(s.param);
            const int dir = dy > 0 ? 1 : dy < 0 ? -1 : 0;
            link_.setParam(h, s.param, link_.value(h, s.param) + dir * p->step);
            return true;
        }
        return false;
    }

private:
    static constexpr float kRowH = 19;
    const Color kDim = Color(150, 154, 162);
    const Color kYellow = Color(240, 180, 60);
    enum TabKind { TabHalf, TabProgram, TabWave };
    struct Rect { float x, y, w, h; };
    struct Tab { float x, y, w, h; TabKind kind; int index; std::string text; };
    struct Slider { int param; float x, y, w, labelW; const char* name; };

    mirage::MasosLink& link_;
    mirage::MirageEngine& engine_;
    std::vector<Tab> tabs_;
    std::vector<Slider> sliders_;
    int dragSlider_ = -1, dragMarker_ = 0;
    const Rect kRefresh{1088, 12, 92, 20};
    const Rect kWave{780, 64, 400, 150};

    template <class R> static bool hit(const R& r, float x, float y)
    {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    }

    void layout()
    {
        tabs_.push_back({100, 12, 56, 20, TabHalf, 0, "Lower"});
        tabs_.push_back({160, 12, 56, 20, TabHalf, 1, "Upper"});
        for (int i = 0; i < 4; ++i) tabs_.push_back({310.0f + i * 26, 12, 22, 20, TabProgram, i, std::to_string(i + 1)});
        for (int i = 0; i < 8; ++i) tabs_.push_back({540.0f + i * 26, 12, 22, 20, TabWave, i, std::to_string(i + 1)});

        // short names where the columns are narrow
        auto shortName = [](int p) -> const char* {
            switch (p) {
            case 45: case 55: return "A \xC3\x97 velocity";
            case 46: case 56: return "P \xC3\x97 velocity";
            case 47: case 57: return "D \xC3\x97 key";
            case 48: case 58: return "S \xC3\x97 velocity";
            case 49: case 59: return "R \xC3\x97 velocity";
            case 64: return "Loop end fine";
            case 69: return "Rel. amplitude";
            case 70: return "Rel. filter";
            case 71: return "Max. filter";
            default: return nullptr;
            }
        };
        auto column = [&](float x, float y, float w, float labelW, std::initializer_list<int> params) {
            for (int p : params) {
                const char* n = shortName(p);
                sliders_.push_back({p, x, y, w, labelW, n ? n : mirage::masosParam(p)->name});
                y += kRowH;
            }
        };
        column(20, 70, 320, 150, {27, 28, 29, 31, 32, 33, 34, 35, 36, 37, 38});
        column(360, 142, 190, 80, {40, 41, 42, 43, 44});
        column(570, 142, 190, 110, {45, 46, 47, 48, 49});
        column(360, 338, 190, 80, {50, 51, 52, 53, 54});
        column(570, 338, 190, 110, {55, 56, 57, 58, 59});
        column(780, 226, 190, 105, {60, 61, 62, 63, 64, 65});
        column(990, 226, 190, 100, {67, 68, 69, 70, 71, 72});
    }

    void dragTo(float x)
    {
        const auto& s = sliders_[size_t(dragSlider_)];
        const mirage::MasosParam* p = mirage::masosParam(s.param);
        const float x0 = s.x + s.labelW, x1 = s.x + s.w - 30;
        const float t = std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
        const int steps = p->maxRaw / p->step;
        const int raw = int(std::lround(t * steps)) * p->step;
        if (raw != link_.value(link_.currentHalf(), s.param)) link_.setParam(link_.currentHalf(), s.param, raw);
    }

    // waveform view: the wavesample's pages, sample start to sample end
    float pageX(int page) const
    {
        const int h = link_.currentHalf();
        const int s = link_.value(h, 60), e = link_.value(h, 61) + 1;
        if (e <= s) return kWave.x;
        return kWave.x + kWave.w * float(page - s) / float(e - s);
    }

    void dragMarkerTo(float x)
    {
        const int h = link_.currentHalf();
        const int s = link_.value(h, 60), e = link_.value(h, 61) + 1;
        int page = s + int(std::lround((x - kWave.x) / kWave.w * float(e - s)));
        if (dragMarker_ == 63) page -= 1;                   // the marker sits at the page's end
        page = std::clamp(page, s, e - 1);
        if (dragMarker_ == 62) page = std::min(page, link_.value(h, 63));
        else page = std::max(page, link_.value(h, 62));
        if (page != link_.value(h, dragMarker_)) link_.setParam(h, dragMarker_, page);
    }

    // ------------------------------------------------------------------ pieces
    static void label(NanoVG& g, float x, float y, const char* t, float size, Color c, int align, float spacing = 0)
    {
        if (t == nullptr || t[0] == '\0') return;
        g.fontSize(size);
        g.textLetterSpacing(spacing);
        g.fillColor(c);
        g.textAlign(align | NanoVG::ALIGN_MIDDLE);
        g.text(x, y, t, nullptr);
        g.textLetterSpacing(0);
    }

    void pill(NanoVG& g, float x, float y, float w, float h, const std::string& t, bool sel, bool enabled)
    {
        g.beginPath(); g.roundedRect(x, y, w, h, 3);
        g.fillColor(sel ? Color(214, 34, 30) : Color(56, 58, 64)); g.fill();
        label(g, x + w / 2, y + h / 2 + 1, t.c_str(), 10, enabled ? Color(240, 240, 240) : kDim, NanoVG::ALIGN_CENTER);
    }

    void section(NanoVG& g, float x, float y, float w, const char* t)
    {
        label(g, x, y, t, 9, kDim, NanoVG::ALIGN_LEFT, 1.5f);
        g.beginPath(); g.rect(x, y + 7, w, 1); g.fillColor(Color(70, 73, 80)); g.fill();
    }

    void slider(NanoVG& g, const Slider& s, bool on)
    {
        const int h = link_.currentHalf();
        const mirage::MasosParam* p = mirage::masosParam(s.param);
        const int raw = on ? link_.value(h, s.param) : 0;
        char name[64];
        std::snprintf(name, sizeof name, "%d  %s", p->number, s.name);
        label(g, s.x, s.y + 8, name, 10, Color(212, 214, 220), NanoVG::ALIGN_LEFT);
        const float x0 = s.x + s.labelW, x1 = s.x + s.w - 30, ty = s.y + 4;
        if (p->display == mirage::ParamDisplay::OnOff) {
            g.beginPath(); g.roundedRect(x0, ty - 1, 30, 11, 5);
            g.fillColor(raw ? Color(214, 34, 30) : Color(56, 58, 64)); g.fill();
            g.beginPath(); g.circle(raw ? x0 + 24 : x0 + 6, ty + 4.5f, 4); g.fillColor(Color(230, 230, 230)); g.fill();
        } else {
            g.beginPath(); g.roundedRect(x0, ty, x1 - x0, 9, 2); g.fillColor(Color(14, 15, 17)); g.fill();
            const float t = p->maxRaw ? float(raw) / float(p->maxRaw) : 0;
            g.beginPath(); g.roundedRect(x0, ty, std::max(2.0f, (x1 - x0) * t), 9, 2);
            g.fillColor(Color(196, 198, 204)); g.fill();
        }
        label(g, s.x + s.w, s.y + 8, on ? mirage::masosDisplay(*p, raw).c_str() : "--", 11,
              Color(255, 70, 50), NanoVG::ALIGN_RIGHT);
    }

    // Rate-based envelope: stages are rates (0 fastest), drawn with a width that grows with
    // the value, between the levels (peak, sustain; 0..31).
    void envelope(NanoVG& g, float x, float y, float w, float hgt, int base, Color c)
    {
        g.beginPath(); g.roundedRect(x, y, w, hgt, 3); g.fillColor(Color(12, 13, 15)); g.fill();
        if (!link_.connected()) return;
        const int h = link_.currentHalf();
        const float a = float(link_.value(h, base)), pk = float(link_.value(h, base + 1)),
                    d = float(link_.value(h, base + 2)), su = float(link_.value(h, base + 3)),
                    r = float(link_.value(h, base + 4));
        auto len = [](float v) { return 4.0f + v * 3.4f; };
        const float hold = 60;
        const float total = len(a) + len(d) + hold + len(r);
        const float k = std::min(1.0f, (w - 16) / total);
        const float bx = x + 8, by = y + hgt - 6, top = y + 6;
        auto lv = [&](float v) { return by - (by - top) * v / 31.0f; };
        float px = bx;
        g.beginPath();
        g.moveTo(px, by);
        px += len(a) * k; g.lineTo(px, lv(pk));
        px += len(d) * k; g.lineTo(px, lv(su));
        px += hold * k;   g.lineTo(px, lv(su));
        px += len(r) * k; g.lineTo(px, by);
        g.strokeColor(c); g.strokeWidth(2); g.stroke();
        g.lineTo(bx, by); g.closePath();
        g.fillColor(Color(c.red, c.green, c.blue, 0.18f)); g.fill();
    }

    void waveform(NanoVG& g, float x, float y, float w, float hgt)
    {
        g.beginPath(); g.roundedRect(x, y, w, hgt, 3); g.fillColor(Color(12, 13, 15)); g.fill();
        if (!link_.connected()) return;
        const int h = link_.currentHalf();
        const int s = link_.value(h, 60), e = link_.value(h, 61) + 1;
        if (e <= s) return;
        const uint8_t* ram = engine_.machine().sampleRam() + (size_t(h) << 16);
        const float mid = y + hgt / 2, amp = hgt / 2 - 4;

        // loop region
        const bool loop = link_.value(h, 65) != 0;
        const float ls = pageX(link_.value(h, 62)), le = pageX(link_.value(h, 63) + 1);
        g.beginPath(); g.rect(ls, y + 1, std::max(1.0f, le - ls), hgt - 2);
        g.fillColor(loop ? Color(240, 180, 60, 0.16f) : Color(120, 120, 120, 0.10f)); g.fill();

        const int first = s * 256, last = std::min(e * 256, 65536);
        const int cols = int(w);
        g.beginPath();
        for (int c = 0; c < cols; ++c) {
            const int i0 = first + int(int64_t(last - first) * c / cols);
            const int i1 = std::max(i0 + 1, first + int(int64_t(last - first) * (c + 1) / cols));
            int lo = 255, hi = 0;
            for (int i = i0; i < i1 && i < 65536; ++i) { lo = std::min(lo, int(ram[i])); hi = std::max(hi, int(ram[i])); }
            const float y0 = mid - (hi - 128) / 128.0f * amp, y1 = mid - (lo - 128) / 128.0f * amp;
            g.rect(x + c, y0, 1, std::max(1.0f, y1 - y0));
        }
        g.fillColor(kYellow); g.fill();

        // loop markers (white, to stand out from the yellow waveform)
        for (float mx : {ls, le}) {
            g.beginPath(); g.rect(mx - 1, y + 1, 2, hgt - 2);
            g.fillColor(loop ? Color(245, 245, 245) : Color(130, 130, 130)); g.fill();
        }
        label(g, x + 6, y + hgt - 9, "drag the white lines to move the loop", 9, kDim, NanoVG::ALIGN_LEFT);
    }
};

END_NAMESPACE_DISTRHO
