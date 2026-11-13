// MIT License - see LICENSE
//
// Mochimaru no Daibouken: a small platform game. Run through three stages, stomp the Poyo, Kurimaru and
// Kamekichi, dodge the Fuwari, and meet Fuku-chan at the torii gate.
//
//   mochimaru                          play with the built-in soundtrack
//   mochimaru song.mid [more.mid ...]  play with MIDI files as the soundtrack (a directory works too)
//   mochimaru --level 2                start at stage 2
//   mochimaru --autoplay --shot out.png --frames 600 --level 1     render a frame offscreen, for checking
//
// Gamepad (Nt Gamepad): d-pad or left stick moves, A jumps, X or B runs, Start pauses, Select toggles the music, Y
// plays the next song. Keyboard: arrows or WASD, Space or Z jump, X or Shift run, Esc or Enter pauses and shows the
// keys, M and N for the music, Alt+Enter for fullscreen, Q quits from the pause screen. Sound effects and the
// soundtrack are made by the FM synthesizer of Nt FM.
#include "sprites.h"

#include <QtCore>
#include <QtFM>
#include <QtGamepad>
#include <QtGui>
#include <QtOpenGL>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string_view>

#include <xcb/xcb.h>

namespace {

// Nt's window has no fullscreen call, so ask the window manager the way every X11 client does.
void xcbFullScreen(quintptr window, bool on) {
    int screenNumber = 0;
    xcb_connection_t *c = xcb_connect(nullptr, &screenNumber);
    if (xcb_connection_has_error(c)) {
        xcb_disconnect(c);
        return;
    }
    auto atom = [&](const char *name) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, uint16_t(std::strlen(name)), name), nullptr);
        const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
        std::free(r);
        return a;
    };
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (; screenNumber > 0 && it.rem > 1; --screenNumber)
        xcb_screen_next(&it);
    xcb_client_message_event_t ev;
    std::memset(&ev, 0, sizeof ev);
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = xcb_window_t(window);
    ev.type = atom("_NET_WM_STATE");
    ev.data.data32[0] = on ? 1 : 0;
    ev.data.data32[1] = atom("_NET_WM_STATE_FULLSCREEN");
    ev.data.data32[3] = 1;
    xcb_send_event(c, 0, it.data->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&ev));
    xcb_flush(c);
    std::free(xcb_get_input_focus_reply(c, xcb_get_input_focus(c), nullptr)); // the request has been handled
    xcb_disconnect(c);
}

constexpr int TILE = 16, VW = 320, VH = 192, ROWS = 12;

// ============================================================================================================
// Art
// ============================================================================================================

struct Spr {
    QImage img;
    int pad = 0; // empty rows under the feet
};

Spr makeSpr(QImage img) {
    int last = -1;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)))
                last = y;
    Spr s;
    s.pad = img.height() - 1 - last;
    s.img = std::move(img);
    return s;
}

QImage flipV(const QImage &in) {
    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y)
        for (int x = 0; x < in.width(); ++x)
            out.setPixel(x, in.height() - 1 - y, in.pixel(x, y));
    return out;
}

QImage scale2(const QImage &in) {
    QImage out(in.width() * 2, in.height() * 2, QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x)
            out.setPixel(x, y, in.pixel(x / 2, y / 2));
    return out;
}

// The coin turned away: the same picture, narrower.
QImage squeeze(const QImage &in, int w) {
    QImage out(in.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    const int off = (in.width() - w) / 2;
    for (int y = 0; y < in.height(); ++y)
        for (int x = 0; x < w; ++x)
            out.setPixel(off + x, y, in.pixel(x * in.width() / w, y));
    return out;
}

class Art {
public:
    Art() {
        for (const Sprites::Def &d : Sprites::all()) {
            QString error;
            QImage img = Sprites::render(d, &error);
            if (img.isNull())
                qFatal("%s", qPrintable(error));
            m_s[d.id] = makeSpr(img);
            if (d.id.rfind("poyo", 0) == 0 || d.id.rfind("kuri", 0) == 0 || d.id.rfind("kame", 0) == 0 ||
                d.id.rfind("fuwa", 0) == 0)
                m_s[d.id + "_v"] = makeSpr(flipV(img));
            if (d.id.rfind("hero_", 0) == 0 && d.id != "hero_power")
                m_s[d.id + "_p"] = makeSpr(Sprites::render(Sprites::powered(d, "", "")));
        }
        m_s["torii2"] = makeSpr(scale2(m_s["torii"].img));
        const int widths[4] = {16, 12, 8, 4};
        for (int i = 0; i < 4; ++i)
            m_s["coin" + std::to_string(i)] = makeSpr(squeeze(m_s["koban"].img, widths[i]));
    }
    const Spr &operator[](const std::string &id) const { return m_s.at(id); }

private:
    std::map<std::string, Spr> m_s;
};

// ============================================================================================================
// Levels
// ============================================================================================================

struct Level {
    QString name;
    int w = 0;
    std::vector<std::string> g;
    int goalX = 0;
    QPointF start;
    struct Spawn {
        char type;
        int x, y;
    };
    std::vector<Spawn> spawns;
    QColor skyTop, skyBottom, hillFar, hillNear;
    bool stars = false;

    char at(int x, int y) const {
        if (x < 0 || x >= w)
            return 'S';
        if (y < 0 || y >= ROWS)
            return '.';
        return g[size_t(y)][size_t(x)];
    }
    bool solid(int x, int y) const { return std::string_view("#=B?O*xSTtUu").find(at(x, y)) != std::string_view::npos; }
};

struct Builder {
    int w;
    std::vector<std::string> g;
    explicit Builder(int width) : w(width), g(ROWS, std::string(size_t(width), '.')) {}
    void put(int x, int y, char c) {
        if (x >= 0 && x < w && y >= 0 && y < ROWS)
            g[size_t(y)][size_t(x)] = c;
    }
    void ground(int x0, int x1) {
        for (int x = x0; x < x1; ++x) {
            put(x, 10, '#');
            put(x, 11, '=');
        }
    }
    void row(int x, int y, int n, char c) {
        for (int i = 0; i < n; ++i)
            put(x + i, y, c);
    }
    void pipe(int x, int h) {
        put(x, 10 - h, 'T');
        put(x + 1, 10 - h, 't');
        for (int y = 11 - h; y <= 9; ++y) {
            put(x, y, 'U');
            put(x + 1, y, 'u');
        }
    }
    void stairs(int x, int n) {
        for (int i = 0; i < n; ++i)
            for (int y = 9 - i; y <= 9; ++y)
                put(x + i, y, 'S');
    }
    void arc(int x, int y, int n) {
        for (int i = 0; i < n; ++i)
            put(x + i, (i > 0 && i < n - 1) ? y - 1 : y, 'c');
    }
};

Level finish(Builder &b, const char *name, const char *top, const char *bottom, const char *far, const char *near,
             bool stars) {
    Level lv;
    lv.name = QString(name);
    lv.w = b.w;
    lv.skyTop = QColor(top);
    lv.skyBottom = QColor(bottom);
    lv.hillFar = QColor(far);
    lv.hillNear = QColor(near);
    lv.stars = stars;
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < b.w; ++x) {
            char &c = b.g[size_t(y)][size_t(x)];
            if (c == '@') {
                lv.start = QPointF(x * TILE + 3, y * TILE + 2);
                c = '.';
            } else if (c == 'G') {
                lv.goalX = x;
                c = '.';
            } else if (c >= '1' && c <= '4') {
                lv.spawns.push_back({c, x, y});
                c = '.';
            }
        }
    lv.g = std::move(b.g);
    return lv;
}

constexpr int kLevels = 3;

Level buildLevel(int n) {
    if (n == 0) {
        Builder b(150);
        b.ground(0, 40);
        b.ground(43, 60);
        b.ground(63, 98);
        b.ground(101, 150);
        b.put(3, 9, '@');
        b.put(14, 6, 'B');
        b.put(15, 6, '?');
        b.put(16, 6, 'B');
        b.put(17, 6, 'O');
        b.put(18, 6, 'B');
        b.put(22, 9, '1');
        b.arc(27, 7, 5);
        b.pipe(32, 2);
        b.put(36, 9, '2');
        b.arc(39, 6, 5);
        b.pipe(46, 3);
        b.put(52, 9, '1');
        b.put(55, 9, '2');
        b.arc(59, 7, 5);
        b.row(59, 6, 4, 'B');
        b.put(66, 9, '3');
        b.put(70, 6, '*');
        b.put(71, 6, '?');
        b.put(72, 6, '?');
        b.put(76, 9, '1');
        b.put(78, 9, '1');
        b.row(80, 6, 3, 'B');
        b.put(84, 9, '2');
        b.stairs(88, 4);
        b.arc(96, 6, 6);
        b.put(105, 9, '3');
        b.put(110, 6, '4');
        b.row(112, 7, 3, 'c');
        b.put(116, 9, '2');
        b.stairs(120, 5);
        b.put(138, 9, 'G');
        return finish(b, "Sakura Meadow", "#9fd8ff", "#ffe3ef", "#cdeed2", "#a5e0b0", false);
    }
    if (n == 1) {
        Builder b(170);
        b.ground(0, 30);
        b.ground(33, 55);
        b.ground(58, 90);
        b.ground(94, 125);
        b.ground(128, 170);
        b.put(3, 9, '@');
        b.put(10, 6, '?');
        b.put(11, 6, 'B');
        b.put(12, 6, 'O');
        b.put(14, 9, '1');
        b.pipe(18, 2);
        b.put(21, 9, '2');
        b.pipe(24, 3);
        b.put(28, 9, '3');
        b.arc(30, 7, 3);
        b.row(34, 6, 2, 'B');
        b.put(36, 9, '1');
        b.pipe(40, 2);
        b.pipe(46, 4);
        b.put(44, 9, '2');
        b.put(50, 9, '3');
        b.row(48, 5, 4, 'c');
        b.arc(56, 7, 2);
        b.put(62, 9, '2');
        b.put(66, 9, '1');
        b.row(64, 6, 5, 'B');
        b.put(66, 6, '*');
        b.pipe(70, 3);
        b.put(74, 9, '3');
        b.put(76, 9, '2');
        b.pipe(80, 2);
        b.put(84, 9, '1');
        b.arc(91, 7, 3);
        b.put(100, 9, '3');
        b.put(104, 9, '2');
        b.put(108, 6, '4');
        b.put(112, 9, '1');
        b.row(110, 6, 3, 'B');
        b.put(111, 6, '?');
        b.put(118, 9, '3');
        b.arc(126, 7, 2);
        b.put(132, 9, '2');
        b.put(135, 9, '2');
        b.put(142, 6, '4');
        b.row(138, 6, 5, 'c');
        b.stairs(150, 5);
        b.put(162, 9, 'G');
        return finish(b, "Bamboo Grove", "#b7efd8", "#fff6cf", "#bfe8c8", "#8fd6a0", false);
    }
    Builder b(150);
    b.ground(0, 12);
    b.ground(128, 150);
    b.put(3, 9, '@');
    b.put(8, 6, '*');
    const int plat[][3] = {{14, 8, 4}, {22, 7, 3}, {29, 8, 3}, {36, 6, 4}, {44, 7, 3},  {51, 8, 5},  {60, 6, 3},
                           {67, 8, 3}, {74, 7, 6}, {84, 8, 3}, {91, 6, 3}, {98, 7, 3},  {105, 8, 6}, {115, 7, 3},
                           {121, 8, 3}};
    for (const auto &p : plat)
        b.row(p[0], p[1], p[2], 'S');
    b.arc(14, 7, 4);
    b.arc(22, 6, 3);
    b.arc(36, 5, 4);
    b.put(53, 5, 'O');
    b.put(76, 6, '1');
    b.put(78, 6, '2');
    b.put(108, 7, '3');
    b.put(54, 7, '1');
    b.put(38, 5, '2');
    b.arc(84, 7, 3);
    b.arc(105, 7, 4);
    b.put(26, 5, '4');
    b.put(58, 4, '4');
    b.put(95, 3, '4');
    b.put(113, 4, '4');
    b.put(110, 4, '*');
    b.put(134, 9, '1');
    b.put(137, 9, '2');
    b.put(144, 9, 'G');
    return finish(b, "Torii Sky", "#6b5bb5", "#ffb3c8", "#8a78c8", "#b48fd0", true);
}

// ============================================================================================================
// Physics
// ============================================================================================================

struct Body {
    float x = 0, y = 0, w = 12, h = 12, vx = 0, vy = 0;
    bool ground = false, wall = false;
    float cx() const { return x + w / 2; }
    float bottom() const { return y + h; }
};

bool overlap(const Body &a, const Body &b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void moveBody(const Level &lv, Body &b, const std::function<void(int, int)> &headHit = {}) {
    b.wall = false;
    b.x += b.vx;
    int top = int(std::floor(b.y / TILE)), bot = int(std::floor((b.y + b.h - 0.01f) / TILE));
    if (b.vx > 0) {
        const int c = int(std::floor((b.x + b.w - 0.01f) / TILE));
        for (int r = top; r <= bot; ++r)
            if (lv.solid(c, r)) {
                b.x = float(c * TILE) - b.w;
                b.vx = 0;
                b.wall = true;
                break;
            }
    } else if (b.vx < 0) {
        const int c = int(std::floor(b.x / TILE));
        for (int r = top; r <= bot; ++r)
            if (lv.solid(c, r)) {
                b.x = float((c + 1) * TILE);
                b.vx = 0;
                b.wall = true;
                break;
            }
    }
    b.y += b.vy;
    b.ground = false;
    const int l = int(std::floor(b.x / TILE)), r = int(std::floor((b.x + b.w - 0.01f) / TILE));
    if (b.vy > 0) {
        const int row = int(std::floor((b.y + b.h - 0.01f) / TILE));
        for (int c = l; c <= r; ++c)
            if (lv.solid(c, row)) {
                b.y = float(row * TILE) - b.h;
                b.vy = 0;
                b.ground = true;
                break;
            }
    } else if (b.vy < 0) {
        const int row = int(std::floor(b.y / TILE));
        for (int c = l; c <= r; ++c)
            if (lv.solid(c, row)) {
                b.y = float((row + 1) * TILE);
                b.vy = 0;
                if (headHit) {
                    int cc = int(std::floor(b.cx() / TILE));
                    if (!lv.solid(cc, row))
                        cc = c;
                    headHit(cc, row);
                }
                break;
            }
    }
}

// ============================================================================================================
// Sound: Nt FM
// ============================================================================================================

enum class Sfx { Jump, Coin, Stomp, Bump, PowerUp, Hurt, Die, Kick, Break, Star };

// A cheerful two-section tune, written as a standard MIDI file so that it plays the way a file you load does.
QByteArray builtinSong() {
    struct Ev {
        int tick, order;
        quint8 status, d1, d2;
    };
    constexpr int eighth = 240;
    std::vector<Ev> ev[4];
    auto note = [&](int track, int ch, int key, int start, int len, int vel) {
        ev[track].push_back({start * eighth, 1, quint8(0x90 | ch), quint8(key), quint8(vel)});
        ev[track].push_back({start * eighth + len * eighth - 20, 0, quint8(0x80 | ch), quint8(key), 0});
    };
    ev[0].push_back({0, -1, 0xC0, 80, 0});
    ev[1].push_back({0, -1, 0xC1, 10, 0});
    ev[2].push_back({0, -1, 0xC2, 33, 0});

    const char *melody[16] = {"e-g-C-g-", "a-g-e-d-", "c-e-g-e-", "d-e-d---", "e-g-C-D-", "C-a-g-e-",
                              "g-e-d-c-", "c-------", "a-a-C-a-", "g-g-D-g-", "e-g-C-g-", "a-g-e---",
                              "a-a-C-D-", "C-a-g-e-", "d-e-g-a-", "C-------"};
    const int chords[16] = {0, 1, 2, 3, 0, 1, 2, 3, 2, 3, 0, 1, 2, 3, 0, 0}; // C Am F G
    const int base[4] = {60, 57, 53, 55};
    const int third[4] = {4, 3, 4, 4};
    auto pitchOf = [](char c) {
        switch (c) {
        case 'c': return 72;
        case 'd': return 74;
        case 'e': return 76;
        case 'g': return 79;
        case 'a': return 81;
        case 'C': return 84;
        case 'D': return 86;
        }
        return 0;
    };
    for (int bar = 0; bar < 16; ++bar) {
        const int t0 = bar * 8;
        const char *m = melody[bar];
        for (int i = 0; i < 8; ++i) {
            if (m[i] == '-')
                continue;
            int len = 1;
            while (i + len < 8 && m[i + len] == '-')
                ++len;
            note(0, 0, pitchOf(m[i]), t0 + i, len, 100);
        }
        const int ch = chords[bar];
        const int tones[4] = {base[ch], base[ch] + third[ch], base[ch] + 7, base[ch] + 12};
        const int arp[8] = {0, 1, 2, 3, 2, 1, 2, 1};
        for (int i = 0; i < 8; ++i)
            note(1, 1, tones[arp[i]], t0 + i, 1, 55);
        note(2, 2, base[ch] - 12, t0, 3, 95);
        note(2, 2, base[ch] - 5, t0 + 4, 2, 85);
        note(2, 2, base[ch] - 12, t0 + 6, 2, 85);
        note(3, 9, 36, t0, 1, 100);
        note(3, 9, 36, t0 + 4, 1, 100);
        note(3, 9, 38, t0 + 2, 1, 70);
        note(3, 9, 38, t0 + 6, 1, 70);
        for (int i = 0; i < 8; ++i)
            note(3, 9, 42, t0 + i, 1, i % 2 ? 40 : 55);
    }

    QByteArray out;
    auto be16 = [&](QByteArray &a, int v) {
        a.append(char(v >> 8));
        a.append(char(v & 0xff));
    };
    auto vlq = [](QByteArray &a, quint32 v) {
        char buf[5];
        int n = 0;
        buf[n++] = char(v & 0x7f);
        while ((v >>= 7) != 0)
            buf[n++] = char((v & 0x7f) | 0x80);
        while (n--)
            a.append(buf[n]);
    };
    out.append("MThd", 4);
    out.append(char(0)).append(char(0)).append(char(0)).append(char(6));
    be16(out, 1);
    be16(out, 5);
    be16(out, 480);
    auto chunk = [&](const QByteArray &data) {
        out.append("MTrk", 4);
        out.append(char(data.size() >> 24)).append(char(data.size() >> 16));
        out.append(char(data.size() >> 8)).append(char(data.size()));
        out.append(data);
    };
    QByteArray tempo;
    vlq(tempo, 0);
    tempo.append(char(0xFF)).append(char(0x51)).append(char(3));
    const int us = 60000000 / 132;
    tempo.append(char(us >> 16)).append(char(us >> 8)).append(char(us));
    vlq(tempo, 0);
    tempo.append(char(0xFF)).append(char(0x2F)).append(char(0));
    chunk(tempo);
    for (int t = 0; t < 4; ++t) {
        auto &events = ev[t];
        std::stable_sort(events.begin(), events.end(), [](const Ev &a, const Ev &b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        QByteArray data;
        int last = 0;
        for (const Ev &e : events) {
            vlq(data, quint32(e.tick - last));
            last = e.tick;
            data.append(char(e.status)).append(char(e.d1));
            if ((e.status & 0xF0) != 0xC0)
                data.append(char(e.d2));
        }
        vlq(data, 0);
        data.append(char(0xFF)).append(char(0x2F)).append(char(0));
        chunk(data);
    }
    return out;
}

class Audio {
public:
    // With `offline` there is no sound device: render() pulls the engine, one video frame at a time.
    explicit Audio(bool enabled, bool offline = false) {
        if (!enabled)
            return;
        QtFM::EngineConfig config;
        config.maxVoices = 64;
        m_engine = std::make_unique<QtFM::Engine>(config);
        m_engine->setMasterGain(-4);
        if (!offline && !m_engine->start()) {
            std::fprintf(stderr, "mochimaru: no sound output, playing silently\n");
            m_engine.reset();
            return;
        }
        m_player = std::make_unique<QtFM::MidiPlayer>(m_engine.get());
        m_player->setInstruments(QtFM::PatchBank::generalMidi());
        m_player->setLoops(true);
        m_player->setGain(-8);
        using QtFM::Category;
        m_sfx[int(Sfx::Jump)] = QtFM::Patch::generate(Category::Jump, 2);
        m_sfx[int(Sfx::Coin)] = QtFM::Patch::generate(Category::Pickup, 7);
        m_sfx[int(Sfx::Stomp)] = QtFM::Patch::generate(Category::Hit, 4);
        m_sfx[int(Sfx::Bump)] = QtFM::Patch::generate(Category::Blip, 9);
        m_sfx[int(Sfx::PowerUp)] = QtFM::Patch::generate(Category::PowerUp, 5);
        m_sfx[int(Sfx::Hurt)] = QtFM::Patch::generate(Category::Hit, 11);
        m_sfx[int(Sfx::Die)] = QtFM::Patch::generate(Category::Explosion, 6);
        m_sfx[int(Sfx::Kick)] = QtFM::Patch::generate(Category::Blip, 3);
        m_sfx[int(Sfx::Break)] = QtFM::Patch::generate(Category::Explosion, 12);
        m_sfx[int(Sfx::Star)] = QtFM::Patch::generate(Category::PowerUp, 9);
    }
    ~Audio() {
        m_player.reset();
        if (m_engine)
            m_engine->stop();
    }

    bool available() const { return bool(m_engine); }

    void render(qint16 *stereo, int frames) {
        if (!m_engine) {
            std::fill(stereo, stereo + 2 * frames, qint16(0));
            return;
        }
        m_player->schedule(m_engine->frame() + quint64(frames));
        m_engine->render(stereo, frames);
    }

    void play(Sfx s, float pitch = 0, float gain = -2) {
        if (!m_engine)
            return;
        QtFM::PlayOptions o;
        o.pitch = pitch;
        o.gain = gain;
        m_engine->play(m_sfx[int(s)], o);
    }

    // A short rising run of notes, played on the pickup patch.
    void jingle(bool happy) {
        if (!m_engine)
            return;
        QtFM::Sequence seq;
        seq.setTempo(happy ? 170.0f : 110.0f);
        const QtFM::Patch p = QtFM::Patch::generate(QtFM::Category::Pickup, happy ? 11 : 21);
        const int up[] = {0, 4, 7, 12, 7, 12, 16, 19, 24};
        const int down[] = {7, 5, 3, 0, -5};
        if (happy)
            for (int i = 0; i < 9; ++i)
                seq.addNote(0.5f * float(i), p, float(up[i]));
        else
            for (int i = 0; i < 5; ++i)
                seq.addNote(float(i), p, float(down[i]));
        m_engine->play(seq);
    }

    bool setSong(const QByteArray &data, QString *error = nullptr) {
        if (!m_player)
            return false;
        const QtFM::MidiFile file = QtFM::MidiFile::fromData(data, error);
        if (!file.isValid())
            return false;
        m_player->stop();
        m_player->setFile(file);
        if (m_music)
            m_player->play();
        return true;
    }
    void setMusic(bool on) {
        if (!m_player)
            return;
        m_music = on;
        if (on)
            m_player->play();
        else
            m_player->stop();
    }
    bool music() const { return m_music; }

private:
    std::unique_ptr<QtFM::Engine> m_engine;
    std::unique_ptr<QtFM::MidiPlayer> m_player;
    QtFM::Patch m_sfx[10];
    bool m_music = false;
};

// ============================================================================================================
// Pixel font: 5x7 letters with a dark outline, a shine on top and, for titles, a wave and pastel colors.
// ============================================================================================================

struct Glyph {
    char c;
    const char *row[7];
};

const Glyph kGlyphs[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".....", "..#.."}},
    {',', {".....", ".....", ".....", ".....", ".....", "..#..", ".#..."}},
    {':', {".....", "..#..", ".....", ".....", ".....", "..#..", "....."}},
    {'-', {".....", ".....", ".....", ".###.", ".....", ".....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'/', {"....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."}},
    {'#', {".#.#.", "#####", "#####", "#####", ".###.", "..#..", "....."}}, // a heart
    {'*', {"..#..", "..#..", "#####", ".###.", ".###.", "#...#", "....."}}, // a star
};

const Glyph *glyphOf(char c) {
    for (const Glyph &g : kGlyphs)
        if (g.c == c)
            return &g;
    return nullptr;
}

enum class Align { Left, Center, Right };

int textWidth(const QString &s, int scale) { return s.isEmpty() ? 0 : int(s.size()) * 6 * scale - scale; }

QColor brighter(const QColor &c, int by) {
    return QColor(std::min(255, c.red() + by), std::min(255, c.green() + by), std::min(255, c.blue() + by));
}

// `x` is the left edge, the center or the right edge of the text, by `align`. With tick >= 0 the letters bob;
// with `rainbow` they take turns in pastel colors.
void pixelText(QPainter &p, const QString &str, int x, int y, int scale, const QColor &fill, Align align = Align::Left,
               int tick = -1, bool rainbow = false) {
    static const char *pastel[] = {"#ff9fc2", "#ffb38a", "#ffd84a", "#9be8c0", "#8ad7f5", "#c9b3ff"};
    static const QColor outline(74, 48, 64);
    const QString s = str.toUpper();
    const int t = scale >= 4 ? 2 : 1;
    const int w = textWidth(s, scale);
    const int left = align == Align::Center ? x - w / 2 : align == Align::Right ? x - w : x;
    if (tick == -1) {
        // Text that keeps still is drawn once into an image, which is far cheaper than a rectangle per pixel.
        static QHash<QString, QImage> cache;
        const QString key = QString("%1|%2|%3|%4").arg(s).arg(scale).arg(fill.rgba()).arg(rainbow);
        auto it = cache.constFind(key);
        if (it == cache.constEnd()) {
            if (cache.size() > 200)
                cache.clear();
            QImage img(std::max(1, w + 2 * t), 7 * scale + 3 * t, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter ip(&img);
            pixelText(ip, s, t, t, scale, fill, Align::Left, -2, rainbow); // -2: draw it, still
            ip.end();
            it = cache.insert(key, img);
        }
        p.drawImage(left - t, y - t, *it);
        return;
    }
    for (int i = 0; i < int(s.size()); ++i) {
        const ushort u = s[i].unicode();
        const Glyph *g = glyphOf(u < 128 ? char(u) : '?');
        if (!g)
            continue;
        const int gx = left + i * 6 * scale;
        const int gy = y + (tick >= 0 ? int(std::lround(std::sin(tick * 0.1 + i * 0.7) * scale * 0.9)) : 0);
        QColor c = fill;
        if (g->c == '#')
            c = QColor("#ff6f9a");
        else if (g->c == '*')
            c = QColor("#ffd84a");
        else if (rainbow)
            c = QColor(pastel[i % 6]);
        const QColor shine = brighter(c, 45);
        auto on = [&](int r, int col) { return r >= 0 && r < 7 && col >= 0 && col < 5 && g->row[r][col] == '#'; };
        for (int dy = -t; dy <= t + t; dy += t)
            for (int dx = -t; dx <= t; dx += t)
                if (dx || dy)
                    for (int r = 0; r < 7; ++r)
                        for (int col = 0; col < 5; ++col)
                            if (on(r, col))
                                p.fillRect(gx + dx + col * scale, gy + dy + r * scale, scale, scale, outline);
        for (int r = 0; r < 7; ++r)
            for (int col = 0; col < 5; ++col) {
                if (!on(r, col))
                    continue;
                p.fillRect(gx + col * scale, gy + r * scale, scale, scale, c);
                if (scale >= 3 && !on(r - 1, col))
                    p.fillRect(gx + col * scale, gy + r * scale, scale, std::max(1, scale / 3), shine);
            }
    }
}

// Bubble letters for the title. Each 5x7 glyph becomes a skeleton, lit cells joined to their neighbors, that is
// drawn several times, fatter or thinner: a dark outline, a shaded body, the body itself and a highlight, so the
// letters come out puffy and round. Drawn on the window, not the canvas, so the curves are smooth. The fat strokes
// are filled shapes (a circle on every cell, a bar along every join) rather than wide pens, because Nt6 draws wide
// pens too wide on a HiDPI screen.
struct Bubble {
    QList<QLineF> lines;  // joins between cells, in cells
    QList<QPointF> cells; // the centers of all lit cells
};

Bubble bubbleOf(const Glyph &g) {
    auto on = [&](int r, int c) { return r >= 0 && r < 7 && c >= 0 && c < 5 && g.row[r][c] == '#'; };
    Bubble b;
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 5; ++c) {
            if (!on(r, c))
                continue;
            const QPointF a(c + 0.5, r + 0.5);
            b.cells.append(a);
            auto join = [&](int r2, int c2) { b.lines.append(QLineF(a, QPointF(c2 + 0.5, r2 + 0.5))); };
            if (on(r, c + 1))
                join(r, c + 1);
            if (on(r + 1, c))
                join(r + 1, c);
            // Diagonals only where there is no square way round, which rounds the corners off.
            if (on(r + 1, c + 1) && !on(r, c + 1) && !on(r + 1, c))
                join(r + 1, c + 1);
            if (on(r + 1, c - 1) && !on(r, c - 1) && !on(r + 1, c))
                join(r + 1, c - 1);
        }
    return b;
}

// A circle as a polygon, with enough corners to look round at its size.
void drawDisc(QPainter &p, const QPointF &c, double r) {
    const int n = std::clamp(int(r * 0.9), 12, 48);
    QPointF pts[48];
    for (int i = 0; i < n; ++i) {
        const double a = i * 6.283185307179586 / n;
        pts[i] = c + QPointF(std::cos(a) * r, std::sin(a) * r);
    }
    p.drawPolygon(pts, n);
}

// `cx` is the center of the text, `top` its top edge and `u` the size of one cell, all in window pixels. Every
// letter's outline is drawn before any body, so the outlines merge into one sticker behind the word. With `aa`
// the painter smooths the edges; an OpenGL window leaves that to multisampling, which is far cheaper.
void bubbleText(QPainter &p, const QString &str, double cx, double top, double u, int tick, int seed,
                const QColor *colors, int ncolors, bool aa) {
    static const QColor ink(74, 48, 64);
    const QString s = str.toUpper();
    const double adv = u * 5.5;
    const double width = s.size() * adv - u * 0.5;
    const double fat = u * 2.0; // how wide the skeleton is made
    const double o = u * 0.6;   // outline thickness
    struct Letter {
        const Glyph *g;
        Bubble b;
        QTransform tr;
        QColor base;
    };
    std::vector<Letter> letters;
    for (int i = 0; i < int(s.size()); ++i) {
        const ushort uc = s[i].unicode();
        const Glyph *g = glyphOf(uc < 128 ? char(uc) : '?');
        if (!g)
            continue;
        const double ph = tick * 0.08 + (i + seed) * 0.8;
        QTransform tr;
        tr.translate(cx - width / 2 + i * adv + u * 2.5, top + u * 3.5 + std::sin(ph) * u * 0.6);
        tr.rotate(std::sin(ph * 0.7 + 1.3) * 3.0 + ((i + seed) % 2 ? 4 : -4));
        tr.scale(u, u);
        tr.translate(-2.5, -3.5);
        letters.push_back({g, bubbleOf(*g), tr, colors[(i + seed) % ncolors]});
    }

    // One layer of one letter: its skeleton made `w` wide, moved by (dx, dy) window pixels.
    auto layer = [&](const Letter &l, const QColor &c, double w, double dx, double dy) {
        const QPointF d(dx, dy);
        p.setBrush(c);
        for (const QLineF &line : l.b.lines) {
            const QLineF m = l.tr.map(line);
            const QPointF a = m.p1() + d, b = m.p2() + d;
            const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
            const QPointF n(-(b.y() - a.y()) / len * w / 2, (b.x() - a.x()) / len * w / 2);
            const QPointF bar[] = {a + n, b + n, b - n, a - n};
            p.drawPolygon(bar, 4);
        }
        for (const QPointF &cell : l.b.cells)
            drawDisc(p, l.tr.map(cell) + d, w / 2);
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing, aa);
    p.setPen(Qt::NoPen);
    for (const Letter &l : letters)
        layer(l, ink, fat + 2 * o, u * 0.35, u * 0.85); // a drop of shadow
    for (const Letter &l : letters)
        layer(l, ink, fat + 2 * o, 0, 0);
    for (const Letter &l : letters) {
        layer(l, l.base.darker(140), fat, 0, 0);
        layer(l, l.base, fat * 0.8, -u * 0.05, -u * 0.2);
        layer(l, brighter(l.base, 70), fat * 0.36, -u * 0.2, -u * 0.42);
        // A glint on the first cell of the letter.
        p.setBrush(QColor(255, 255, 255, 240));
        for (int k = 0; k < 35; ++k)
            if (l.g->row[k / 5][k % 5] == '#') {
                p.drawEllipse(l.tr.map(QPointF(k % 5 + 0.25, k / 5 + 0.2)), u * 0.32, u * 0.24);
                break;
            }
    }
    p.restore();
}

// ============================================================================================================
// The game
// ============================================================================================================

enum class State { Title, Ready, Play, Dying, Clear, GameOver, Win, Paused };
enum EType { Poyo, Kuri, Kame, Fuwa };
enum EState { Walk, Squished, ShellIdle, ShellMoving, Knocked };

struct Enemy {
    EType t = Poyo;
    Body b;
    int state = Walk, timer = 0, dir = -1;
    bool alive = true, awake = false;
    float baseY = 0, startX = 0, phase = 0;
};

struct Item {
    int type = 0; // 0 onigiri, 1 kirari
    Body b;
    int emerge = 14, dir = 1;
    bool alive = true;
};

struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    int life = 0, kind = 0; // 0 spark, 1 popping coin, 2 brick shard
    QColor col;
    int size = 2;
};

struct Bump {
    int tx, ty, t;
};

struct Input {
    bool left = false, right = false, jump = false, run = false, start = false;
};

struct Options {
    int level = -1;
    bool sound = true, autoplay = false, paused = false, offline = false, invincible = false;
    QStringList songs;
};

// The window is an OpenGL window: the canvas is scaled up to the window by the GPU, which keeps fullscreen fast.
class Game : public QOpenGLWindow {
public:
    explicit Game(const Options &opt) : m_opt(opt), m_audio(opt.sound, opt.offline) {
        setTitle("Mochimaru no Daibouken");
        QSurfaceFormat format;
        format.setSamples(4); // smooths the title's curves on the GPU
        setFormat(format);
        resize(VW * 3, VH * 3);
        m_canvas = QImage(VW, VH, QImage::Format_RGB32);

        QGamepadManager *mgr = QGamepadManager::instance();
        mgr->setDeadZone(0.2);
        auto pick = [this, mgr] {
            const QList<int> ids = mgr->connectedGamepads();
            if (!ids.isEmpty() && !m_pad.isConnected())
                m_pad.setDeviceId(ids.first());
        };
        QObject::connect(mgr, &QGamepadManager::connectedGamepadsChanged, this, pick);
        pick();

        for (int i = 0; i < 40; ++i)
            m_petals.push_back({float(QRandomGenerator::global()->bounded(VW)),
                                float(QRandomGenerator::global()->bounded(VH)),
                                0.2f + float(QRandomGenerator::global()->bounded(60)) / 100.0f,
                                0.25f + float(QRandomGenerator::global()->bounded(50)) / 100.0f,
                                QRandomGenerator::global()->bounded(628), 0, QColor(), 2});

        m_songs.push_back({QString("built-in tune"), {}, true});
        for (const QString &path : opt.songs)
            addSongPath(path);
        if (m_audio.available()) {
            loadSong(m_songs.size() > 1 ? 1 : 0);
            m_audio.setMusic(true);
        }

        loadLevel(0);
        if (opt.level >= 0)
            newGame(opt.level);

        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { onTimer(); });
        m_timer.setInterval(8);
        m_clock.start();
        m_last = m_clock.nsecsElapsed();
    }

    void start() { m_timer.start(); }

    // Advances the game without a window, then draws it.
    QImage shot(int frames, const QSize &size) {
        for (int i = 0; i < frames; ++i)
            step();
        if (m_opt.paused && m_state == State::Play)
            m_state = State::Paused;
        QImage img(size, QImage::Format_RGB32);
        QPainter p(&img);
        render(p, size);
        p.end();
        return img;
    }

    // Plays `frames` frames with the bot, offline and at exactly 60 frames a second, and writes an MP4 with the
    // video and the sound. It needs ffmpeg.
    bool record(const QString &file, int frames, const QSize &size) {
        const QString tmpVideo = file + ".video.mp4", tmpAudio = file + ".audio.wav";
        QProcess enc;
        enc.start("ffmpeg", {"-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgr0", "-s",
                             QString("%1x%2").arg(size.width()).arg(size.height()), "-r", "60", "-i", "-", "-c:v",
                             "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p", tmpVideo});
        if (!enc.waitForStarted()) {
            std::fprintf(stderr, "mochimaru: cannot run ffmpeg\n");
            return false;
        }
        constexpr int perFrame = 48000 / 60;
        std::vector<qint16> pcm;
        pcm.reserve(size_t(frames) * perFrame * 2);
        std::vector<qint16> block(size_t(perFrame) * 2);
        QImage img(size, QImage::Format_RGB32);
        for (int i = 0; i < frames; ++i) {
            step();
            QPainter p(&img);
            render(p, size);
            p.end();
            enc.write(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes());
            enc.waitForBytesWritten(-1);
            m_audio.render(block.data(), perFrame);
            pcm.insert(pcm.end(), block.begin(), block.end());
        }
        enc.closeWriteChannel();
        enc.waitForFinished(-1);

        QByteArray wav;
        auto le32 = [&](quint32 v) { for (int b = 0; b < 4; ++b) wav.append(char((v >> (8 * b)) & 0xff)); };
        auto le16 = [&](quint16 v) { wav.append(char(v & 0xff)).append(char(v >> 8)); };
        const quint32 bytes = quint32(pcm.size() * 2);
        wav.append("RIFF", 4);
        le32(36 + bytes);
        wav.append("WAVEfmt ", 8);
        le32(16);
        le16(1);
        le16(2);
        le32(48000);
        le32(48000 * 4);
        le16(4);
        le16(16);
        wav.append("data", 4);
        le32(bytes);
        wav.append(reinterpret_cast<const char *>(pcm.data()), qsizetype(bytes));
        QFile out(tmpAudio);
        if (!out.open(QIODevice::WriteOnly) || out.write(wav) != wav.size()) {
            std::fprintf(stderr, "mochimaru: cannot write %s\n", qPrintable(tmpAudio));
            return false;
        }
        out.close();

        QProcess mux;
        mux.start("ffmpeg", {"-y", "-loglevel", "error", "-i", tmpVideo, "-i", tmpAudio, "-c:v", "copy", "-c:a", "aac",
                             "-b:a", "192k", "-movflags", "+faststart", file});
        mux.waitForFinished(-1);
        QFile::remove(tmpVideo);
        QFile::remove(tmpAudio);
        std::fprintf(stderr, "recorded %d frames (%.1f s): stage %d, %d lives, score %d\n", frames, frames / 60.0,
                     m_levelIdx + 1, m_lives, m_score);
        return mux.exitStatus() == QProcess::NormalExit && mux.exitCode() == 0;
    }

protected:
    void paintGL() override {
        QPainter p(this);
        m_gl = true;
        render(p, size());
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->isAutoRepeat())
            return;
        const int key = e->key();
        if (key == Qt::Key_F11 || ((key == Qt::Key_Return || key == Qt::Key_Enter) && (e->modifiers() & Qt::AltModifier))) {
            toggleFullScreen();
            return;
        }
        m_keys.insert(key);
        if (key == Qt::Key_Escape)
            escape();
        else if (key == Qt::Key_Q && (m_state == State::Paused || m_state == State::Title))
            close();
        else if (key == Qt::Key_M)
            toggleMusic();
        else if (key == Qt::Key_N)
            nextSong();
    }
    void keyReleaseEvent(QKeyEvent *e) override {
        if (!e->isAutoRepeat())
            m_keys.erase(e->key());
    }
    void focusOutEvent(QFocusEvent *) override { m_keys.clear(); }

private:
    struct Song {
        QString path;
        QByteArray data;
        bool builtin = false;
    };

    void escape() {
        switch (m_state) {
        case State::Play: m_state = State::Paused; break;
        case State::Paused: m_state = State::Play; break;
        case State::Title: close(); break;
        case State::GameOver:
        case State::Win:
            if (m_stateT > 30)
                loadLevel(0);
            break;
        default: break;
        }
    }

    void toggleFullScreen() {
        if (!winId())
            return;
        m_full = !m_full;
        xcbFullScreen(winId(), m_full);
        setCursor(QCursor(m_full ? Qt::BlankCursor : Qt::ArrowCursor));
    }

    // ---- soundtrack ----------------------------------------------------------------------------------------
    void addSongPath(const QString &path) {
        const QFileInfo info(path);
        if (info.isDir()) {
            const QStringList files = QDir(path).entryList({"*.mid", "*.midi", "*.kar", "*.rmi"}, QDir::Files, QDir::Name);
            for (const QString &f : files)
                m_songs.push_back({info.absoluteFilePath() + "/" + f, {}, false});
        } else {
            m_songs.push_back({info.absoluteFilePath(), {}, false});
        }
    }

    void loadSong(int index) {
        while (index >= 0 && index < m_songs.size()) {
            Song &s = m_songs[size_t(index)];
            QByteArray data = s.builtin ? builtinSong() : s.data;
            if (!s.builtin && data.isEmpty()) {
                QFile f(s.path);
                if (f.open(QIODevice::ReadOnly))
                    data = f.readAll();
            }
            QString error;
            if (m_audio.setSong(data, &error)) {
                m_song = index;
                toast(s.builtin ? QString("Music: built-in tune") : QString("Music: ") + QFileInfo(s.path).fileName());
                return;
            }
            std::fprintf(stderr, "mochimaru: %s: %s\n", s.builtin ? "built-in tune" : qPrintable(s.path),
                         qPrintable(error));
            if (s.builtin)
                return;
            m_songs.erase(m_songs.begin() + index);
            if (index >= m_songs.size())
                index = 0;
        }
    }
    void nextSong() { loadSong((m_song + 1) % int(m_songs.size())); }
    void toggleMusic() {
        m_audio.setMusic(!m_audio.music());
        toast(QString(m_audio.music() ? "Music on" : "Music off"));
    }
    void toast(const QString &s) {
        m_toast = s;
        m_toastT = 180;
    }

    // ---- input ---------------------------------------------------------------------------------------------
    Input readInput() {
        Input in;
        auto held = [&](std::initializer_list<int> keys) {
            for (int k : keys)
                if (m_keys.count(k))
                    return true;
            return false;
        };
        in.left = held({Qt::Key_Left, Qt::Key_A});
        in.right = held({Qt::Key_Right, Qt::Key_D});
        in.jump = held({Qt::Key_Space, Qt::Key_Z, Qt::Key_Up, Qt::Key_W});
        in.run = held({Qt::Key_X, Qt::Key_Shift, Qt::Key_Control});
        in.start = held({Qt::Key_Return, Qt::Key_Enter, Qt::Key_P});
        if (m_pad.isConnected()) {
            const double x = m_pad.axisLeftX();
            in.left |= m_pad.buttonLeft() || x < -0.35;
            in.right |= m_pad.buttonRight() || x > 0.35;
            in.jump |= m_pad.buttonA();
            in.run |= m_pad.buttonX() || m_pad.buttonB();
            in.start |= m_pad.buttonStart();
            const bool select = m_pad.buttonSelect(), next = m_pad.buttonY();
            if (select && !m_padSelect)
                toggleMusic();
            if (next && !m_padNext)
                nextSong();
            m_padSelect = select;
            m_padNext = next;
        }
        if (m_opt.autoplay)
            autoplay(in);
        return in;
    }

    // A simple bot, to look at the game without playing it.
    void autoplay(Input &in) {
        if (m_state == State::Title || m_state == State::GameOver || m_state == State::Win) {
            in.start = m_tick % 60 == 30;
            return;
        }
        in = Input();
        in.right = true;
        in.run = true;
        if (m_opt.invincible)
            m_p.star = std::max(m_p.star, 60);
        if (m_p.b.y > VH) { // the bot is only here to look at the stages, so it does not fall for good
            m_p.b.y = 0;
            m_p.b.vy = 0;
        }
        const Body &b = m_p.b;
        const int ax = int((b.x + b.w + 6) / TILE), foot = int((b.y + b.h - 1) / TILE);
        const int px = int((b.x + b.w + 14) / TILE);
        bool jump = m_lv.solid(ax, foot) || m_lv.solid(ax, foot - 1);
        bool pit = true;
        for (int r = foot + 1; r <= foot + 3; ++r)
            if (m_lv.solid(px, r))
                pit = false;
        jump |= pit && b.ground;
        bool pitSoon = false;
        for (int dx = 2; dx <= 6; ++dx)
            if (!m_lv.solid(int(b.x / TILE) + dx, foot + 1) && !m_lv.solid(int(b.x / TILE) + dx, foot + 2))
                pitSoon = true;
        for (const Enemy &e : m_enemies)
            if (e.alive && e.awake && e.state != Knocked && e.b.x > b.x && e.b.x - b.x < 44 &&
                std::abs(e.b.y - b.y) < 24 && !pitSoon)
                jump = true;
        in.jump = (jump && b.ground) || (!b.ground && b.vy < 0);
    }

    // ---- flow ----------------------------------------------------------------------------------------------
    void newGame(int level) {
        m_lives = 3;
        m_score = 0;
        m_coins = 0;
        m_powered = false;
        m_levelIdx = level;
        loadLevel(level);
        m_state = State::Ready;
        m_stateT = 110;
    }

    void loadLevel(int idx) {
        m_lv = buildLevel(idx);
        m_levelIdx = idx;
        m_enemies.clear();
        m_items.clear();
        m_particles.clear();
        m_bumps.clear();
        for (const Level::Spawn &s : m_lv.spawns) {
            Enemy e;
            e.t = s.type == '1' ? Poyo : s.type == '2' ? Kuri : s.type == '3' ? Kame : Fuwa;
            e.b.w = 12;
            e.b.h = 12;
            e.b.x = float(s.x * TILE + 2);
            e.b.y = float((s.y + 1) * TILE) - e.b.h;
            e.startX = e.b.x;
            e.baseY = e.b.y;
            e.phase = float(s.x);
            m_enemies.push_back(e);
        }
        m_p = Player();
        m_p.b.w = 10;
        m_p.b.h = 14;
        m_p.b.x = float(m_lv.start.x());
        m_p.b.y = float(m_lv.start.y());
        m_p.powered = m_powered;
        m_cam = 0;
        m_state = State::Title;
        buildBackground();
    }

    void buildBackground() {
        m_bg = QImage(VW, VH, QImage::Format_RGB32);
        QPainter p(&m_bg);
        for (int y = 0; y < VH; ++y) {
            const double t = double(y) / VH;
            p.fillRect(0, y, VW, 1,
                       QColor(int(m_lv.skyTop.red() * (1 - t) + m_lv.skyBottom.red() * t),
                              int(m_lv.skyTop.green() * (1 - t) + m_lv.skyBottom.green() * t),
                              int(m_lv.skyTop.blue() * (1 - t) + m_lv.skyBottom.blue() * t)));
        }
        if (m_lv.stars)
            for (int i = 0; i < 60; ++i)
                p.fillRect((i * 97) % VW, (i * 53) % 110, (i % 5 == 0) ? 2 : 1, (i % 5 == 0) ? 2 : 1,
                           QColor(255, 255, 255, 200));
        p.end();
    }

    void die() {
        if (m_state != State::Play)
            return;
        m_state = State::Dying;
        m_stateT = 0;
        m_p.b.vy = -6;
        m_p.b.vx = 0;
        m_audio.play(Sfx::Die, 0, -4);
        rumble(1.0, 0.6, 400);
    }

    void hurt() {
        if (m_p.invuln > 0 || m_p.star > 0 || m_state != State::Play)
            return;
        if (m_p.powered) {
            m_p.powered = false;
            m_powered = false;
            m_p.invuln = 120;
            m_audio.play(Sfx::Hurt);
            rumble(0.6, 0.3, 200);
        } else {
            die();
        }
    }

    void rumble(double strong, double weak, int ms) {
        if (m_pad.isConnected() && m_pad.canRumble())
            m_pad.rumble(strong, weak, ms);
    }

    void addCoin() {
        m_score += 100;
        if (++m_coins >= 100) {
            m_coins = 0;
            ++m_lives;
            m_audio.play(Sfx::Star, 12);
            toast("1UP!");
        }
    }

    // ---- simulation ----------------------------------------------------------------------------------------
    void onTimer() {
        const qint64 now = m_clock.nsecsElapsed();
        m_acc += now - m_last;
        m_last = now;
        constexpr qint64 frame = 16666667;
        if (m_acc > 250000000)
            m_acc = 250000000;
        while (m_acc >= frame) {
            step();
            m_acc -= frame;
        }
        update();
    }

    void step() {
        ++m_tick;
        const Input prev = m_in;
        m_in = readInput();
        m_jumpPressed = m_in.jump && !prev.jump;
        const bool startPressed = m_in.start && !prev.start;
        if (m_toastT > 0)
            --m_toastT;
        for (auto &p : m_petals) {
            p.x += p.vx + std::sin(p.y * 0.04f + float(p.life) * 0.01f) * 0.3f;
            p.y += p.vy;
            if (p.y > VH + 4) {
                p.y = -4;
                p.x = float(QRandomGenerator::global()->bounded(VW));
            }
            if (p.x > VW + 4)
                p.x = -4;
        }

        switch (m_state) {
        case State::Title:
            m_cam = float(std::fmod(m_tick * 0.7, (m_lv.w - 24) * TILE - VW));
            updateParade();
            if (startPressed)
                newGame(0);
            break;
        case State::Ready:
            if (--m_stateT <= 0)
                m_state = State::Play;
            break;
        case State::Play:
            if (startPressed)
                m_state = State::Paused;
            else
                updateWorld(true);
            break;
        case State::Paused:
            if (startPressed)
                m_state = State::Play;
            break;
        case State::Dying:
            m_p.b.vy = std::min(m_p.b.vy + 0.35f, 7.0f);
            m_p.b.y += m_p.b.vy;
            updateParticles();
            if (++m_stateT > 110) {
                if (--m_lives > 0) {
                    m_powered = false;
                    loadLevel(m_levelIdx);
                    m_state = State::Ready;
                    m_stateT = 90;
                } else {
                    m_state = State::GameOver;
                    m_stateT = 0;
                    m_audio.jingle(false);
                }
            }
            break;
        case State::Clear:
            updateWorld(false);
            if (++m_stateT % 28 == 0)
                firework();
            if (m_stateT > 260) {
                if (m_levelIdx + 1 < kLevels) {
                    loadLevel(m_levelIdx + 1);
                    m_state = State::Ready;
                    m_stateT = 100;
                } else {
                    loadLevel(0);
                    m_state = State::Win;
                    m_stateT = 0;
                }
            }
            break;
        case State::Win:
            ++m_stateT;
            m_cam = 0;
            updateParade();
            if (m_stateT % 20 == 0)
                firework();
            updateParticles();
            if (m_stateT > 90 && startPressed)
                loadLevel(0);
            break;
        case State::GameOver:
            ++m_stateT;
            if (m_stateT > 90 && startPressed)
                loadLevel(0);
            break;
        }
    }

    void firework() {
        const float x = m_cam + 40 + float(QRandomGenerator::global()->bounded(VW - 80));
        const float y = 20 + float(QRandomGenerator::global()->bounded(70));
        const QColor c = QColor::fromHsv(int(QRandomGenerator::global()->bounded(360)), 140, 255);
        for (int i = 0; i < 26; ++i) {
            const float a = float(i) * 6.2832f / 26;
            const float s = 1.2f + float(QRandomGenerator::global()->bounded(100)) / 80.0f;
            m_particles.push_back({x, y, std::cos(a) * s, std::sin(a) * s, 50, 0, c, 2});
        }
        m_audio.play(Sfx::Coin, float(QRandomGenerator::global()->bounded(12)), -8);
    }

    struct Player {
        Body b;
        bool powered = false;
        int invuln = 0, star = 0, coyote = 0, buffer = 0;
        float anim = 0;
    };

    void updateWorld(bool control) {
        updatePlayer(control);
        updateItems();
        for (Enemy &e : m_enemies)
            updateEnemy(e);
        enemyContacts();
        for (auto it = m_bumps.begin(); it != m_bumps.end();)
            it = (--it->t <= 0) ? m_bumps.erase(it) : it + 1;
        updateParticles();
        const float target = std::clamp(m_p.b.cx() - VW / 2.0f + 24, 0.0f, float(m_lv.w * TILE - VW));
        m_cam += (target - m_cam) * 0.12f;
        m_cam = std::clamp(m_cam, 0.0f, float(m_lv.w * TILE - VW));
    }

    void updateParticles() {
        for (Particle &p : m_particles) {
            p.x += p.vx;
            p.y += p.vy;
            p.vy += p.kind == 0 ? 0.04f : 0.3f;
            --p.life;
        }
        m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle &p) { return p.life <= 0; }),
                          m_particles.end());
    }

    void updatePlayer(bool control) {
        Player &p = m_p;
        Body &b = p.b;
        Input in = control ? m_in : Input();
        const int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
        const float maxv = in.run ? 2.4f : 1.5f;
        if (dir) {
            float acc = b.ground ? (in.run ? 0.12f : 0.09f) : 0.07f;
            if (dir * b.vx < 0)
                acc *= b.ground ? 2.5f : 1.5f;
            b.vx += float(dir) * acc;
            if (std::abs(b.vx) > maxv) {
                if (b.ground)
                    b.vx -= (b.vx > 0 ? 1.0f : -1.0f) * std::min(0.1f, std::abs(b.vx) - maxv);
                else
                    b.vx = std::clamp(b.vx, -2.4f, 2.4f);
            }
        } else if (b.ground) {
            b.vx -= (b.vx > 0 ? 1.0f : -1.0f) * std::min(std::abs(b.vx), 0.1f);
        }
        if (control && m_jumpPressed)
            p.buffer = 6;
        else if (p.buffer > 0)
            --p.buffer;
        p.coyote = b.ground ? 6 : std::max(0, p.coyote - 1);
        if (p.buffer > 0 && p.coyote > 0) {
            b.vy = std::abs(b.vx) > 2.0f ? -6.2f : -5.8f;
            p.buffer = p.coyote = 0;
            m_audio.play(Sfx::Jump);
        }
        const float g = (b.vy < 0 && in.jump) ? 0.22f : 0.42f;
        b.vy = std::min(b.vy + g, 6.0f);
        moveBody(m_lv, b, [this](int tx, int ty) { hitBlock(tx, ty); });
        p.anim += std::abs(b.vx) * 0.12f;
        if (p.invuln > 0)
            --p.invuln;
        if (p.star > 0) {
            --p.star;
            if (m_tick % 3 == 0)
                m_particles.push_back({b.cx() + float(QRandomGenerator::global()->bounded(12)) - 6,
                                       b.y + float(QRandomGenerator::global()->bounded(14)), 0, -0.3f, 25,
                                       0, QColor::fromHsv(int(m_tick * 12 % 360), 120, 255), 2});
        }

        if (!control) { // after the goal: walk to Fuku-chan
            const float stop = float((m_lv.goalX + 3) * TILE - 18);
            b.vx = b.cx() < stop ? 1.1f : 0;
            if (b.ground && b.vx == 0 && m_stateT % 50 == 25) {
                b.vy = -4;
                m_audio.play(Sfx::Jump, 7, -6);
            }
            return;
        }

        if (b.y > VH + 20) {
            die();
            return;
        }
        const int x0 = int(std::floor(b.x / TILE)), x1 = int(std::floor((b.x + b.w - 0.01f) / TILE));
        const int y0 = int(std::floor(b.y / TILE)), y1 = int(std::floor((b.y + b.h - 0.01f) / TILE));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (m_lv.at(x, y) == 'c') {
                    m_lv.g[size_t(y)][size_t(x)] = '.';
                    addCoin();
                    m_audio.play(Sfx::Coin, float(std::min(m_coins % 8, 7)));
                    sparkle(float(x * TILE + 8), float(y * TILE + 8), 6, QColor("#ffd84a"));
                }
        if (b.cx() > float(m_lv.goalX * TILE + 12)) {
            m_state = State::Clear;
            m_stateT = 0;
            m_score += 1000 + (p.powered ? 500 : 0);
            m_audio.jingle(true);
            for (int i = 0; i < 3; ++i)
                firework();
        }
    }

    void sparkle(float x, float y, int n, const QColor &c) {
        for (int i = 0; i < n; ++i) {
            const float a = float(QRandomGenerator::global()->bounded(628)) / 100.0f;
            m_particles.push_back({x, y, std::cos(a) * 1.2f, std::sin(a) * 1.2f - 0.5f, 24, 0, c, 2});
        }
    }

    void hitBlock(int tx, int ty) {
        const char c = m_lv.at(tx, ty);
        auto set = [&](char n) { m_lv.g[size_t(ty)][size_t(tx)] = n; };
        auto spawnItem = [&](int type) {
            Item it;
            it.type = type;
            it.b.w = 12;
            it.b.h = 12;
            it.b.x = float(tx * TILE + 2);
            it.b.y = float(ty * TILE + 2);
            m_items.push_back(it);
        };
        switch (c) {
        case '?':
            set('x');
            m_particles.push_back({float(tx * TILE), float(ty * TILE - 16), 0, -5, 28, 1, QColor(), 0});
            addCoin();
            m_audio.play(Sfx::Coin);
            break;
        case 'O':
            set('x');
            spawnItem(0);
            m_audio.play(Sfx::Bump, 5);
            break;
        case '*':
            set('x');
            spawnItem(1);
            m_audio.play(Sfx::Bump, 5);
            break;
        case 'B':
            if (m_p.powered) {
                set('.');
                for (int i = 0; i < 4; ++i)
                    m_particles.push_back({float(tx * TILE + 4 + (i % 2) * 8), float(ty * TILE + 4 + (i / 2) * 8),
                                           (i % 2 ? 1.2f : -1.2f), (i / 2) ? -2.0f : -3.2f, 40, 2, QColor("#ffa6c4"), 4});
                m_audio.play(Sfx::Break, 6, -6);
                m_score += 50;
            } else {
                m_audio.play(Sfx::Bump);
            }
            break;
        default:
            return;
        }
        m_bumps.push_back({tx, ty, 10});
        for (Enemy &e : m_enemies)
            if (e.alive && e.state != Knocked && std::abs(e.b.bottom() - float(ty * TILE)) < 3 &&
                e.b.x + e.b.w > float(tx * TILE) && e.b.x < float((tx + 1) * TILE))
                knock(e, e.b.cx() < float(tx * TILE + 8) ? -1 : 1);
    }

    void knock(Enemy &e, int dir) {
        e.state = Knocked;
        e.b.vx = float(dir) * 1.2f;
        e.b.vy = -3.5f;
        m_score += 100;
        m_audio.play(Sfx::Stomp, 4);
    }

    void updateItems() {
        for (Item &it : m_items) {
            if (!it.alive)
                continue;
            Body &b = it.b;
            if (it.emerge > 0) {
                b.y -= 1;
                --it.emerge;
                continue;
            }
            b.vy = std::min(b.vy + 0.3f, 5.0f);
            b.vx = float(it.dir) * (it.type == 0 ? 0.9f : 1.2f);
            moveBody(m_lv, b);
            if (b.wall)
                it.dir = -it.dir;
            if (b.ground && it.type == 1)
                b.vy = -4.2f;
            if (b.y > VH + 20)
                it.alive = false;
            if (m_state == State::Play && overlap(b, m_p.b)) {
                it.alive = false;
                if (it.type == 0) {
                    m_p.powered = m_powered = true;
                    m_score += 1000;
                    m_audio.play(Sfx::PowerUp);
                    rumble(0.3, 0.5, 150);
                } else {
                    m_p.star = 600;
                    m_score += 1000;
                    m_audio.play(Sfx::Star);
                    toast("Kirari power!");
                    rumble(0.4, 0.4, 250);
                }
                sparkle(b.cx(), b.y, 10, QColor("#ffffff"));
            }
        }
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](const Item &i) { return !i.alive; }), m_items.end());
    }

    void updateEnemy(Enemy &e) {
        if (!e.alive)
            return;
        Body &b = e.b;
        if (e.state == Knocked) {
            b.vy += 0.35f;
            b.x += b.vx;
            b.y += b.vy;
            if (b.y > VH + 40)
                e.alive = false;
            return;
        }
        if (!e.awake) {
            if (b.x > m_cam - 24 && b.x < m_cam + VW + 32)
                e.awake = true;
            else
                return;
        }
        switch (e.t) {
        case Fuwa:
            e.phase += 0.06f;
            b.x += float(e.dir) * 0.5f;
            if (std::abs(b.x - e.startX) > 48)
                e.dir = -e.dir;
            b.y = e.baseY + std::sin(e.phase) * 9;
            return;
        default:
            break;
        }
        if (e.state == Squished) {
            if (--e.timer <= 0)
                e.alive = false;
            return;
        }
        float speed = e.t == Poyo ? 0.45f : e.t == Kuri ? 0.6f : 0.5f;
        if (e.state == ShellIdle)
            speed = 0;
        if (e.state == ShellMoving)
            speed = 3.8f;
        b.vx = float(e.dir) * speed;
        b.vy = std::min(b.vy + 0.3f, 5.0f);
        if (e.t == Poyo && e.state == Walk && b.ground && ++e.timer > 50) {
            b.vy = -3.2f;
            e.timer = 0;
        }
        if (e.t == Kame && e.state == Walk && b.ground) {
            const int ahead = int(std::floor((b.cx() + float(e.dir) * 9) / TILE));
            if (!m_lv.solid(ahead, int(std::floor((b.bottom() + 2) / TILE))))
                e.dir = -e.dir, b.vx = float(e.dir) * speed;
        }
        moveBody(m_lv, b);
        if (b.wall) {
            e.dir = -e.dir;
            if (e.state == ShellMoving)
                m_audio.play(Sfx::Bump, -3, -6);
        }
        if (e.state == ShellMoving) {
            if (e.timer > 0)
                --e.timer;
            for (Enemy &o : m_enemies)
                if (&o != &e && o.alive && o.state != Knocked && o.state != Squished && overlap(b, o.b))
                    knock(o, e.dir);
        }
        if (b.y > VH + 40)
            e.alive = false;
    }

    void enemyContacts() {
        if (m_state != State::Play)
            return;
        Body &pb = m_p.b;
        for (Enemy &e : m_enemies) {
            if (!e.alive || !e.awake || e.state == Knocked || e.state == Squished || !overlap(pb, e.b))
                continue;
            if (m_p.star > 0) {
                knock(e, pb.cx() < e.b.cx() ? 1 : -1);
                continue;
            }
            const bool stomp = pb.vy > 0 && e.t != Fuwa && (pb.bottom() - pb.vy) <= e.b.y + 7;
            if (stomp) {
                pb.vy = m_in.jump ? -6.0f : -4.2f;
                m_score += 100;
                m_audio.play(Sfx::Stomp);
                if (e.t == Kame) {
                    if (e.state == Walk) {
                        e.state = ShellIdle;
                        e.b.h = 10;
                        e.b.y += 2;
                    } else if (e.state == ShellMoving) {
                        e.state = ShellIdle;
                    } else {
                        kick(e);
                    }
                } else {
                    e.state = Squished;
                    e.timer = 25;
                }
                continue;
            }
            if (e.t == Kame && e.state == ShellIdle) {
                kick(e);
                continue;
            }
            if (e.t == Kame && e.state == ShellMoving && e.timer > 0)
                continue;
            hurt();
        }
    }

    void kick(Enemy &e) {
        e.state = ShellMoving;
        e.dir = e.b.cx() < m_p.b.cx() ? -1 : 1;
        e.timer = 10;
        m_audio.play(Sfx::Kick);
    }

    // ---- drawing -------------------------------------------------------------------------------------------
    void blit(QPainter &p, const Spr &s, float x, float y) { p.drawImage(int(std::lround(x)), int(std::lround(y)), s.img); }
    // The sprite is placed so that its feet are on `bottom`, centered on `cx`.
    void blitFeet(QPainter &p, const Spr &s, float cx, float bottom) {
        blit(p, s, cx - float(s.img.width()) / 2, bottom - float(s.img.height()) + float(s.pad));
    }

    void render(QPainter &out, const QSize &size) {
        m_scale = std::min(double(size.width()) / VW, double(size.height()) / VH);
        m_ox = (size.width() - VW * m_scale) / 2;
        m_oy = (size.height() - VH * m_scale) / 2;
        out.fillRect(0, 0, size.width(), size.height(), QColor("#4a3040"));
        {
            QPainter p(&m_canvas);
            drawWorld(p);
            drawOverlay(p);
        }
        out.drawImage(QRectF(m_ox, m_oy, VW * m_scale, VH * m_scale), m_canvas);
        // The title is drawn at window resolution: bubble letters, and kana from a system font.
        if (m_state == State::Title) {
            static const QColor pastel[] = {QColor("#ff8fb8"), QColor("#ffa877"), QColor("#ffd84a"),
                                            QColor("#7fe0b0"), QColor("#74cdf2"), QColor("#b9a0ff")};
            static const QColor cream[] = {QColor("#fff6ea")};
            const double cx = m_ox + VW / 2.0 * m_scale;
            bubbleText(out, "Mochimaru", cx, m_oy + 13 * m_scale, 3.4 * m_scale, m_tick, 0, pastel, 6, !m_gl);
            bubbleText(out, "no Daibouken", cx, m_oy + 50 * m_scale, 1.8 * m_scale, m_tick + 20, 3, cream, 1, !m_gl);
            text(out, QString::fromUtf8("もちまるのだいぼうけん"), 0, 68, VW, 8, QColor("#ffd84a"), Qt::AlignHCenter,
                 "Noto Sans CJK JP");
        }
    }

    void drawWorld(QPainter &p) {
        p.drawImage(0, 0, m_bg);
        const int cam = int(std::lround(m_cam));

        // Hills that move slower than the ground.
        for (int layer = 0; layer < 2; ++layer) {
            const double f = layer == 0 ? 0.2 : 0.4;
            const int base = layer == 0 ? 120 : 140;
            const QColor col = layer == 0 ? m_lv.hillFar : m_lv.hillNear;
            for (int x = 0; x < VW; ++x) {
                const double wx = x + cam * f;
                const int h = int(34 - (layer ? 12 : 0) + 14 * std::sin(wx * (layer ? 0.035 : 0.022) + layer) +
                                  6 * std::sin(wx * 0.07));
                p.fillRect(x, base - h + 30, 1, VH, col);
            }
        }
        // Clouds.
        for (int i = 0; i < 7; ++i) {
            double x = std::fmod(i * 97.0 + 40 - cam * 0.25, VW + 64.0);
            if (x < 0)
                x += VW + 64.0;
            blit(p, m_art["cloud"], float(x - 32), float(8 + (i * 23) % 56));
        }
        // Bushes behind the ground.
        const int first = cam / TILE, last = (cam + VW) / TILE + 1;
        for (int x = first; x <= last; ++x) {
            if (m_lv.at(x, 10) != '#' || m_lv.at(x, 9) != '.')
                continue;
            const unsigned h = unsigned(x) * 2654435761u >> 16;
            if (h % 9 == 0)
                blit(p, m_art["bush"], float(x * TILE - cam), float(9 * TILE));
            else if (h % 9 == 4)
                blit(p, m_art["bush_sakura"], float(x * TILE - cam), float(9 * TILE));
        }
        // The goal.
        {
            const float gx = float(m_lv.goalX * TILE - cam);
            blit(p, m_art["torii2"], gx, float(8 * TILE));
            const bool wave = (m_tick / 22) % 2;
            blitFeet(p, m_art[wave ? "fuku2" : "fuku1"], float((m_lv.goalX + 3) * TILE + 8 - cam), float(10 * TILE));
        }
        // Items come out from behind their block.
        for (const Item &it : m_items)
            blitFeet(p, m_art[it.type == 0 ? "onigiri" : "kirari"], it.b.cx() - float(cam), it.b.bottom());
        // Tiles.
        for (int y = 0; y < ROWS; ++y)
            for (int x = first; x <= last; ++x) {
                const char c = m_lv.at(x, y);
                const char *id = nullptr;
                switch (c) {
                case '#': id = "grass"; break;
                case '=': id = "soil"; break;
                case 'B': id = "brick"; break;
                case '?':
                case 'O':
                case '*': id = "hatena"; break;
                case 'x': id = "used"; break;
                case 'S': id = "stone"; break;
                case 'T': id = "bamboo_top"; break;
                case 'U': id = "bamboo_body"; break;
                case 'c': {
                    static const char *frames[] = {"coin0", "coin1", "coin2", "coin3", "coin2", "coin1"};
                    blit(p, m_art[frames[(m_tick / 7 + x) % 6]], float(x * TILE - cam), float(y * TILE));
                    break;
                }
                default: break;
                }
                if (!id)
                    continue;
                float dy = 0;
                for (const Bump &b : m_bumps)
                    if (b.tx == x && b.ty == y)
                        dy = -std::sin(float(b.t) / 10.0f * 3.1416f) * 5;
                blit(p, m_art[id], float(x * TILE - cam), float(y * TILE) + dy);
            }
        // Enemies.
        for (const Enemy &e : m_enemies) {
            if (!e.alive)
                continue;
            std::string id;
            const int f = (m_tick / (e.t == Kuri ? 12 : 16)) % 2 + 1;
            switch (e.t) {
            case Poyo: id = e.state == Squished ? "poyo_flat" : (e.b.ground ? "poyo1" : "poyo2"); break;
            case Kuri: id = e.state == Squished ? "kuri_flat" : "kuri" + std::to_string(f); break;
            case Kame:
                id = (e.state == ShellIdle || e.state == ShellMoving) ? "kame_shell" : "kame" + std::to_string(f);
                break;
            case Fuwa: id = "fuwa" + std::to_string(f); break;
            }
            if (e.state == Knocked)
                id += "_v";
            blitFeet(p, m_art[id], e.b.cx() - float(cam), e.b.bottom());
        }
        // Mochimaru.
        if (m_state != State::Title && m_state != State::Win && !(m_p.invuln > 0 && (m_tick / 3) % 2)) {
            const Body &b = m_p.b;
            std::string id = "hero_idle";
            if (m_state == State::Dying)
                id = "hero_hurt";
            else if (!b.ground)
                id = "hero_jump";
            else if (std::abs(b.vx) > 0.2f)
                id = (int(m_p.anim) % 2) ? "hero_walk2" : "hero_walk1";
            if (m_p.powered)
                id += "_p";
            blitFeet(p, m_art[id], b.cx() - float(cam), b.bottom());
        }
        // Particles.
        for (const Particle &pt : m_particles) {
            if (pt.kind == 1) {
                static const char *frames[] = {"coin0", "coin1", "coin2", "coin3"};
                blit(p, m_art[frames[(m_tick / 3) % 4]], pt.x - float(cam), pt.y);
            } else {
                p.fillRect(int(pt.x) - cam, int(pt.y), pt.size, pt.size, pt.col);
            }
        }
        if (m_state == State::Title || m_state == State::Win)
            drawParade(p);
        // Sakura petals.
        for (const auto &pt : m_petals)
            p.fillRect(int(pt.x), int(pt.y), 3, 2, QColor(255, 190, 215, 220));
    }

    // ---- the parade on the title screen ------------------------------------------------------------------
    // The level scrolls under the parade, so its walkers have to get over pits, bamboo and stairs. Each one keeps
    // a place on the screen. When something is in the way it plans one jump, from where it stands to the next
    // flat footing, so it never falls in; after landing it walks back into its place.

    // The top of the ground at world x, or a big number over a pit. Floating blocks don't count.
    float surfaceAt(float wx) const {
        const int c = int(std::floor(wx / TILE));
        if (!m_lv.solid(c, ROWS - 1))
            return 1e6f;
        int r = ROWS - 1;
        while (r > 0 && m_lv.solid(c, r - 1))
            --r;
        return float(r * TILE);
    }

    struct Walker {
        float x = 0, home = 0, y = 0, vy = 0; // x on the screen, y the feet
        bool air = false;
        float fromX = 0, toX = 0, fromY = 0, toY = 0, arc = 0; // the jump, in the world
    };
    static constexpr float kHalfFoot = 6;

    // The ground under a walker standing at world x: the highest point under its feet.
    float footing(float wx) const { return std::min(surfaceAt(wx - kHalfFoot), surfaceAt(wx + kHalfFoot)); }

    void resetParade() {
        static const float homes[] = {40, 82, 118, 160, 202, 244, 280};
        for (int i = 0; i < 7; ++i) {
            Walker &w = m_parade[i];
            w = Walker();
            w.x = w.home = homes[i];
            w.y = footing(w.x + m_cam);
            if (w.y > VH)
                w.y = float(10 * TILE);
        }
        m_paradeCam = m_cam;
    }

    // Plans a jump for a walker at world x on ground y, if something is in its way.
    bool planJump(Walker &w, float wx) {
        bool blocked = false;
        for (float x = wx + kHalfFoot; x <= wx + kHalfFoot + 8; x += 2)
            if (surfaceAt(x) > 1e5f || surfaceAt(x) < w.y - 6)
                blocked = true;
        if (!blocked)
            return false;
        // Land on the first place past the obstacle that is flat for a little wider than a foot: clear over the
        // obstacle back to ground no higher than here if that is in reach, else up onto it (stairs).
        for (int pass = 0; pass < 2; ++pass) {
            for (float c = wx + 20; c <= wx + 150; c += 2) {
                const float s = surfaceAt(c);
                bool flat = s < 1e5f && (pass == 1 || s >= w.y - 1);
                for (float x = c - kHalfFoot - 4; flat && x <= c + kHalfFoot + 4; x += 2)
                    flat = surfaceAt(x) == s;
                if (!flat || (std::abs(s - w.y) < 1 && c < wx + 40))
                    continue;
                float top = std::min(w.y, s);
                for (float x = wx; x <= c; x += 2)
                    top = std::min(top, surfaceAt(x));
                w.air = true;
                w.fromX = wx;
                w.toX = c;
                w.fromY = w.y;
                w.toY = s;
                w.arc = ((w.y + s) / 2 - top) * 1.4f + 14;
                return true;
            }
        }
        return false;
    }

    void updateParade() {
        if (std::abs(m_cam - m_paradeCam) > 2)
            resetParade(); // the scroll wrapped round, or the title screen has just come up
        m_paradeCam = m_cam;
        for (int i = 0; i < 7; ++i) {
            Walker &w = m_parade[i];
            const float wx = w.x + m_cam;
            if (i == 5) { // Fuwa flies: it floats above whatever is below, and over pits it keeps its height.
                float top = 1e6f;
                for (float x = wx - 16; x <= wx + 24; x += 4)
                    top = std::min(top, surfaceAt(x));
                if (top < 1e5f)
                    w.y += (top - 18 - w.y) * 0.08f;
                continue;
            }
            if (w.air) { // along the planned arc, a little faster than walking
                const float nx = wx + 2.0f;
                const float t = std::min(1.0f, (nx - w.fromX) / (w.toX - w.fromX));
                w.x = std::min(nx, w.toX) - m_cam;
                w.y = w.fromY + (w.toY - w.fromY) * t - 4 * w.arc * t * (1 - t);
                if (t >= 1)
                    w.air = false;
                continue;
            }
            const float under = footing(wx);
            if (under > w.y + 0.5f && under < 1e5f) { // stepping down: drop, gently
                w.vy += 0.3f;
                w.y = std::min(under, w.y + w.vy);
                continue;
            }
            w.vy = 0;
            if (under < 1e5f)
                w.y = under;
            if (!planJump(w, wx) && w.x > w.home)
                w.x = std::max(w.home, w.x - 0.35f); // back into its place
        }
    }

    void drawParade(QPainter &p) {
        const int f = (m_tick / 14) % 2 + 1;
        static const char *ids[] = {"fuku", "poyo", "kuri", "hero_walk", "kame", "fuwa", "onigiri"};
        for (int i = 0; i < 7; ++i) {
            const Walker &w = m_parade[i];
            std::string id = ids[i];
            if (i == 3 && w.air)
                id = "hero_jump";
            else if (i != 6)
                id += std::to_string(f);
            float y = w.y;
            if (i == 3 && !w.air)
                y -= std::abs(std::sin(m_tick * 0.12f)) * 6;
            if (i == 5)
                y += std::sin(m_tick * 0.06f) * 4;
            blitFeet(p, m_art[id], w.x, y);
        }
    }

    void text(QPainter &p, const QString &s, double vx, double vy, double vw, int px, const QColor &c,
              int align = Qt::AlignHCenter, const char *family = "Sans Serif") {
        QFont f(family);
        f.setPixelSize(std::max(6, int(px * m_scale)));
        f.setBold(true);
        p.setFont(f);
        const QRect r(int(m_ox + vx * m_scale), int(m_oy + vy * m_scale), int(vw * m_scale), int(px * m_scale * 1.6));
        const int sh = std::max(1, int(m_scale * 0.7));
        p.setPen(QColor(74, 48, 64, 230));
        p.drawText(r.translated(sh, sh), align, s);
        p.setPen(c);
        p.drawText(r, align, s);
    }

    void drawOverlay(QPainter &p) {
        const QColor white("#ffffff"), pink("#ff9fc2"), yellow("#ffd84a"), pale("#ffe3ef");
        const int cx = VW / 2;
        auto blink = [&] { return (m_tick / 30) % 2 == 0; };
        if (m_state == State::Title) {
            // The title itself is drawn in render(), in bubble letters.
            if (blink())
                pixelText(p, m_pad.isConnected() ? "Press START" : "Press ENTER or START", cx, 118, 1, white,
                          Align::Center);
            pixelText(p, "# Stage by stage, find Fuku-chan! #", cx, 168, 1, pale, Align::Center);
            pixelText(p, m_pad.isConnected() ? "Pad: " + m_pad.name() : QString("Plug in a gamepad, or use the keyboard"),
                      cx, 179, 1, pale, Align::Center);
        } else if (m_state != State::Paused) {
            pixelText(p, QString("# x%1").arg(m_lives), 6, 5, 1, white);
            blit(p, m_art["coin0"], float(cx - 24), 1);
            pixelText(p, QString("x%1").arg(m_coins, 2, 10, QChar('0')), cx - 6, 5, 1, yellow);
            pixelText(p, QString("%1").arg(m_score, 6, 10, QChar('0')), VW - 6, 5, 1, white, Align::Right);
            pixelText(p, QString("1-%1 %2").arg(m_levelIdx + 1).arg(m_lv.name), 6, 16, 1, pale);
        }
        if (m_state == State::Ready) {
            pixelText(p, QString("Stage 1-%1").arg(m_levelIdx + 1), cx, 58, 3, white, Align::Center, m_tick, true);
            pixelText(p, m_lv.name, cx, 88, 1, pink, Align::Center);
        } else if (m_state == State::Paused) {
            p.fillRect(0, 0, VW, VH, QColor(74, 48, 64, 200));
            pixelText(p, "Paused", cx, 8, 3, white, Align::Center, m_tick, true);
            struct Row {
                const char *what, *pad, *keys;
            };
            static const Row rows[] = {{"Move", "D-pad / stick", "Arrows / WASD"},
                                       {"Jump", "A", "Space / Z / Up"},
                                       {"Run", "X or B", "X / Shift"},
                                       {"Pause", "Start", "Esc / Enter"},
                                       {"Music on/off", "Select", "M"},
                                       {"Next song", "Y", "N"},
                                       {"Fullscreen", "", "Alt+Enter / F11"},
                                       {"Quit", "", "Q"}};
            pixelText(p, "Gamepad", 108, 36, 1, yellow);
            pixelText(p, "Keyboard", 206, 36, 1, yellow);
            int y = 48;
            for (const Row &r : rows) {
                pixelText(p, r.what, 16, y, 1, pink);
                pixelText(p, r.pad, 108, y, 1, white);
                pixelText(p, r.keys, 206, y, 1, white);
                y += 12;
            }
            if (blink())
                pixelText(p, "Esc or Start: go on", cx, 166, 1, yellow, Align::Center);
        } else if (m_state == State::Clear) {
            pixelText(p, "Stage clear!", cx, 52, 3, white, Align::Center, m_tick, true);
            if (m_stateT > 100)
                pixelText(p, "Fuku-chan: arigato!", cx, 82, 1, white, Align::Center);
        } else if (m_state == State::GameOver) {
            pixelText(p, "Game over", cx, 58, 3, white, Align::Center, m_tick);
            if (m_stateT > 90 && blink())
                pixelText(p, "Press START", cx, 94, 1, pink, Align::Center);
        } else if (m_state == State::Win) {
            pixelText(p, "Yay! You win!", cx, 34, 2, white, Align::Center, m_tick, true);
            pixelText(p, QString("Score %1").arg(m_score, 6, 10, QChar('0')), cx, 56, 1, yellow, Align::Center);
            if (m_stateT > 90 && blink())
                pixelText(p, "Press START", cx, 90, 1, pink, Align::Center);
        }
        if (m_toastT > 0)
            pixelText(p, m_toast.left(50), cx, VH - 12, 1, white, Align::Center);
    }

    Options m_opt;
    Audio m_audio;
    Art m_art;
    QGamepad m_pad;
    bool m_padSelect = false, m_padNext = false, m_full = false;
    std::set<int> m_keys;
    Input m_in;
    bool m_jumpPressed = false;

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0, m_acc = 0;
    int m_tick = 0;

    State m_state = State::Title;
    int m_stateT = 0, m_levelIdx = 0, m_lives = 3, m_coins = 0, m_score = 0;
    bool m_powered = false;
    Level m_lv;
    Player m_p;
    std::vector<Enemy> m_enemies;
    std::vector<Item> m_items;
    std::vector<Particle> m_particles, m_petals;
    std::vector<Bump> m_bumps;
    float m_cam = 0;
    bool m_gl = false; // painting the OpenGL window, rather than an image
    Walker m_parade[7];
    float m_paradeCam = -1000;

    QImage m_canvas, m_bg;
    double m_scale = 3, m_ox = 0, m_oy = 0;

    std::vector<Song> m_songs;
    int m_song = 0;
    QString m_toast;
    int m_toastT = 0;
};

} // namespace

int main(int argc, char **argv) {
    // --shot draws offscreen; this must be decided before the application starts.
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--shot" || std::string_view(argv[i]) == "--record")
            ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Mochimaru no Daibouken, a platform game.");
    parser.addHelpOption();
    parser.addPositionalArgument("music", "MIDI files, or directories of them, to play as the soundtrack.", "[music...]");
    const QCommandLineOption midiOpt({"m", "midi"}, "A MIDI file for the soundtrack (may be repeated).", "file");
    const QCommandLineOption levelOpt("level", "Start at this stage, 1 to 3.", "n");
    const QCommandLineOption silentOpt("no-sound", "Play without sound.");
    const QCommandLineOption autoOpt("autoplay", "Let a simple bot play.");
    const QCommandLineOption shotOpt("shot", "Draw a frame to a PNG file and quit.", "file");
    const QCommandLineOption framesOpt("frames", "Frames to run before --shot.", "n", "0");
    const QCommandLineOption exportOpt("export-song", "Write the built-in tune as a MIDI file and quit.", "file");
    const QCommandLineOption pausedOpt("paused", "Pause before --shot draws, to see the pause screen.");
    const QCommandLineOption recordOpt("record", "Let the bot play offline and write an MP4 with sound (needs ffmpeg).", "file");
    const QCommandLineOption godOpt("invincible", "With --autoplay: the bot cannot be hurt.");
    parser.addOptions({midiOpt, levelOpt, silentOpt, autoOpt, shotOpt, framesOpt, exportOpt, pausedOpt, recordOpt, godOpt});
    parser.process(app);

    if (parser.isSet(exportOpt)) {
        QFile f(parser.value(exportOpt));
        if (!f.open(QIODevice::WriteOnly) || f.write(builtinSong()) < 0) {
            std::fprintf(stderr, "mochimaru: cannot write %s\n", qPrintable(parser.value(exportOpt)));
            return 1;
        }
        return 0;
    }

    Options opt;
    opt.songs = parser.values(midiOpt) + parser.positionalArguments();
    if (parser.isSet(levelOpt))
        opt.level = std::clamp(parser.value(levelOpt).toInt() - 1, 0, kLevels - 1);
    opt.autoplay = parser.isSet(autoOpt);
    opt.paused = parser.isSet(pausedOpt);
    opt.invincible = parser.isSet(godOpt);
    opt.offline = parser.isSet(recordOpt);
    opt.autoplay = opt.autoplay || opt.offline;
    opt.sound = !parser.isSet(silentOpt) && !parser.isSet(shotOpt);

    Game game(opt);
    if (parser.isSet(recordOpt)) {
        const int frames = parser.value(framesOpt).toInt();
        return game.record(parser.value(recordOpt), frames > 0 ? frames : 3600, QSize(1280, 768)) ? 0 : 1;
    }
    if (parser.isSet(shotOpt)) {
        const QImage img = game.shot(parser.value(framesOpt).toInt(), QSize(960, 576));
        if (!img.save(parser.value(shotOpt))) {
            std::fprintf(stderr, "mochimaru: cannot write %s\n", qPrintable(parser.value(shotOpt)));
            return 1;
        }
        return 0;
    }
    game.show();
    game.start();
    return app.exec();
}
