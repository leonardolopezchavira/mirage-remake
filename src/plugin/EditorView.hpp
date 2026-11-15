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
        pill(g, kKeymapBtn.x, kKeymapBtn.y, kKeymapBtn.w, kKeymapBtn.h, "Key map", keymapPage_, true);
        label(g, 976, 24, on ? "Connected to MASOS" : "Not connected", 10,
              on ? Color(120, 220, 120) : Color(230, 140, 90), NanoVG::ALIGN_RIGHT);

        if (keymapPage_) {
            keymapGrid(g, on);
        } else {
            parameterPage(g, on);
        }

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
        if (hit(kKeymapBtn, x, y)) { keymapPage_ = !keymapPage_; return true; }
        if (!link_.connected()) return y > 0;
        for (const auto& t : tabs_) {
            if (!hit(t, x, y)) continue;
            if (t.kind == TabHalf) link_.selectWavesample(t.index, link_.half(t.index).wavesample);
            if (t.kind == TabProgram) link_.selectProgram(h, t.index);
            if (t.kind == TabWave) link_.selectWavesample(h, t.index);
            return true;
        }
        if (keymapPage_) return gridMouseDown(x, y);
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
        // keymap: drag a band's right edge (top key) or click a band (select it)
        if (y >= kMapY && y <= kKeysY + kKeysH) {
            const auto bands = keymapBands();
            for (const auto& b : bands)
                if (std::fabs(x - edgeAfter(b.last)) <= 5 && b.last < 60) {
                    if (b.ws != link_.half(h).wavesample) link_.selectWavesample(h, b.ws);
                    dragEdge_ = b.ws;
                    return true;
                }
            for (const auto& b : bands)
                if (x >= edgeAfter(b.first - 1) && x < edgeAfter(b.last)) { link_.selectWavesample(h, b.ws); return true; }
            return true;
        }
        // waveform markers: the upper half grabs the sample start / end, the lower half the loop
        if (x >= kWave.x - 6 && x <= kWave.x + kWave.w + 6 && y >= kWave.y && y <= kWave.y + kWave.h) {
            const bool top = y < kWave.y + kWave.h / 2;
            const int a = top ? 60 : 62, b = top ? 61 : 63;
            const float xa = pageX(link_.value(h, a)), xb = pageX(link_.value(h, b) + 1);
            const float da = std::fabs(x - xa), db = std::fabs(x - xb);
            if (std::min(da, db) > 12) return true;
            dragMarker_ = da < db ? a : b;
            dragMarkerTo(x);
            return true;
        }
        return y > 0;
    }

    bool mouseMove(float x, float)
    {
        if (dragSlider_ >= 0) { dragTo(x); return true; }
        if (dragMarker_) { dragMarkerTo(x); return true; }
        if (dragEdge_ >= 0) { dragEdgeTo(x); return true; }
        if (dragGrid_) { gridSetTop(x); return true; }
        return false;
    }

    void mouseUp() { dragSlider_ = -1; dragMarker_ = 0; dragEdge_ = -1; dragGrid_ = false; }
    bool dragging() const { return dragSlider_ >= 0 || dragMarker_ != 0 || dragEdge_ >= 0 || dragGrid_; }

    // Pointer moved with no button down: the keymap shows the band under it. True to repaint.
    bool hover(float x, float y)
    {
        int ws = -1;
        if (link_.connected() && !keymapPage_ && y >= kMapY && y <= kKeysY + kKeysH)
            for (const auto& b : keymapBands())
                if (x >= edgeAfter(b.first - 1) && x < edgeAfter(b.last)) ws = b.ws;
        if (ws == hoverWs_) return false;
        hoverWs_ = ws;
        return true;
    }

    bool scroll(float x, float y, float dy)
    {
        if (!link_.connected() || keymapPage_) return false;
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
    int dragSlider_ = -1, dragMarker_ = 0, dragEdge_ = -1, hoverWs_ = -1;
    bool keymapPage_ = false, dragGrid_ = false;
    const Rect kRefresh{1088, 12, 92, 20};
    const Rect kKeymapBtn{990, 12, 90, 20};
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
    // Waveform view: the sample (start to end) with a margin on each side, so the start and end
    // markers have room to move. The view stays put while a marker is dragged.
    mutable int viewFirst_ = 0, viewLast_ = 256;   // pages, last exclusive

    void updateView() const
    {
        if (dragMarker_) return;
        const int h = link_.currentHalf();
        const int s = link_.value(h, 60), e = std::max(s + 1, link_.value(h, 61) + 1);
        const int pad = std::max(2, (e - s) / 4);
        viewFirst_ = std::max(0, s - pad);
        viewLast_ = std::min(256, e + pad);
    }

    float pageX(int page) const
    {
        return kWave.x + kWave.w * float(page - viewFirst_) / float(std::max(1, viewLast_ - viewFirst_));
    }

    int pageAt(float x) const
    {
        return viewFirst_ + int(std::lround((x - kWave.x) / kWave.w * float(viewLast_ - viewFirst_)));
    }

    // Markers: 60 sample start and 61 sample end (tags at the top), 62 loop start and 63 loop end
    // (tags at the bottom). End markers sit at the end of their page.
    void dragMarkerTo(float x)
    {
        const int h = link_.currentHalf();
        const int s = link_.value(h, 60), e = link_.value(h, 61);
        int page = pageAt(x);
        switch (dragMarker_) {
        case 60: page = std::clamp(page, 0, std::min(e, 254)); break;
        case 61: page = std::clamp(page - 1, std::max(s, 1), 255); break;
        case 62: page = std::clamp(page, s, std::min(e, link_.value(h, 63))); break;
        case 63: page = std::clamp(page - 1, std::max(s, link_.value(h, 62)), e); break;
        default: return;
        }
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

    void parameterPage(NanoVG& g, bool on)
    {
        section(g, 20, 52, 320, "PROGRAM");
        section(g, 360, 52, 400, "FILTER ENVELOPE");
        section(g, 360, 248, 400, "AMPLIFIER ENVELOPE");
        section(g, 780, 52, 400, "WAVESAMPLE");
        envelope(g, 360, 64, 400, 70, 40, kYellow);
        envelope(g, 360, 260, 400, 70, 50, kYellow);
        waveform(g, 780, 64, 400, 150);
        section(g, 20, 292, 320, "KEYMAP");
        keymap(g);

        for (const auto& s : sliders_) slider(g, s, on);
    }

    // ------------------------------------------------------------------ keymap
    // The 61 keys (top key 0 = the bottom C) and the wavesamples the selected program plays:
    // from its initial wavesample (27) upwards, each one taking the keys above the previous
    // one's top key up to its own (72), until one whose top key doesn't rise any more.
    static constexpr float kMapX = 20, kMapW = 320, kMapY = 304, kBandH = 18, kKeysY = 324, kKeysH = 58;
    struct Band { int ws, first, last; };

    std::vector<Band> keymapBands() const
    {
        std::vector<Band> bands;
        const int h = link_.currentHalf();
        const auto& half = link_.half(h);
        const mirage::MasosParam& top = *mirage::masosParam(72);
        int ws = half.programRaw(half.program, *mirage::masosParam(27)) & 7, first = 0, prevTop = -1;
        for (; ws < 8; ++ws) {
            const int t = std::min(60, half.wavesampleRaw(ws, top));
            if (t <= prevTop) break;
            bands.push_back({ws, first, t});
            first = t + 1;
            prevTop = t;
            if (t >= 60) break;
        }
        return bands;
    }

    static bool isBlack(int k) { const int n = k % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
    static int whitesBefore(int k)
    {
        int c = 0;
        for (int i = 0; i < k; ++i) c += isBlack(i) ? 0 : 1;
        return c;
    }
    static float whiteW() { return kMapW / 36.0f; }   // C to C, 5 octaves: 36 white keys
    static float keyCentre(int k)
    {
        if (k < 0) return kMapX;
        if (k > 60) return kMapX + kMapW;
        return isBlack(k) ? kMapX + whitesBefore(k) * whiteW() : kMapX + (whitesBefore(k) + 0.5f) * whiteW();
    }
    // the boundary between key k and key k + 1
    static float edgeAfter(int k)
    {
        if (k < 0) return kMapX;
        if (k >= 60) return kMapX + kMapW;
        return (keyCentre(k) + keyCentre(k + 1)) / 2;
    }

    void dragEdgeTo(float x)
    {
        const int h = link_.currentHalf();
        const auto bands = keymapBands();
        for (size_t i = 0; i < bands.size(); ++i) {
            if (bands[i].ws != dragEdge_) continue;
            // the nearest key boundary, keeping at least one key for this band and the next
            int k = 0;
            float best = 1e9f;
            for (int t = 0; t <= 60; ++t)
                if (std::fabs(edgeAfter(t) - x) < best) { best = std::fabs(edgeAfter(t) - x); k = t; }
            const int lo = bands[i].first, hi = i + 1 < bands.size() ? bands[i + 1].last - 1 : 60;
            k = std::clamp(k, lo, std::max(lo, hi));
            if (k != link_.value(h, 72)) link_.setParam(h, 72, k);
            return;
        }
    }

    static std::string noteName(int k)
    {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return std::string(names[k % 12]) + std::to_string(k / 12 + 2);
    }

    void keymap(NanoVG& g)
    {
        // keys
        const float ww = whiteW();
        for (int k = 0, wi = 0; k <= 60; ++k) {
            if (isBlack(k)) continue;
            g.beginPath(); g.rect(kMapX + wi * ww + 0.5f, kKeysY, ww - 1, kKeysH);
            g.fillColor(Color(226, 224, 216)); g.fill();
            ++wi;
        }
        for (int k = 0; k <= 60; ++k) {
            if (!isBlack(k)) continue;
            g.beginPath(); g.rect(keyCentre(k) - ww * 0.32f, kKeysY, ww * 0.64f, kKeysH * 0.6f);
            g.fillColor(Color(24, 24, 26)); g.fill();
        }
        g.beginPath(); g.rect(kMapX, kMapY, kMapW, kBandH); g.fillColor(Color(12, 13, 15)); g.fill();
        if (!link_.connected()) return;

        const int h = link_.currentHalf();
        const int sel = link_.half(h).wavesample;
        const auto bands = keymapBands();
        for (size_t i = 0; i < bands.size(); ++i) {
            const auto& b = bands[i];
            const float x0 = edgeAfter(b.first - 1), x1 = edgeAfter(b.last);
            const bool on = b.ws == sel, hov = b.ws == hoverWs_;
            g.beginPath(); g.rect(x0 + 0.5f, kMapY, x1 - x0 - 1, kBandH);
            g.fillColor(on ? kYellow : hov ? Color(170, 128, 44) : Color(120, 90, 32)); g.fill();
            // the same range shaded over the keys
            g.beginPath(); g.rect(x0, kKeysY, x1 - x0, kKeysH);
            g.fillColor(on ? Color(240, 180, 60, 0.30f) : Color(240, 180, 60, 0.0f)); g.fill();
            char t[16];
            std::snprintf(t, sizeof t, i == 0 ? "%d\xE2\x96\xB8" : "%d", b.ws + 1);   // ▸ marks the initial wavesample
            if (x1 - x0 > 14)
                label(g, (x0 + x1) / 2, kMapY + kBandH / 2 + 1, t, 10, on ? Color(20, 20, 20) : Color(235, 235, 235), NanoVG::ALIGN_CENTER);
            // draggable top-key edge
            if (b.last < 60) {
                g.beginPath(); g.rect(x1 - 1, kMapY - 2, 2, kBandH + kKeysH + 8);
                g.fillColor(Color(245, 245, 245, on ? 0.9f : 0.45f)); g.fill();
            }
        }
        // what the pointer (or else the selection) is on
        const int show = hoverWs_ >= 0 ? hoverWs_ : sel;
        std::string info;
        for (const auto& b : bands)
            if (b.ws == show) {
                info = "Wavesample " + std::to_string(b.ws + 1) + ": " + noteName(b.first) + " to " + noteName(b.last) +
                       "  (keys " + std::to_string(b.first + 1) + "\xE2\x80\x93" + std::to_string(b.last + 1) + ")";
                if (b.ws == bands.front().ws) info += ", initial";
            }
        if (info.empty())
            info = "Wavesample " + std::to_string(sel + 1) + " is not played by program " + std::to_string(link_.half(h).program + 1);
        label(g, kMapX, kKeysY + kKeysH + 14, info.c_str(), 10, Color(212, 214, 220), NanoVG::ALIGN_LEFT);
        label(g, kMapX, kKeysY + kKeysH + 30, "click a band to select it, drag a white line to move its top key", 9, kDim,
              NanoVG::ALIGN_LEFT);
    }

    // ------------------------------------------------------------------ key map page
    // All 16 wavesamples, one row each (U8..U1 above L8..L1), one column per key: each row is
    // filled from the bottom C up to the wavesample's top key, as in BoxedEar's Visage.
    static constexpr float kGX = 60, kGW = 1120, kRowStep = 16, kUpperY = 72, kLowerY = 210, kGKeysY = 348, kGKeysH = 46;
    const Rect kStepDown{460, 42, 24, 20}, kStepUp{554, 42, 24, 20};
    const Color kBlue = Color(96, 150, 220);

    static float colX(int k) { return kGX + kGW * float(k) / 61.0f; }
    static float colW() { return kGW / 61.0f; }
    static float rowY(int half, int ws) { return (half ? kUpperY : kLowerY) + float(7 - ws) * kRowStep; }

    void keymapGrid(NanoVG& g, bool on)
    {
        const int h = link_.currentHalf(), sel = link_.half(h).wavesample;
        const mirage::MasosParam& topP = *mirage::masosParam(72);
        label(g, 20, 52, "Key map", 15, Color(240, 240, 240), NanoVG::ALIGN_LEFT);
        char title[32];
        std::snprintf(title, sizeof title, "Top key of %c%d", h ? 'U' : 'L', sel + 1);
        label(g, 450, 52, title, 11, kDim, NanoVG::ALIGN_RIGHT);
        pill(g, kStepDown.x, kStepDown.y, kStepDown.w, kStepDown.h, "\xE2\x80\xB9", false, on);
        g.beginPath(); g.roundedRect(488, 42, 62, 20, 3); g.fillColor(Color(56, 58, 64)); g.fill();
        label(g, 519, 53, on ? noteName(link_.value(h, 72)).c_str() : "--", 11, Color(240, 240, 240), NanoVG::ALIGN_CENTER);
        pill(g, kStepUp.x, kStepUp.y, kStepUp.w, kStepUp.h, "\xE2\x80\xBA", false, on);
        // legend
        g.beginPath(); g.roundedRect(1046, 46, 10, 10, 2); g.fillColor(kYellow); g.fill();
        label(g, 1062, 52, "Lower", 11, Color(212, 214, 220), NanoVG::ALIGN_LEFT);
        g.beginPath(); g.roundedRect(1114, 46, 10, 10, 2); g.fillColor(kBlue); g.fill();
        label(g, 1130, 52, "Upper", 11, Color(212, 214, 220), NanoVG::ALIGN_LEFT);

        const float cw = colW();
        for (int half = 1; half >= 0; --half) {
            for (int ws = 7; ws >= 0; --ws) {
                const float y = rowY(half, ws);
                const bool selRow = on && half == h && ws == sel;
                const int top = on && link_.half(half).valid ? std::min(60, link_.half(half).wavesampleRaw(ws, topP)) : -1;
                const Color base = half ? kBlue : kYellow;
                for (int k = 0; k <= 60; ++k) {
                    const float x = colX(k);
                    g.beginPath(); g.rect(x + 0.5f, y + 0.5f, cw - 1, kRowStep - 1);
                    if (k <= top)
                        g.fillColor(Color(base.red, base.green, base.blue, selRow ? 1.0f : 0.30f));
                    else
                        g.fillColor(isBlack(k) ? Color(30, 31, 34) : Color(42, 43, 47));
                    g.fill();
                }
                char name[8];
                std::snprintf(name, sizeof name, "%c%d", half ? 'U' : 'L', ws + 1);
                if (selRow) { g.beginPath(); g.rect(20, y, 36, kRowStep); g.fillColor(Color(70, 72, 78)); g.fill(); }
                label(g, 50, y + kRowStep / 2 + 1, name, selRow ? 11 : 10, selRow ? Color(255, 255, 255) : kDim,
                      NanoVG::ALIGN_RIGHT);
            }
        }
        // the selected wavesample's range, repeated just above the keys
        if (on) {
            const int top = std::min(60, link_.value(h, 72));
            g.beginPath(); g.rect(kGX, kGKeysY - 6, colX(top + 1) - kGX, 3);
            g.fillColor(h ? kBlue : kYellow); g.fill();
        }
        // keys: white keys drawn, black keys left as gaps
        for (int k = 0; k <= 60; ++k) {
            if (isBlack(k)) continue;
            g.beginPath(); g.rect(colX(k) + 0.5f, kGKeysY, cw - 1, kGKeysH);
            g.fillColor(Color(226, 224, 216)); g.fill();
            if (k % 12 == 0) {
                char c[8];
                std::snprintf(c, sizeof c, "C%d", k / 12 + 2);
                label(g, colX(k) + 2, kGKeysY + kGKeysH - 7, c, 8, Color(110, 110, 110), NanoVG::ALIGN_LEFT);
            }
        }
        label(g, 20, 418, "Click a row to select that wavesample; click or drag in the selected row to set its top key.", 9,
              kDim, NanoVG::ALIGN_LEFT);
    }

    bool gridMouseDown(float x, float y)
    {
        const int h = link_.currentHalf(), sel = link_.half(h).wavesample;
        if (hit(kStepDown, x, y)) { link_.setParam(h, 72, link_.value(h, 72) - 1); return true; }
        if (hit(kStepUp, x, y)) { link_.setParam(h, 72, link_.value(h, 72) + 1); return true; }
        if (x < 20 || x > kGX + kGW) return true;
        for (int half = 0; half < 2; ++half)
            for (int ws = 0; ws < 8; ++ws) {
                const float ry = rowY(half, ws);
                if (y < ry || y >= ry + kRowStep) continue;
                if (half != h || ws != sel) { link_.selectWavesample(half, ws); return true; }
                if (x >= kGX) { dragGrid_ = true; gridSetTop(x); }
                return true;
            }
        return true;
    }

    void gridSetTop(float x)
    {
        const int h = link_.currentHalf();
        const int k = std::clamp(int((x - kGX) / colW()), 0, 60);
        if (k != link_.value(h, 72)) link_.setParam(h, 72, k);
    }

    void waveform(NanoVG& g, float x, float y, float w, float hgt)
    {
        g.beginPath(); g.roundedRect(x, y, w, hgt, 3); g.fillColor(Color(12, 13, 15)); g.fill();
        if (!link_.connected()) return;
        updateView();
        const int h = link_.currentHalf();
        const int s = link_.value(h, 60), e = link_.value(h, 61) + 1;
        const uint8_t* ram = engine_.machine().sampleRam() + (size_t(h) << 16);
        const float mid = y + hgt / 2, amp = hgt / 2 - 14;

        // loop region
        const bool loop = link_.value(h, 65) != 0;
        const float ls = pageX(link_.value(h, 62)), le = pageX(link_.value(h, 63) + 1);
        g.beginPath(); g.rect(ls, y + 1, std::max(1.0f, le - ls), hgt - 2);
        g.fillColor(loop ? Color(240, 180, 60, 0.16f) : Color(120, 120, 120, 0.10f)); g.fill();

        // waveform over the whole view; outside start..end it is dimmed
        const int first = viewFirst_ * 256, last = viewLast_ * 256;
        const int cols = int(w);
        const float xs = pageX(s), xe = pageX(e);
        for (int pass = 0; pass < 2; ++pass) {
            g.beginPath();
            for (int c = 0; c < cols; ++c) {
                const bool inside = x + c >= xs && x + c < xe;
                if (inside != (pass == 1)) continue;
                const int i0 = first + int(int64_t(last - first) * c / cols);
                const int i1 = std::max(i0 + 1, first + int(int64_t(last - first) * (c + 1) / cols));
                int lo = 255, hi = 0;
                for (int i = i0; i < i1 && i < 65536; ++i) { lo = std::min(lo, int(ram[i])); hi = std::max(hi, int(ram[i])); }
                const float y0 = mid - (hi - 128) / 128.0f * amp, y1 = mid - (lo - 128) / 128.0f * amp;
                g.rect(x + c, y0, 1, std::max(1.0f, y1 - y0));
            }
            g.fillColor(pass ? kYellow : Color(110, 92, 52)); g.fill();
        }

        // markers with tags: S and E (red, at the top), L (white, at the bottom)
        auto marker = [&](float mx, Color c, bool tagTop, const char* t) {
            g.beginPath(); g.rect(mx - 1, y + 1, 2, hgt - 2); g.fillColor(c); g.fill();
            const float ty = tagTop ? y + 2 : y + hgt - 14;
            g.beginPath(); g.roundedRect(mx - 6, ty, 12, 12, 2); g.fillColor(c); g.fill();
            label(g, mx, ty + 6.5f, t, 9, Color(16, 16, 16), NanoVG::ALIGN_CENTER);
        };
        const Color loopC = loop ? Color(245, 245, 245) : Color(130, 130, 130);
        marker(ls, loopC, false, "L");
        marker(le, loopC, false, "L");
        marker(xs, Color(232, 60, 50), true, "S");
        marker(xe, Color(232, 60, 50), true, "E");
        label(g, x + w - 6, y + hgt + 8, "drag S / E for the sample, L for the loop", 9, kDim, NanoVG::ALIGN_RIGHT);
    }
};

END_NAMESPACE_DISTRHO
