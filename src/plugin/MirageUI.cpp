// Mirage editor: front panel (two-digit LED display, keypad, load/sample/sequencer keys),
// a disk drive with the bundled disk library, and a 61-key keyboard. NanoVG, drawn at any size.
#include "DistrhoUI.hpp"
#include "MiragePlugin.hpp"
#include "StandaloneHook.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using mirage::Mirage;

namespace {
constexpr float kW = 960, kH = 440;

struct Button {
    float x, y, w, h;
    const char* label;
    const char* sub;   // second line (or nullptr)
    int key;           // Mirage::Key, or -1
    int action;        // editor action when key == -1
};

enum Action { ActNone = 0, ActDiskPrev, ActDiskNext, ActDiskOpen, ActDiskInsert };

// Keyboard: 61 keys, C (MIDI 36) to C (MIDI 96).
constexpr int kFirstNote = 36, kNumKeys = 61;
constexpr float kKbX = 20, kKbY = 300, kKbW = 920, kKbH = 125;
} // namespace

class MirageUI : public UI {
public:
    MirageUI() : UI(int(kW), int(kH))
    {
        loadSharedResources();
        // Our own scale() handles sizing (no DPF auto-scaling, which would also rescale
        // mouse coordinates and getWidth()).
        setGeometryConstraints(uint(kW * 0.75f), uint(kH * 0.75f), true, false);
        plugin_ = static_cast<MiragePlugin*>(getPluginInstancePointer());
        // Standalone app only (null in every plug-in format): menus, settings, MIDI inputs.
        if (g_mirageStandaloneStart) g_mirageStandaloneStart();

        const float kx = 520, ky = 58, bw = 62, bh = 42, gx = 70, gy = 50;
        // keypad, as on the instrument
        const struct { const char* l; const char* s; int k; } pad[4][4] = {
            {{"1", nullptr, Mirage::K1}, {"2", nullptr, Mirage::K2}, {"3", nullptr, Mirage::K3}, {"ON", "\xE2\x96\xB2", Mirage::Up}},
            {{"4", nullptr, Mirage::K4}, {"5", nullptr, Mirage::K5}, {"6", nullptr, Mirage::K6}, {"OFF", "\xE2\x96\xBC", Mirage::Down}},
            {{"7", nullptr, Mirage::K7}, {"8", nullptr, Mirage::K8}, {"9", nullptr, Mirage::K9}, {"PARAM", nullptr, Mirage::Param}},
            {{"CANCEL", nullptr, Mirage::Cancel}, {"0", "PROG", Mirage::K0Prog}, {"ENTER", nullptr, Mirage::Enter}, {"VALUE", nullptr, Mirage::Value}},
        };
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                buttons_.push_back({kx + c * gx, ky + r * gy, bw, bh, pad[r][c].l, pad[r][c].s, pad[r][c].k, 0});

        const float lx = 250;
        buttons_.push_back({lx, 58, 54, 42, "LOAD", "UPPER", Mirage::LoadUpper, 0});
        buttons_.push_back({lx + 60, 58, 54, 42, "LOAD", "LOWER", Mirage::LoadLower, 0});
        buttons_.push_back({lx, 108, 54, 42, "SAMPLE", "UPPER", Mirage::SampleUpper, 0});
        buttons_.push_back({lx + 60, 108, 54, 42, "SAMPLE", "LOWER", Mirage::SampleLower, 0});
        const float sx = 385;
        buttons_.push_back({sx, 58, 54, 42, "PLAY", "SEQ", Mirage::PlaySeq, 0});
        buttons_.push_back({sx + 60, 58, 54, 42, "REC", "SEQ", Mirage::RecSeq, 0});
        buttons_.push_back({sx, 108, 54, 42, "LOAD", "SEQ", Mirage::LoadSeq, 0});
        buttons_.push_back({sx + 60, 108, 54, 42, "SAVE", "SEQ", Mirage::SaveSeq, 0});

        // disk drive
        buttons_.push_back({812, 194, 38, 28, "\xE2\x97\x80", nullptr, -1, ActDiskPrev});
        buttons_.push_back({854, 194, 38, 28, "\xE2\x96\xB6", nullptr, -1, ActDiskNext});
        buttons_.push_back({812, 228, 80, 26, "OPEN\xE2\x80\xA6", nullptr, -1, ActDiskOpen});

        // Show the disk that is in the drive.
        const std::string cur = plugin_->engine().diskName();
        for (size_t i = 0; i < plugin_->disks().size(); ++i)
            if (plugin_->disks()[i].name == cur) sel_ = int(i);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index == kParamVolume) volume_ = value;
        repaint();
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
        const std::string saved = plugin_->flushWrittenDisk();
        if (!saved.empty()) { status_ = "Saved " + saved; repaint(); }
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename == nullptr) return;
        if (plugin_->loadDiskFile(filename)) {
            status_ = std::string("Inserted ") + filename;
            sel_ = -1;
            customName_ = filename;
        } else {
            status_ = "Not a Mirage disk image (needs 450560 bytes)";
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float s = getWidth() / kW;
        scale(s, s);

        // panel
        beginPath(); rect(0, 0, kW, kH);
        fillPaint(linearGradient(0, 0, 0, kH, Color(52, 52, 56), Color(30, 30, 33))); fill();
        beginPath(); rect(0, 0, kW, 34); fillColor(Color(20, 20, 22)); fill();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(20); fillColor(Color(230, 230, 230)); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(20, 17, "ensoniq", nullptr);
        fontSize(22); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE); fillColor(Color(200, 205, 215));
        text(kW - 20, 17, "M I R A G E", nullptr);
        fontSize(10); fillColor(Color(130, 130, 140)); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(kW - 160, 18, "DIGITAL SAMPLING KEYBOARD", nullptr);

        // volume slider
        label(40, 52, "VOLUME");
        beginPath(); roundedRect(34, 62, 14, 150, 4); fillColor(Color(15, 15, 16)); fill();
        const float vy = 62 + (1.0f - volume_) * 138;
        beginPath(); roundedRect(26, vy, 30, 14, 3); fillColor(Color(190, 190, 195)); fill();

        // LED display
        beginPath(); roundedRect(80, 62, 150, 96, 6); fillColor(Color(12, 4, 4)); fill();
        strokeColor(Color(70, 70, 75)); strokeWidth(2); stroke();
        drawDigit(96, 74, 52, 72, shown0_);
        drawDigit(160, 74, 52, 72, shown1_);

        // groups
        groupFrame(244, 44, 128, 114, "");
        groupFrame(379, 44, 128, 114, "SEQUENCER");
        groupFrame(512, 44, 286, 214, "");
        for (const auto& b : buttons_) drawButton(b);

        // disk drive
        groupFrame(806, 44, 134, 214, "DISK");
        beginPath(); roundedRect(814, 62, 118, 40, 3); fillColor(Color(18, 18, 20)); fill();
        beginPath(); rect(826, 79, 94, 5); fillColor(Color(5, 5, 6)); fill();
        beginPath(); circle(922, 94, 4);
        fillColor(shownMotor_ ? Color(255, 60, 40) : Color(70, 20, 15)); fill();
        fontSize(18); textAlign(ALIGN_CENTER | ALIGN_TOP); fillColor(Color(235, 235, 235));
        const auto& disks = plugin_->disks();
        std::string name = plugin_->engine().diskName(), lab;
        if (sel_ >= 0 && sel_ < int(disks.size())) { name = disks[size_t(sel_)].name; lab = disks[size_t(sel_)].label; }
        if (!name.empty()) text(873, 110, name.c_str(), nullptr);
        fontSize(10); fillColor(Color(170, 170, 180));
        if (!lab.empty()) textBox(812, 132, 122, lab.c_str(), nullptr);
        fontSize(9); fillColor(Color(120, 120, 130)); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(812, 262, "\xE2\x97\x80 \xE2\x96\xB6 insert disk, then LOAD", nullptr);

        // help line / status
        fontSize(11); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(Color(150, 150, 160));
        text(20, 278, status_.empty()
             ? "Change sound: LOAD LOWER (or UPPER), 1-3, ENTER.   Edit: PARAM, two digits, VALUE, \xE2\x96\xB2\xE2\x96\xBC."
             : status_.c_str(), nullptr);

        drawKeyboard();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = getWidth() / kW;
        const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
        if (std::getenv("MIRAGE_TRACE")) std::fprintf(stderr, "mouse b=%u press=%d %.0f,%.0f s=%.2f\n", ev.button, ev.press, x, y, s);
        if (ev.button != 1) return false;
        if (ev.press) {
            if (x >= 20 && x <= 60 && y >= 55 && y <= 220) { draggingVol_ = true; setVolumeFromY(y); return true; }
            for (size_t i = 0; i < buttons_.size(); ++i) {
                const auto& b = buttons_[i];
                if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
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
        return true;
    }

private:
    MiragePlugin* plugin_;
    std::vector<Button> buttons_;
    int pressed_ = -1, playing_ = -1, sel_ = -1;
    uint8_t shown0_ = 0, shown1_ = 0;
    bool shownMotor_ = false, draggingVol_ = false;
    float volume_ = 0.8f;
    std::string status_, customName_;

    void setVolumeFromY(float y)
    {
        volume_ = std::fmin(1.0f, std::fmax(0.0f, 1.0f - (y - 62 - 7) / 138));
        setParameterValue(kParamVolume, volume_);
        plugin_->engine().volume.store(volume_);
        repaint();
    }

    void doAction(int a)
    {
        const auto& disks = plugin_->disks();
        if (a == ActDiskOpen) {
            FileBrowserOptions opts;
            opts.title = "Insert a Mirage disk image (.img)";
            const std::string dir = plugin_->userDiskDir();
            opts.startDir = dir.c_str();
            openFileBrowser(opts);
            return;
        }
        if (disks.empty()) { status_ = "No bundled disks found"; return; }
        if (a == ActDiskPrev) sel_ = sel_ <= 0 ? int(disks.size()) - 1 : sel_ - 1;
        if (a == ActDiskNext) sel_ = sel_ < 0 || sel_ >= int(disks.size()) - 1 ? 0 : sel_ + 1;
        const auto& d = disks[size_t(sel_)];
        status_ = plugin_->loadDiskFile(d.path) ? "Inserted " + d.name + (d.label.empty() ? "" : " \xE2\x80\x94 " + d.label)
                                                : "Could not read " + d.path;
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

    static bool isBlack(int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

    int noteAt(float x, float y) const
    {
        if (x < kKbX || x > kKbX + kKbW || y < kKbY || y > kKbY + kKbH) return -1;
        const int whites = 36;
        const float ww = kKbW / whites;
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
        const int whites = 36;
        const float ww = kKbW / whites;
        beginPath(); rect(kKbX - 4, kKbY - 6, kKbW + 8, kKbH + 10); fillColor(Color(10, 10, 11)); fill();
        int wi = 0;
        for (int n = kFirstNote; n < kFirstNote + kNumKeys; ++n) {
            if (isBlack(n)) continue;
            beginPath(); rect(kKbX + wi * ww + 0.5f, kKbY, ww - 1.0f, kKbH);
            fillColor(n == playing_ ? Color(200, 210, 230) : Color(238, 236, 228)); fill();
            ++wi;
        }
        for (int n = kFirstNote; n < kFirstNote + kNumKeys; ++n) {
            if (!isBlack(n)) continue;
            beginPath(); rect(blackX(n, ww), kKbY, ww * 0.62f, kKbH * 0.62f);
            fillColor(n == playing_ ? Color(90, 100, 130) : Color(22, 22, 24)); fill();
        }
    }

    void label(float x, float y, const char* t)
    {
        fontSize(10); fillColor(Color(170, 170, 178)); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x, y, t, nullptr);
    }

    void groupFrame(float x, float y, float w, float h, const char* t)
    {
        beginPath(); roundedRect(x, y, w, h, 5);
        strokeColor(Color(80, 80, 88)); strokeWidth(1); stroke();
        if (t && *t) {
            fontSize(9); fillColor(Color(150, 150, 160)); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(x + w / 2, y + 7, t, nullptr);
        }
    }

    void drawButton(const Button& b)
    {
        const bool down = pressed_ >= 0 && &buttons_[size_t(pressed_)] == &b;
        beginPath(); roundedRect(b.x, b.y, b.w, b.h, 4);
        fillPaint(linearGradient(b.x, b.y, b.x, b.y + b.h,
                                 down ? Color(120, 120, 128) : Color(98, 98, 106),
                                 down ? Color(90, 90, 96) : Color(64, 64, 70)));
        fill();
        strokeColor(Color(20, 20, 22)); strokeWidth(1); stroke();
        fillColor(Color(235, 235, 240)); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        const bool small = std::strlen(b.label) > 2;
        fontSize(small ? 10.5f : 17);
        if (b.sub) {
            text(b.x + b.w / 2, b.y + b.h * 0.36f, b.label, nullptr);
            fontSize(10.5f);
            text(b.x + b.w / 2, b.y + b.h * 0.70f, b.sub, nullptr);
        } else {
            text(b.x + b.w / 2, b.y + b.h / 2, b.label, nullptr);
        }
    }

    // Seven-segment digit; bits a..g = 0..6, dp = 7.
    void drawDigit(float x, float y, float w, float h, uint8_t segs)
    {
        const float t = w * 0.16f, sk = w * 0.12f; // thickness, italic skew
        auto seg = [&](int bit, float x0, float y0, float x1, float y1) {
            const bool on = segs & (1 << bit);
            beginPath();
            const float ym = (y0 + y1) / 2;
            auto X = [&](float px, float py) { return px + sk * (1.0f - (py - y) / h); };
            if (y0 == y1) { // horizontal
                moveTo(X(x0 + t * 0.5f, y0), y0);
                lineTo(X(x0 + t, y0 - t / 2), y0 - t / 2);
                lineTo(X(x1 - t, y0 - t / 2), y0 - t / 2);
                lineTo(X(x1 - t * 0.5f, y0), y0);
                lineTo(X(x1 - t, y0 + t / 2), y0 + t / 2);
                lineTo(X(x0 + t, y0 + t / 2), y0 + t / 2);
            } else { // vertical
                moveTo(X(x0, y0 + t * 0.5f), y0 + t * 0.5f);
                lineTo(X(x0 + t / 2, y0 + t), y0 + t);
                lineTo(X(x0 + t / 2, y1 - t), y1 - t);
                lineTo(X(x0, y1 - t * 0.5f), y1 - t * 0.5f);
                lineTo(X(x0 - t / 2, y1 - t), y1 - t);
                lineTo(X(x0 - t / 2, y0 + t), y0 + t);
                (void)ym;
            }
            closePath();
            fillColor(on ? Color(255, 40, 30) : Color(45, 12, 10));
            fill();
        };
        const float x0 = x + t / 2, x1 = x + w - t / 2 - sk, y0 = y + t / 2, ym = y + h / 2, y1 = y + h - t / 2;
        seg(0, x0, y0, x1, y0);   // a
        seg(1, x1, y0, x1, ym);   // b
        seg(2, x1, ym, x1, y1);   // c
        seg(3, x0, y1, x1, y1);   // d
        seg(4, x0, ym, x0, y1);   // e
        seg(5, x0, y0, x0, ym);   // f
        seg(6, x0, ym, x1, ym);   // g
        beginPath(); circle(x + w - t * 0.2f, y1, t * 0.45f);
        fillColor((segs & 0x80) ? Color(255, 40, 30) : Color(45, 12, 10)); fill();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MirageUI)
};

void (*g_mirageStandaloneStart)() = nullptr;

UI* createUI() { return new MirageUI(); }

END_NAMESPACE_DISTRHO
