// MIT License - see LICENSE
//
// The pixel art of "Mochimaru no Daibouken". Every sprite is a grid of letters, one letter per pixel, drawn with
// the palette below, so the art can be reviewed and changed as text. `mirror` builds a sprite twice as wide from
// its left half.
#pragma once

#include <QColor>
#include <QImage>
#include <QRgb>
#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace Sprites {

inline QRgb paletteColor(char c) {
    switch (c) {
    case '.': return qRgba(0, 0, 0, 0);
    case 'K': return qRgb(0x4a, 0x30, 0x40); // outline
    case 'W': return qRgb(0xff, 0xfa, 0xf5); // mochi white
    case 'H': return qRgb(0xff, 0xff, 0xff); // highlight
    case 'S': return qRgb(0xe6, 0xd6, 0xe8); // white's shade
    case 'P': return qRgb(0xff, 0xa6, 0xc4); // pink
    case 'p': return qRgb(0xee, 0x6f, 0x98); // deep pink
    case 'R': return qRgb(0xe8, 0x44, 0x5a); // red
    case 'r': return qRgb(0xb0, 0x30, 0x4a);
    case 'O': return qRgb(0xff, 0xb3, 0x47); // orange
    case 'Y': return qRgb(0xff, 0xd8, 0x4a); // yellow
    case 'y': return qRgb(0xe3, 0xa2, 0x1f);
    case 'B': return qRgb(0xc9, 0x8f, 0x5b); // brown
    case 'b': return qRgb(0x9a, 0x62, 0x38);
    case 'T': return qRgb(0xfb, 0xe3, 0xc0); // cream
    case 'L': return qRgb(0xbf, 0xf2, 0x8a); // light green
    case 'G': return qRgb(0x7e, 0xd9, 0x57); // green
    case 'g': return qRgb(0x4f, 0xae, 0x48);
    case 'E': return qRgb(0x2b, 0x1f, 0x33); // eye
    case 'N': return qRgb(0xc9, 0xc2, 0xda); // stone
    case 'n': return qRgb(0x8f, 0x87, 0xa8);
    case 'D': return qRgb(0x2f, 0x4a, 0x3a); // nori
    case 'Q': return qRgb(0xd6, 0xec, 0xfa); // cloud shade
    case 'k': return qRgb(0x8f, 0xbf, 0xe0); // cloud outline
    default: return 0;
    }
}

struct Def {
    std::string id;
    std::string group; // section of the review sheet
    std::string label; // caption of this frame
    bool mirror = false;
    std::vector<std::string> rows;
};

// Builds a list of rows: rows("....")("....", 3) adds one row, then the same row three times.
struct Rows {
    std::vector<std::string> v;
    Rows &operator()(const char *row, int n = 1) {
        for (; n > 0; --n)
            v.emplace_back(row);
        return *this;
    }
};

// Empty rows and a blank row, so that sprites can be drawn at the bottom of their cell.
inline constexpr const char *kBlank = "................";

// Returns the sprite, or a null image with `error` set when the grid is not rectangular or uses a letter that is
// not in the palette.
inline QImage render(const Def &d, QString *error = nullptr) {
    auto fail = [&](const QString &why) {
        if (error)
            *error = QString::fromStdString(d.id) + ": " + why;
        return QImage();
    };
    if (d.rows.empty())
        return fail("no rows");
    const int w = int(d.rows.front().size());
    const int h = int(d.rows.size());
    if (w != h)
        return fail(QString("%1 pixels wide and %2 high; sprites are square (before mirroring)").arg(w).arg(h));
    for (int y = 0; y < h; ++y)
        if (int(d.rows[size_t(y)].size()) != w)
            return fail(QString("row %1 has %2 pixels, not %3").arg(y).arg(d.rows[size_t(y)].size()).arg(w));
    QImage img(d.mirror ? w * 2 : w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const char c = d.rows[size_t(y)][size_t(x)];
            if (c != '.' && paletteColor(c) == 0)
                return fail(QString("row %1: no palette color '%2'").arg(y).arg(c));
            img.setPixel(x, y, paletteColor(c));
            if (d.mirror)
                img.setPixel(2 * w - 1 - x, y, paletteColor(c));
        }
    }
    return img;
}

inline Def remap(Def d, const char *id, const char *label, std::initializer_list<std::pair<char, char>> swaps) {
    d.id = id;
    d.label = label;
    for (auto &row : d.rows)
        for (char &c : row)
            for (const auto &s : swaps)
                if (c == s.first) {
                    c = s.second;
                    break;
                }
    return d;
}

// Onigiri makes Mochimaru wear a red hachimaki: the forehead row turns red.
inline Def powered(Def d, const char *id, const char *label) {
    d.id = id;
    d.label = label;
    std::string &row = d.rows[6];
    for (size_t x = 1; x + 1 < row.size(); ++x)
        row[x] = 'R';
    return d;
}

inline const std::vector<Def> &all() {
    static const std::vector<Def> defs = [] {
        const char *E = kBlank;
        std::vector<Def> v;

        // ---- Mochimaru (もちまる): a little mochi bunny, the hero. -------------------------------------------
        const char *hero = "Mochimaru  -  hero, a little mochi bunny";
        const Rows headTop = Rows{}("...KKK....KKK...")("..KWWWK..KWWWK..")("..KWPWK..KWPWK..")(
            "..KWPWK..KWPWK..")("..KWWWKKKKWWWK..")(".KWWWWWWWWWWWWK.")("KWWWWWWWWWWWWWWK");
        auto body = [&](const char *eyes1, const char *eyes2, const char *eyes3, const char *mouth, const char *f1,
                        const char *f2) {
            Rows r = headTop;
            r(eyes1)(eyes2)(eyes3)(mouth)(".KSWWWWWWWWWWSK.")("..KSSWWWWWWSSK..")("...KKKKKKKKKK...")(f1)(f2);
            return r.v;
        };
        const char *eyeA = "KWWEHWWWWWWEHWWK", *eyeB = "KWWEEWWWWWWEEWWK";
        const char *mouth = "KWPPPWWKKWWPPPWK";
        v.push_back({"hero_idle", hero, "idle", false,
                     body(eyeA, eyeB, eyeB, mouth, "...KPPK..KPPK...", "...KKKK..KKKK...")});
        v.push_back({"hero_walk1", hero, "walk 1", false,
                     body(eyeA, eyeB, eyeB, mouth, "..KPPK....KPPK..", "..KKKK....KKKK..")});
        v.push_back({"hero_walk2", hero, "walk 2", false,
                     body(eyeA, eyeB, eyeB, mouth, "....KPPKKPPK....", "....KKKKKKKK....")});
        v.push_back({"hero_jump", hero, "jump", false,
                     body(eyeA, eyeB, eyeB, "KWPPPWWppWWPPPWK", ".KPPK......KPPK.", ".KKKK......KKKK.")});
        v.push_back({"hero_hurt", hero, "oops", false,
                     body("KWWEWWWWWWWWEWWK", "KWWWEEWWWWEEWWWK", "KWWEWWWWWWWWEWWK", mouth, "..KPPK....KPPK..",
                          "..KKKK....KKKK..")});
        v.push_back(powered(v[0], "hero_power", "onigiri power"));

        // ---- Poyo (ぽよ): a bouncy pink slime with a sprout. --------------------------------------------------
        const char *poyo = "Poyo  -  bouncy pink slime";
        v.push_back({"poyo1", poyo, "bounce 1", false,
                     Rows{}(E, 2)(".......GG.......")("......GGK.......")(".....KKKKKK.....")("...KKPPPPPPKK...")(
                         "..KPPPPPPPPPPK..")(".KPPPPPPPPPPPPK.")(".KPPEHPPPPEHPPK.")(".KPPEEPPPPEEPPK.")(
                         ".KPppPPKKPPppPK.")(".KPPPPPPPPPPPPK.")(".KSPPPPPPPPPPSK.")("..KSSPPPPPPSSK..")(
                         "...KKKKKKKKKK...")(E)
                         .v});
        v.push_back({"poyo2", poyo, "bounce 2", false,
                     Rows{}(E, 5)(".......GG.......")("......GGK.......")("...KKKKKKKKKK...")("..KPPPPPPPPPPK..")(
                         ".KPPPPPPPPPPPPK.")("KPPEHPPPPPPEHPPK")("KPPEEPPPPPPEEPPK")("KPppPPPKKPPPppPK")(
                         "KSPPPPPPPPPPPPSK")(".KSSPPPPPPPPSSK.")("..KKKKKKKKKKKK..")
                         .v});
        v.push_back({"poyo_flat", poyo, "squished", false,
                     Rows{}(E, 11)("..KKKKKKKKKKKK..")(".KPPPPPPPPPPPPK.")("KPPEEPPPPPPEEPPK")("KPppPPPPPPPPppPK")(
                         ".KKKKKKKKKKKKKK.")
                         .v});

        // ---- Kurimaru (くりまる): a grumpy-cute chestnut. -----------------------------------------------------
        const char *kuri = "Kurimaru  -  little chestnut";
        const Rows kuriTop = Rows{}(E)(".......KK.......")("......KBBK......")(".....KBBBBK.....")(
            "....KBBbBBBK....")("...KBBBBBBBBK...")("..KBBbBBBBbBBK..")("..KBBBBBBBBBBK..")(".KTTTTTTTTTTTTK.")(
            ".KTTEHTTTTEHTTK.")(".KTTEETTTTEETTK.")(".KTppTTKKTTppTK.")("..KTTTTTTTTTTK..")("...KKKKKKKKKK...");
        auto kuriFrame = [&](const char *f1, const char *f2) {
            Rows r = kuriTop;
            r(f1)(f2);
            return r.v;
        };
        v.push_back({"kuri1", kuri, "walk 1", false, kuriFrame("..KbbbK..KbbbK..", "..KKKKK..KKKKK..")});
        v.push_back({"kuri2", kuri, "walk 2", false, kuriFrame("..KbbbK....KbbbK", "..KKKKK....KKKKK")});
        v.push_back({"kuri_flat", kuri, "squished", false,
                     Rows{}(E, 11)("...KKKKKKKKKK...")("..KBBBBBBBBBBK..")(".KTTEETTTTEETTK.")(".KTTppTTTTppTTK.")(
                         "..KKKKKKKKKKKK..")
                         .v});

        // ---- Kamekichi (かめきち): a turtle; stomp it and it hides in its shell. ---------------------------------
        const char *kame = "Kamekichi  -  turtle with a shell";
        const Rows kameTop = Rows{}(E, 2)("....KKKKKKKK....")("...KGGLLLLGGK...")("..KGLLGGGGLLGK..")(
            "..KGGGGLLGGGGK..")(".KKKKKKKKKKKKKK.")(".KTTTTTTTTTTTTK.")(".KTTEHTTTTEHTTK.")(".KTTEETTTTEETTK.")(
            ".KTppTTKKTTppTK.")("..KTTTTTTTTTTK..")("...KKKKKKKKKK...");
        auto kameFrame = [&](const char *f1, const char *f2) {
            Rows r = kameTop;
            r(f1)(f2)(E);
            return r.v;
        };
        v.push_back({"kame1", kame, "walk 1", false, kameFrame("..KGGK....KGGK..", "..KKKK....KKKK..")});
        v.push_back({"kame2", kame, "walk 2", false, kameFrame("...KGGK..KGGK...", "...KKKK..KKKK...")});
        v.push_back({"kame_shell", kame, "shell", false,
                     Rows{}(E, 6)("....KKKKKKKK....")("...KGGLLLLGGK...")("..KGLLGGGGLLGK..")(".KGGGGLLLLGGGGK.")(
                         ".KGLLGGGGGGLLGK.")(".KTTTTTTTTTTTTK.")("..KTTETTTTETTK..")("...KKKKKKKKKK...")(E, 2)
                         .v});

        // ---- Fuwari (ふわり): a cloud ghost that floats and cannot be stomped. -----------------------------------
        const char *fuwa = "Fuwari  -  floating cloud ghost";
        const Rows fuwaMid = Rows{}("KWWWEHWWWWEHWWWK")("KWWWEEWWWWEEWWWK")("KWWPPWWKKWWPPWWK")(
            ".KWWWWWWWWWWWWK.")("..KSWWWWWWWWSK..")("..KWWKSSSSKWWK..");
        auto fuwaFrame = [&](const char *p1, const char *p2, const char *tail) {
            Rows r;
            r(E)(p1)(p2)("..KWWWWWWWWWWK..")(".KWWWWWWWWWWWWK.");
            for (const auto &row : fuwaMid.v)
                r(row.c_str());
            r(tail)(E, 4);
            return r.v;
        };
        v.push_back({"fuwa1", fuwa, "float 1", false,
                     fuwaFrame("....KKK..KKK....", "...KWWWKKWWWK...", "...KKK.KK.KKK...")});
        v.push_back({"fuwa2", fuwa, "float 2", false,
                     fuwaFrame("...KKK....KKK...", "..KWWWKKKKWWWK..", "..KKK..KK..KKK..")});

        // ---- Fuku-chan (ふくちゃん): a lucky cat who waits at the end of each stage. -----------------------------
        const char *fuku = "Fuku-chan  -  lucky cat at the goal";
        const Rows fukuBody = Rows{}("KWWWWYYWWWWKWWWK")("KWWWWYyWWWWWKKKK")("KWWWWWWWWWWWWK..")(
            ".KSWWWWWWWWWSK..")("..KKKKKKKKKKK...")(E, 2);
        v.push_back({"fuku1", fuku, "wave 1", false,
                     [&] {
                         Rows r = Rows{}("..KK....KK......")(".KWWK..KWWK.....")(".KWPKKKKPWK.KKKK")(
                             ".KWWWWWWWWK.KWWK")(".KWEHWWEHWK.KPPK")(".KWEEWWEEWK.KPPK")(".KWPPKKPPWK.KWWK")(
                             ".KWWWWWWWWK.KWWK")(".KRRRRRRRRKKKWWK");
                         for (const auto &row : fukuBody.v)
                             r(row.c_str());
                         return r.v;
                     }()});
        v.push_back({"fuku2", fuku, "wave 2", false,
                     [&] {
                         Rows r = Rows{}("..KK....KK......")(".KWWK..KWWK.....")(".KWPKKKKPWK.....")(
                             ".KWWWWWWWWK.KKKK")(".KWEHWWEHWK.KWWK")(".KWEEWWEEWK.KPPK")(".KWPPKKPPWK.KPPK")(
                             ".KWWWWWWWWK.KWWK")(".KRRRRRRRRKKKWWK");
                         for (const auto &row : fukuBody.v)
                             r(row.c_str());
                         return r.v;
                     }()});

        // ---- Items --------------------------------------------------------------------------------------------
        const char *items = "Items  -  koban coin, onigiri power-up, kirari star";
        v.push_back({"koban", items, "koban", false,
                     Rows{}(E)(".....KKKKKK.....")("...KKYYYYYYKK...")("..KYYWWYYYYYyK..")(".KYYWYYYYYYYyyK.")(
                         ".KYYYYKKKKYYyyK.")(".KYYYYKyyKYYyyK.", 2)(".KYYYYKKKKYYyyK.")(".KYYYYYYYYYYyyK.")(
                         ".KYYYYYYYYYyyyK.")("..KYYYYYYYyyyK..")("...KKyyyyyyKK...")(".....KKKKKK.....")(E, 2)
                         .v});
        v.push_back({"onigiri", items, "onigiri", false,
                     Rows{}(E)(".......KK.......")("......KWWK......")(".....KWWWWK.....")("....KWWHWWWK....")(
                         "...KWWHWWWWWK...")("..KWWWWWWWWWWK..")(".KWWWEHWWEHWWWK.")(".KWWWEEWWEEWWWK.")(
                         ".KWWPPWKKWPPWWK.")(".KWWWWWWWWWWWWK.")(".KDDDDDDDDDDDDK.", 2)("..KKKKKKKKKKKK..")(E, 2)
                         .v});
        v.push_back({"kirari", items, "kirari", false,
                     Rows{}(".......KK.......")("......KYYK......", 2)(".....KYYYYK.....", 2)("KKKKKKYYYYKKKKKK")(
                         "KYYYYYYYYYYYYYYK")(".KYYYYYYYYYYYYK.")("..KYEHYYYYEHYK..")("..KYEEYYYYEEYK..")(
                         "..KYppYKKYppYK..")("...KYYYYYYYYK...")("..KYYYKKKKYYYK..")("..KYYK....KYYK..")(
                         "..KKK......KKK..")(E)
                         .v});

        // ---- Blocks -------------------------------------------------------------------------------------------
        const char *blocks = "Blocks  -  grass, soil, kawara brick, hatena block, used block, stone";
        const Rows soil = Rows{}("BBBBBBBBBBBBBBBB")("BBBBBbBBBBBBBBBB")("BbBBBBBBBBBBbBBB")("BBBBBBBBBBBBBBBB")(
            "BBBBBBBBBbBBBBBB")("BBBBBBBBBBBBBBBB")("BBBbBBBBBBBBBbBB")("BBBBBBBBBBBBBBBB")("BBBBBBbBBBBBBBBB")(
            "BbBBBBBBBBBBbBBB")("BBBBBBBBBBBBBBBB")("BBBBBBBBBbBBBBBB")("BBBBBBBBBBBBBBBB")("BBbBBBBBBBBBBBbB")(
            "BBBBBBBBBBBBBBBB", 2);
        v.push_back({"grass", blocks, "grass", false,
                     Rows{}("LLLLLLLLLLLLLLLL")("LLLGLLLLLLGLLLLL")("GGGGGGGGGGGGGGGG")("gGGGgGGGgGGGgGGG")(
                         "BBGBBBBBBGGBBBBB")("BBBBBBBBBBBBBBBB")("BBBbBBBBBBBBBbBB")("BBBBBBBBBBBBBBBB")(
                         "BBBBBBbBBBBBBBBB")("BbBBBBBBBBBBbBBB")("BBBBBBBBBBBBBBBB")("BBBBBBBBBbBBBBBB")(
                         "BBBBBBBBBBBBBBBB")("BBbBBBBBBBBBBBbB")("BBBBBBBBBBBBBBBB", 2)
                         .v});
        v.push_back({"soil", blocks, "soil", false, soil.v});
        v.push_back({"brick", blocks, "brick", false,
                     Rows{}("KKKKKKKKKKKKKKKK")("PWWPPPPKPWWPPPPK")("PPPPPPPKPPPPPPPK")("pppppppKpppppppK")(
                         "KKKKKKKKKKKKKKKK")("PPPKPWWPPPPKPWWP")("PPPKPPPPPPPKPPPP")("pppKpppppppKpppp")(
                         "KKKKKKKKKKKKKKKK")("PWWPPPPKPWWPPPPK")("PPPPPPPKPPPPPPPK")("pppppppKpppppppK")(
                         "KKKKKKKKKKKKKKKK")("PPPKPWWPPPPKPWWP")("PPPKPPPPPPPKPPPP")("pppKpppppppKpppp")
                         .v});
        v.push_back({"hatena", blocks, "hatena", false,
                     Rows{}("KKKKKKKKKKKKKKKK")("KYWYYYYYYYYYYWYK")("KYYYYYYYYYYYYYYK")("KYYYYKKKKKKYYYYK")(
                         "KYYYKKYYYYKKYYYK")("KYYYYYYYYYKKYYYK")("KYYYYYYYKKKYYYYK")("KYYYYYYKKYYYYYYK", 2)(
                         "KYYYYYYYYYYYYYYK")("KYYYYYYKKYYYYYYK", 2)("KYYYYYYYYYYYYYYK")("KYyYYYYYYYYYYyYK")(
                         "KyyyyyyyyyyyyyyK")("KKKKKKKKKKKKKKKK")
                         .v});
        v.push_back({"used", blocks, "used", false,
                     Rows{}("KKKKKKKKKKKKKKKK")("KNnNNNNNNNNNNnNK")("KNNNNNNNNNNNNNNK", 11)("KNnNNNNNNNNNNnNK")(
                         "KnnnnnnnnnnnnnnK")("KKKKKKKKKKKKKKKK")
                         .v});
        v.push_back({"stone", blocks, "stone", false,
                     Rows{}("KKKKKKKKKKKKKKKK")("KWWWWWWWWWWWWWNK")("KWNNNNNNNNNNNnNK", 12)("KNnnnnnnnnnnnnnK")(
                         "KKKKKKKKKKKKKKKK")
                         .v});

        // ---- Take-no-tsutsu (たけのつつ): a bamboo pipe, two tiles wide. -----------------------------------------
        const char *bamboo = "Take-no-tsutsu  -  bamboo pipe";
        v.push_back({"bamboo_top", bamboo, "top", true,
                     Rows{}("KKKKKKKKKKKKKKKK")("KgGGGGGGGGGLLLLL", 5)("KggggggggggGGGGG")("KKKKKKKKKKKKKKKK")(
                         ".KgGGGGGGGGLLLLL", 3)(".KggggggggggGGGG")(".KgGGGGGGGGLLLLL", 4)
                         .v});
        v.push_back({"bamboo_body", bamboo, "body", true,
                     Rows{}(".KgGGGGGGGGLLLLL", 3)(".KggggggggggGGGG")(".KgGGGGGGGGLLLLL", 12).v});

        // ---- Scenery ------------------------------------------------------------------------------------------
        const char *scenery = "Scenery  -  Kumo-chan cloud, Shigemi bush, sakura bush, torii goal gate";
        v.push_back({"cloud", scenery, "kumo-chan", true,
                     Rows{}(E)("..........kkkkkk")(".........kWWWWWW")("...kkkk.kWWWWWWW")("..kWWWWkWWWWWWWW")(
                         ".kWWWWWWWWWWWWWW")("kWWWWWWWWWWWWWWW")("kWWWWWWWWEHWWWWW")("kWWWWWWWWEEWWWWW")(
                         "kWWWWWPPWWWWWWWp")("kWWWWWWWWWWWWWWW")(".kWWWWWWWWWWWWWW")("..kQQWWWWWWWWWWW")(
                         "...kkQQQQQQQQQQQ")(".....kkkkkkkkkkk")(E)
                         .v});
        const Def bush{"bush", scenery, "shigemi", false,
                       Rows{}(E, 5)(".....KKKKKK.....")("...KKGGLLGGKK...")("..KGGLLGGGGGGGK.")(".KGGGGGGGGGGGGGK")(
                           "KGGGEHGGGGEHGGGK")("KGGGEEGGGGEEGGGK")("KGGpGGGKKGGGpGGK")("KGGGGGGGGGGGGGGK")(
                           ".KgGGGGGGGGGGgK.")("..KKggggggggKK..")(E)
                           .v};
        v.push_back(bush);
        v.push_back(remap(bush, "bush_sakura", "sakura", {{'G', 'P'}, {'g', 'p'}, {'L', 'W'}}));
        v.push_back({"torii", scenery, "torii", false,
                     Rows{}("KK............KK")("KRKKKKKKKKKKKKRK")(".KRRRRRRRRRRRRK.")("..KKKKKKKKKKKK..")(
                         "....KRK..KRK....", 2)("...KRRRRRRRRK...")("...KKKKKKKKKK...")("....KRK..KRK....", 6)(
                         "...KRRK..KRRK...")("...KKKK..KKKK...")
                         .v});
        return v;
    }();
    return defs;
}

// A sprite by id; a null image when there is none.
inline QImage image(const std::string &id) {
    for (const Def &d : all())
        if (d.id == id)
            return render(d);
    return QImage();
}

} // namespace Sprites
