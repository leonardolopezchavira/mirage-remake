// Mirage editor, modelled on the front panel of the Ensoniq Mirage DMS-8 rack unit:
// slate panel, two-digit yellow LED display, small flat yellow / grey / white / black keys,
// section lines and labels (POWER, VOLUME, CONTROL, SELECT, SEQ, SAMPLE, LOAD), the floppy
// drive and the Mirage logo. A 61-key keyboard strip sits below the rack unit.
// NanoVG, drawn in a 1200 x 410 coordinate space scaled to the window.
#include "DistrhoUI.hpp"
#include "MiragePlugin.hpp"
#include "StandaloneHook.h"
#include "EmbeddedData.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using mirage::Mirage;

namespace {
constexpr float kW = 1200, kRackH = 262, kH = 410;

// Panel colours (from photographs of a DMS-8)
const Color kPanelTop(46, 50, 57), kPanelBottom(33, 36, 42);
const Color kLine(132, 142, 156), kLabel(186, 192, 202);
const Color kKeyGrey(150, 155, 162), kKeyGreyDark(118, 122, 130);
const Color kKeyYellow(226, 168, 40), kKeyYellowDark(186, 132, 22);
const Color kKeyWhite(236, 238, 240), kKeyWhiteDark(200, 204, 210);
const Color kKeyBlackDark(16, 17, 20);
const Color kLedOn(250, 240, 70), kLedOff(30, 30, 14), kLedWindow(12, 14, 9);

enum KeyColour { Grey, Yellow, White, Black, Small };
enum Action { ActNone = 0, ActDiskPrev, ActDiskNext, ActDiskOpen, ActFactoryTuning };

struct Button {
    float cx, cy;        // centre of the key
    const char* label;   // printed above the key (inside for Small)
    int key;             // Mirage::Key, or -1
    int action;          // editor action when key == -1
    KeyColour colour;
    float w, h;
};

// Keyboard strip: 61 keys, C (MIDI 36) to C (MIDI 96).
constexpr int kFirstNote = 36, kNumKeys = 61;
constexpr float kKbX = 30, kKbY = kRackH + 22, kKbW = kW - 60, kKbH = kH - kRackH - 34;
constexpr float kVolTop = 62, kVolBot = 222;
} // namespace

class MirageUI : public UI {
public:
    MirageUI() : UI(int(kW), int(kH))
    {
        loadSharedResources();
        // Our own scale() handles sizing (no DPF auto-scaling, which would also rescale
        // mouse coordinates and getWidth()).
        setGeometryConstraints(uint(kW * 0.6f), uint(kH * 0.6f), true, false);
        plugin_ = static_cast<MiragePlugin*>(getPluginInstancePointer());
        // Standalone app only (null in every plug-in format): menus, settings, MIDI inputs.
        if (g_mirageStandaloneStart) g_mirageStandaloneStart();

        // Rows and columns as on the DMS-8.
        const float r1 = 42, r2 = 100, r3 = 158, r4 = 215;
        const float c1 = 408, c2 = 461, c3 = 514;
        // CONTROL
        add(274, r3, "Param", Mirage::Param, Grey);
        add(337, r3, "Value", Mirage::Value, Grey);
        add(274, r4, "Off/\xE2\x96\xBC", Mirage::Down, Grey);
        add(337, r4, "On/\xE2\x96\xB2", Mirage::Up, Grey);
        // SELECT
        add(c1, r1, "1", Mirage::K1, Yellow); add(c2, r1, "2", Mirage::K2, Yellow); add(c3, r1, "3", Mirage::K3, Yellow);
        add(c1, r2, "4", Mirage::K4, Yellow); add(c2, r2, "5", Mirage::K5, Grey);   add(c3, r2, "6", Mirage::K6, Grey);
        add(c1, r3, "7", Mirage::K7, Grey);   add(c2, r3, "8", Mirage::K8, Grey);   add(c3, r3, "9", Mirage::K9, Grey);
        add(c1, r4, "Cancel", Mirage::Cancel, Grey);
        add(c2, r4, "0/Prog", Mirage::K0Prog, Yellow);
        add(c3, r4, "Enter", Mirage::Enter, White);
        // SEQ, SAMPLE, LOAD
        add(589, r1, "Rec", Mirage::RecSeq, Grey);
        add(589, r2, "Play", Mirage::PlaySeq, Grey);
        add(589, r3, "Load", Mirage::LoadSeq, Grey);
        add(589, r4, "Save", Mirage::SaveSeq, Grey);
        add(659, r1, "Upper", Mirage::SampleUpper, Black);
        add(659, r2, "Lower", Mirage::SampleLower, Black);
        add(752, r1, "Upper", Mirage::LoadUpper, White);
        add(752, r2, "Lower", Mirage::LoadLower, White);

        // Factory tuning switch (an addition), above the power switch
        buttons_.push_back({57, 100, "", -1, ActFactoryTuning, Grey, 30, 13});

        // Disk library (an addition: picks the disk image in the drive)
        buttons_.push_back({868, 152, "\xE2\x97\x80", -1, ActDiskPrev, Small, 22, 13});
        buttons_.push_back({896, 152, "\xE2\x96\xB6", -1, ActDiskNext, Small, 22, 13});
        buttons_.push_back({1126, 152, "Open\xE2\x80\xA6", -1, ActDiskOpen, Small, 44, 13});

        const std::string cur = plugin_->engine().diskName();
        for (size_t i = 0; i < plugin_->disks().size(); ++i)
            if (plugin_->disks()[i].name == cur) sel_ = int(i);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index == kParamVolume) { volume_ = value; repaint(); }
        if (index == kParamFactoryTuning) { factoryTuning_ = value > 0.5f; repaint(); }
    }

    void uiIdle() override
    {
        auto& e = plugin_->engine();
        const uint8_t d0 = e.display(0), d1 = e.display(1);
        const bool motor = e.diskActive();
        if (d0 != shown0_ || d1 != shown1_ || motor != shownMotor_) {
            shown0_ = d0; shown1_ = d1; shownMotor_ = motor;
            repaint();
        }
        // input meter: peak since the last idle call, with a gentle fall-back
        const float peak = e.inputPeak.exchange(0.0f);
        const float lvl = std::max(peak, inLevel_ * 0.82f);
        if (std::fabs(lvl - inLevel_) > 0.002f || (lvl > 0 && lvl < 0.002f)) {
            inLevel_ = lvl < 0.0005f ? 0.0f : lvl;
            repaint();
        }
        const std::string saved = plugin_->flushWrittenDisk();
        if (!saved.empty()) { status_ = "Saved to your disk folder"; repaint(); }
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename == nullptr) return;
        std::string err;
        if (plugin_->loadDiskFile(filename, &err)) {
            sel_ = -1;
            status_.clear();
        } else {
            status_ = err;
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float s = getWidth() / kW;
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        drawRack();
        drawKeyboard();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = getWidth() / kW;
        const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
        if (ev.button != 1) return false;
        if (ev.press) {
            if (x >= 140 && x <= 205 && y >= kVolTop - 8 && y <= kVolBot + 8) { draggingVol_ = true; setVolumeFromY(y); return true; }
            for (size_t i = 0; i < buttons_.size(); ++i) {
                const auto& b = buttons_[i];
                // generous hit area: the key plus its label
                const float top = b.colour == Small ? b.cy - b.h / 2 - 3 : b.cy - b.h / 2 - 16;
                if (x >= b.cx - b.w / 2 - 6 && x <= b.cx + b.w / 2 + 6 && y >= top && y <= b.cy + b.h / 2 + 5) {
                    pressed_ = int(i);
                    if (b.key >= 0) plugin_->engine().setPanelKey(b.key, true);
                    else doAction(b.action);
                    repaint();
                    return true;
                }
            }
            const int n = noteAt(x, y);
            if (n >= 0) { noteOn(n); return true; }
        } else {
            draggingVol_ = false;
            if (pressed_ >= 0) {
                const auto& b = buttons_[size_t(pressed_)];
                if (b.key >= 0) plugin_->engine().setPanelKey(b.key, false);
                pressed_ = -1;
                repaint();
            }
            if (playing_ >= 0) noteOff();
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float s = getWidth() / kW;
        const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
        if (draggingVol_) { setVolumeFromY(y); return true; }
        if (playing_ >= 0) {
            const int n = noteAt(x, y);
            if (n != playing_) { noteOff(); if (n >= 0) noteOn(n); }
            return true;
        }
        return false;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        int key = -1;
        switch (ev.key) {
        case '0': key = Mirage::K0Prog; break;
        case '1': key = Mirage::K1; break;  case '2': key = Mirage::K2; break;
        case '3': key = Mirage::K3; break;  case '4': key = Mirage::K4; break;
        case '5': key = Mirage::K5; break;  case '6': key = Mirage::K6; break;
        case '7': key = Mirage::K7; break;  case '8': key = Mirage::K8; break;
        case '9': key = Mirage::K9; break;
        case kKeyEnter: key = Mirage::Enter; break;
        case kKeyEscape: key = Mirage::Cancel; break;
        case kKeyUp: key = Mirage::Up; break;
        case kKeyDown: key = Mirage::Down; break;
        case 'p': key = Mirage::Param; break;
        case 'v': key = Mirage::Value; break;
        default: break;
        }
        if (key < 0) return false;
        plugin_->engine().setPanelKey(key, ev.press);
        for (size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i].key == key) pressed_ = ev.press ? int(i) : -1;
        repaint();
        return true;
    }

private:
    MiragePlugin* plugin_;
    std::vector<Button> buttons_;
    int pressed_ = -1, playing_ = -1, sel_ = -1;
    uint8_t shown0_ = 0, shown1_ = 0;
    bool shownMotor_ = false, draggingVol_ = false;
    float volume_ = 0.8f;
    std::string status_;
    NanoImage wordmark_;
    float inLevel_ = 0.0f;
    bool factoryTuning_ = true;

    void add(float cx, float cy, const char* label, int key, KeyColour c)
    {
        buttons_.push_back({cx, cy, label, key, 0, c, 36, 16});
    }

    // ------------------------------------------------------------------ rack panel
    void drawRack()
    {
        beginPath(); rect(0, 0, kW, kRackH);
        fillPaint(linearGradient(0, 0, 0, kRackH, kPanelTop, kPanelBottom)); fill();
        beginPath(); rect(0, 0, kW, 2); fillColor(Color(78, 84, 94)); fill();
        beginPath(); rect(0, kRackH - 3, kW, 3); fillColor(Color(16, 17, 20)); fill();

        drawEnsoniqLogo(10, 14, 144, 26);

        // POWER rocker (always on)
        beginPath(); roundedRect(38, 128, 38, 80, 3); fillColor(Color(14, 15, 17)); fill();
        beginPath(); roundedRect(43, 133, 28, 70, 2);
        fillPaint(linearGradient(43, 133, 43, 203, Color(46, 48, 52), Color(24, 25, 28))); fill();
        beginPath(); rect(43, 133, 28, 34); fillColor(Color(56, 58, 63)); fill();

        drawVolume();
        drawDisplay();

        // Section rules between the rows
        hline(394, 766, 66, 2);
        hline(394, 766, 124, 2);
        hline(262, 640, 181, 2);      // gap for the INPUT meter
        hline(678, 800, 181, 2);
        // Group bars and labels along the bottom
        groupBar(30, 92, "POWER");
        groupBar(100, 248, "VOLUME");
        groupBar(254, 360, "CONTROL");
        groupBar(384, 540, "SELECT");
        groupBar(566, 612, "SEQ");
        groupBar(626, 694, "SAMPLE");
        groupBar(726, 778, "LOAD");

        for (size_t i = 0; i < buttons_.size(); ++i) drawKey(buttons_[i], int(i) == pressed_);
        // Factory tuning label and LED
        fontSize(8.5f); fillColor(kLabel); textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        text(57, 80, "FACTORY", nullptr);
        text(57, 90, "TUNING", nullptr);
        beginPath(); circle(57, 117, 3.2f);
        fillColor(factoryTuning_ ? Color(255, 60, 40) : Color(60, 18, 14)); fill();

        drawDrive();
        drawInputMeter(659, 146, 220);

        drawBrand();
    }

    void hline(float x0, float x1, float y, float t)
    {
        beginPath(); rect(x0, y - t / 2, x1 - x0, t); fillColor(kLine); fill();
    }

    void groupBar(float x0, float x1, const char* t)
    {
        hline(x0, x1, 236, 3);
        fontSize(10.5f); fillColor(kLabel); textAlign(ALIGN_CENTER | ALIGN_TOP);
        text((x0 + x1) / 2, 241, t, nullptr);
    }

    // right-aligned text with extra letter spacing
    void spacedText(float right, float y, const char* t, float spacing)
    {
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        std::vector<float> widths;
        float total = 0;
        for (const char* p = t; *p; ++p) {
            const char c[2] = {*p, 0};
            Rectangle<float> b;
            const float adv = textBounds(0, 0, c, nullptr, b);
            widths.push_back(adv);
            total += adv + spacing;
        }
        float x = right - total;
        size_t i = 0;
        for (const char* p = t; *p; ++p, ++i) {
            const char c[2] = {*p, 0};
            if (*p != ' ') text(x, y, c, nullptr);
            x += widths[i] + spacing;
        }
    }

    void drawEnsoniqLogo(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 4);
        strokeColor(Color(150, 156, 166)); strokeWidth(2); stroke();
        beginPath(); roundedRect(x + 3, y + 3, w - 6, h - 6, 2);
        fillColor(Color(40, 43, 49)); fill();
        fontSize(21); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(176, 182, 192));
        text(x + w / 2, y + h / 2 + 1, "ensoniq", nullptr);
        text(x + w / 2 + 0.5f, y + h / 2 + 1, "ensoniq", nullptr);
    }

    // "Mirage" in the wordmark lettering (the same drawing as the app icon's "Mi"), the
    // grey and red stripes, and the legend in small spaced capitals, as on the DSK panel.
    void drawBrand()
    {
        const float right = 1150, capTop = 176, capH = 40;
        // stripes: grey above red, from the drive column to the panel's right edge
        const float gy = 224, ry = 232;
        beginPath(); rect(806, gy, kW - 806, 2.5f); fillColor(Color(200, 202, 206)); fill();
        beginPath(); rect(806, ry, kW - 806, 4.0f); fillColor(Color(214, 34, 30)); fill();
        // wordmark image: cap height 160 px at y = (0.05 + 0.12) * 160 in the PNG
        if (!wordmark_.isValid())
            wordmark_ = createImageFromMemory(embedded::kWordmarkPng, uint(embedded::kWordmarkPngSize), IMAGE_GENERATE_MIPMAPS);
        if (wordmark_.isValid()) {
            const Size<uint> sz = wordmark_.getSize();
            const float k = capH / 160.0f;
            const float w = sz.getWidth() * k, h = sz.getHeight() * k;
            const float x = right - w, y = capTop - (0.05f + 0.12f) * 160.0f * k;
            beginPath(); rect(x, y, w, h);
            fillPaint(imagePattern(x, y, w, h, 0, wordmark_, 1.0f));
            fill();
        }
        fontSize(9.0f); fillColor(Color(232, 232, 232));
        spacedText(right, 248, "VINTAGE  DIGITAL  SAMPLER", 2.8f);
    }

    void drawVolume()
    {
        const float sx = 172;
        for (int i = 0; i <= 8; ++i) {
            const float ty = kVolTop + 12 + i * (kVolBot - kVolTop - 24) / 8;
            beginPath(); rect(sx - 30, ty, 9, 1.2f); fillColor(kLine); fill();
            beginPath(); rect(sx + 21, ty, 9, 1.2f); fillColor(kLine); fill();
        }
        beginPath(); roundedRect(sx - 15, kVolTop, 30, kVolBot - kVolTop, 3); fillColor(Color(12, 13, 15)); fill();
        beginPath(); rect(sx - 2, kVolTop + 8, 4, kVolBot - kVolTop - 16); fillColor(Color(4, 4, 5)); fill();
        const float cy = kVolTop + 12 + (1.0f - volume_) * (kVolBot - kVolTop - 24);
        beginPath(); roundedRect(sx - 14, cy - 14, 28, 28, 2);
        fillPaint(linearGradient(sx, cy - 14, sx, cy + 14, Color(40, 42, 46), Color(18, 19, 22))); fill();
        beginPath(); rect(sx - 12, cy - 1.5f, 24, 3); fillColor(Color(230, 232, 236)); fill();
    }

    void drawDisplay()
    {
        const float x = 262, y = 40, w = 80, h = 58;
        beginPath(); roundedRect(x - 4, y - 4, w + 8, h + 8, 3); fillColor(Color(10, 11, 12)); fill();
        beginPath(); rect(x, y, w, h); fillColor(kLedWindow); fill();
        drawDigit(x + 10, y + 7, 28, 44, shown0_);
        drawDigit(x + 46, y + 7, 28, 44, shown1_);
        // brackets from the digits down to Param / Value
        const float by = y + h + 12;
        strokeColor(kLine); strokeWidth(1.5f);
        beginPath(); moveTo(x + 22, y + h + 4); lineTo(x + 22, by); stroke();
        beginPath(); moveTo(258, by); lineTo(292, by); stroke();
        beginPath(); moveTo(x + 60, y + h + 4); lineTo(x + 60, by); lineTo(356, by); stroke();
    }

    // Seven-segment digit; bits a..g = 0..6, dp = 7 (drawn at the lower left, as on the unit).
    void drawDigit(float x, float y, float w, float h, uint8_t segs)
    {
        const float t = w * 0.17f, sk = w * 0.18f;
        auto X = [&](float px, float py) { return px + sk * (1.0f - (py - y) / h); };
        auto seg = [&](int bit, float x0, float y0, float x1, float y1) {
            const bool on = segs & (1 << bit);
            beginPath();
            if (y0 == y1) {
                moveTo(X(x0 + t * 0.55f, y0), y0);
                lineTo(X(x0 + t, y0 - t / 2), y0 - t / 2);
                lineTo(X(x1 - t, y0 - t / 2), y0 - t / 2);
                lineTo(X(x1 - t * 0.55f, y0), y0);
                lineTo(X(x1 - t, y0 + t / 2), y0 + t / 2);
                lineTo(X(x0 + t, y0 + t / 2), y0 + t / 2);
            } else {
                moveTo(X(x0, y0 + t * 0.55f), y0 + t * 0.55f);
                lineTo(X(x0 + t / 2, y0 + t), y0 + t);
                lineTo(X(x0 + t / 2, y1 - t), y1 - t);
                lineTo(X(x0, y1 - t * 0.55f), y1 - t * 0.55f);
                lineTo(X(x0 - t / 2, y1 - t), y1 - t);
                lineTo(X(x0 - t / 2, y0 + t), y0 + t);
            }
            closePath();
            fillColor(on ? kLedOn : kLedOff);
            fill();
            if (on) { strokeColor(Color(246, 236, 60, 60)); strokeWidth(2.5f); stroke(); }
        };
        const float x0 = x + t / 2, x1 = x + w - t / 2 - sk, y0 = y + t / 2, ym = y + h / 2, y1 = y + h - t / 2;
        seg(0, x0, y0, x1, y0);
        seg(1, x1, y0, x1, ym);
        seg(2, x1, ym, x1, y1);
        seg(3, x0, y1, x1, y1);
        seg(4, x0, ym, x0, y1);
        seg(5, x0, y0, x0, ym);
        seg(6, x0, ym, x1, ym);
        beginPath(); circle(X(x - t * 0.6f, y1), y1, t * 0.42f);
        fillColor((segs & 0x80) ? kLedOn : kLedOff); fill();
    }

    void drawKey(const Button& b, bool down)
    {
        Color top = kKeyGrey, bot = kKeyGreyDark;
        switch (b.colour) {
        case Yellow: top = kKeyYellow; bot = kKeyYellowDark; break;
        case White:  top = kKeyWhite;  bot = kKeyWhiteDark;  break;
        case Black:  top = Color(44, 46, 52); bot = kKeyBlackDark; break;
        case Small:  top = Color(120, 125, 133); bot = Color(92, 96, 104); break;
        default: break;
        }
        const float x = b.cx - b.w / 2, y = b.cy - b.h / 2 + (down ? 1.0f : 0.0f);
        beginPath(); roundedRect(x + 0.5f, b.cy - b.h / 2 + 2, b.w, b.h, 1.5f); fillColor(Color(0, 0, 0, 120)); fill();
        beginPath(); roundedRect(x, y, b.w, b.h, 1.5f);
        fillPaint(linearGradient(x, y, x, y + b.h, down ? bot : top, bot));
        fill();
        if (b.colour == Black) { strokeColor(Color(90, 94, 102)); strokeWidth(1); stroke(); }
        if (!down) { beginPath(); rect(x + 1.5f, y + 1, b.w - 3, 1); fillColor(Color(255, 255, 255, 70)); fill(); }

        if (b.colour == Small) {
            fontSize(9); fillColor(Color(235, 236, 240)); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(b.cx, y + b.h / 2 + 0.5f, b.label, nullptr);
        } else {
            fontSize(11); fillColor(kLabel); textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
            text(b.cx, b.cy - b.h / 2 - 3, b.label, nullptr);
        }
    }

    // Audio input level (an addition: the Mirage itself shows the level on its display in
    // level-detect mode). 8 LEDs, -42 dBFS to 0 dBFS, 6 dB apart.
    void drawInputMeter(float cx, float top, float bottom)
    {
        fontSize(9.5f); fillColor(kLabel); textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(cx, top, "INPUT", nullptr);
        const int n = 8;
        const float y0 = top + 16, h = (bottom - y0) / n;
        const float db = inLevel_ > 0 ? 20.0f * std::log10(inLevel_) : -100.0f;
        for (int i = 0; i < n; ++i) {
            const float threshold = -42.0f + 6.0f * i;     // bottom LED lights at -42 dBFS
            const bool on = db >= threshold;
            Color c = i >= 7 ? Color(255, 40, 30) : i >= 5 ? Color(250, 210, 40) : Color(80, 230, 70);
            Color off = i >= 7 ? Color(60, 14, 12) : i >= 5 ? Color(56, 48, 12) : Color(18, 50, 16);
            const float y = bottom - (i + 1) * h + 1;
            beginPath(); roundedRect(cx - 9, y, 18, h - 2.5f, 1);
            fillColor(on ? c : off); fill();
        }
    }

    void drawDrive()
    {
        const float x = 853, y = 30, w = 295, h = 98;
        beginPath(); roundedRect(x, y, w, h, 3); fillColor(Color(18, 19, 22)); fill();
        beginPath(); roundedRect(x + 4, y + 4, w - 8, h - 8, 2);
        fillPaint(linearGradient(x, y, x, y + h, Color(34, 36, 40), Color(22, 23, 26))); fill();
        // slot and door
        beginPath(); roundedRect(x + 22, y + 22, w - 44, 14, 2); fillColor(Color(6, 6, 7)); fill();
        beginPath(); rect(x + 26, y + 27, w - 52, 4); fillColor(Color(44, 46, 50)); fill();
        // activity LED and eject button
        beginPath(); roundedRect(x + 26, y + 58, 14, 5, 1);
        fillColor(shownMotor_ ? Color(90, 255, 80) : Color(20, 50, 18)); fill();
        beginPath(); roundedRect(x + w - 70, y + 54, 44, 16, 2);
        fillPaint(linearGradient(x, y + 54, x, y + 70, Color(64, 66, 72), Color(38, 40, 44))); fill();

        // the inserted disk, written on the drive face
        const auto& disks = plugin_->disks();
        std::string name = plugin_->engine().diskName(), lab;
        if (sel_ >= 0 && sel_ < int(disks.size())) { name = disks[size_t(sel_)].name; lab = disks[size_t(sel_)].label; }
        fontSize(11.5f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(Color(210, 214, 220));
        if (!name.empty()) text(x + 50, y + 61, name.c_str(), nullptr);
        if (!lab.empty()) {
            fontSize(9); fillColor(Color(146, 152, 162));
            const std::string l = lab.size() > 44 ? lab.substr(0, 43) + "\xE2\x80\xA6" : lab;
            text(x + 26, y + 82, l.c_str(), nullptr);
        }

        // disk library row
        fontSize(9.5f); fillColor(kLabel); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(914, 152, status_.empty() ? "DISK LIBRARY" : status_.c_str(), nullptr);
    }

    // ------------------------------------------------------------------ keyboard strip
    static bool isBlack(int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

    int noteAt(float x, float y) const
    {
        if (x < kKbX || x > kKbX + kKbW || y < kKbY || y > kKbY + kKbH) return -1;
        const float ww = kKbW / 36;
        if (y < kKbY + kKbH * 0.62f) {
            for (int n = kFirstNote; n < kFirstNote + kNumKeys; ++n) {
                if (!isBlack(n)) continue;
                const float bx = blackX(n, ww);
                if (x >= bx && x <= bx + ww * 0.62f) return n;
            }
        }
        const int wi = int((x - kKbX) / ww);
        int count = 0;
        for (int n = kFirstNote; n < kFirstNote + kNumKeys; ++n)
            if (!isBlack(n)) { if (count == wi) return n; ++count; }
        return -1;
    }

    static float blackX(int n, float ww)
    {
        int whitesBefore = 0;
        for (int m = kFirstNote; m < n; ++m) if (!isBlack(m)) ++whitesBefore;
        return kKbX + whitesBefore * ww - ww * 0.31f;
    }

    void drawKeyboard()
    {
        beginPath(); rect(0, kRackH, kW, kH - kRackH); fillColor(Color(20, 21, 24)); fill();
        beginPath(); rect(kKbX - 6, kKbY - 8, kKbW + 12, kKbH + 14); fillColor(Color(8, 8, 9)); fill();
        const float ww = kKbW / 36;
        int wi = 0;
        for (int n = kFirstNote; n < kFirstNote + kNumKeys; ++n) {
            if (isBlack(n)) continue;
            beginPath(); roundedRect(kKbX + wi * ww + 0.6f, kKbY, ww - 1.2f, kKbH, 2);
            fillPaint(linearGradient(0, kKbY, 0, kKbY + kKbH,
                                     n == playing_ ? Color(196, 200, 210) : Color(244, 242, 236),
                                     n == playing_ ? Color(170, 176, 188) : Color(222, 220, 212)));
            fill();
            ++wi;
        }
        for (int n = kFirstNote; n < kFirstNote + kNumKeys; ++n) {
            if (!isBlack(n)) continue;
            const float bx = blackX(n, ww);
            beginPath(); roundedRect(bx, kKbY, ww * 0.62f, kKbH * 0.62f, 2);
            fillPaint(linearGradient(0, kKbY, 0, kKbY + kKbH * 0.62f,
                                     n == playing_ ? Color(90, 96, 110) : Color(40, 40, 44),
                                     n == playing_ ? Color(70, 76, 90) : Color(10, 10, 12)));
            fill();
        }
    }

    // ------------------------------------------------------------------ actions
    void setVolumeFromY(float y)
    {
        volume_ = std::fmin(1.0f, std::fmax(0.0f, 1.0f - (y - kVolTop - 12) / (kVolBot - kVolTop - 24)));
        setParameterValue(kParamVolume, volume_);
        plugin_->engine().volume.store(volume_);
        repaint();
    }

    void doAction(int a)
    {
        const auto& disks = plugin_->disks();
        if (a == ActFactoryTuning) {
            factoryTuning_ = !factoryTuning_;
            setParameterValue(kParamFactoryTuning, factoryTuning_ ? 1.0f : 0.0f);
            plugin_->engine().factoryTuning.store(factoryTuning_);
            plugin_->savePreference("factory_tuning", factoryTuning_ ? 1.0f : 0.0f);
            status_ = factoryTuning_ ? "Factory tuning on: Ensoniq disks at A440"
                                     : "Factory tuning off: nominal 8 MHz clock (third-party disks)";
            return;
        }
        if (a == ActDiskOpen) {
            FileBrowserOptions opts;
            opts.title = "Insert a Mirage disk image (.img, .hfe, .edm)";
            const std::string dir = plugin_->userDiskDir();
            opts.startDir = dir.c_str();
            openFileBrowser(opts);
            return;
        }
        if (disks.empty()) { status_ = "No disks found"; return; }
        if (a == ActDiskPrev) sel_ = sel_ <= 0 ? int(disks.size()) - 1 : sel_ - 1;
        if (a == ActDiskNext) sel_ = sel_ < 0 || sel_ >= int(disks.size()) - 1 ? 0 : sel_ + 1;
        std::string err;
        status_ = plugin_->loadDiskFile(disks[size_t(sel_)].path, &err) ? "" : err;
    }

    void noteOn(int n)
    {
        const uint8_t m[3] = {0x90, uint8_t(n), 100};
        plugin_->engine().postMidi(m, 3);
        playing_ = n;
        repaint();
    }
    void noteOff()
    {
        const uint8_t m[3] = {0x80, uint8_t(playing_), 64};
        plugin_->engine().postMidi(m, 3);
        playing_ = -1;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MirageUI)
};

void (*g_mirageStandaloneStart)() = nullptr;

UI* createUI() { return new MirageUI(); }

END_NAMESPACE_DISTRHO
