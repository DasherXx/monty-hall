// =====================================================================
//  PARADOKS MONTY HALLA - symulacja graficzna (raylib)
// =====================================================================
//  Kompilacja (okno MSYS2 UCRT64):
//    g++ monty_hall_gui.cpp -o monty_hall_gui.exe -std=c++17 -O2 -mwindows -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm
//  Obok pliku .exe musza lezec biblioteki DLL:
//    cp /ucrt64/bin/{libraylib,glfw3,libstdc++-6,libgcc_s_seh-1,libwinpthread-1}.dll .
//
//  Czcionki sa wbudowane w ten plik (na samym dole), wiec nie trzeba nic wiecej kopiowac.
// =====================================================================

#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace std;

extern const char FONT_REG_B64[];
extern const char FONT_BOLD_B64[];
extern const char FONT_TITLE_B64[];

// ------------------------------------------------------------------
//  Uklad okna i sceny
// ------------------------------------------------------------------
static const int   WIN_W = 1240, WIN_H = 750;
static const float ST_X = 10, ST_Y = 10, ST_W = 840, ST_H = 500;       // scena
static const float FLOOR_Y = 400;                                       // linia podlogi
static const float DOOR_W = 156, DOOR_H = 250, DOOR_GAP = 46, DOOR_X0 = 84, DOOR_Y = 150;
static const float PRES_X = 742, PRES_Y = 446;                          // pozycja prowadzacego (stopy)

static Rectangle DoorRect(int i) {
    return Rectangle{DOOR_X0 + i * (DOOR_W + DOOR_GAP), DOOR_Y, DOOR_W, DOOR_H};
}

// ------------------------------------------------------------------
//  Czas, wejscie (z haczykami do testow)
// ------------------------------------------------------------------
static double gTime = 0.0;
static float  gDt = 1.0f / 60.0f;
static bool   gHand = false;  // czy pokazac kursor "raczka"

// skalowanie calego interfejsu do rozmiaru okna (litery - proporcje 1240x750 zachowane)
static float gScale = 1.0f;
static Vector2 gOff = {0, 0};

static bool gBlock = false;   // nakladka wyjasnienia przechwytuje myszke
static bool gUseRaw = false;  // rysowanie nakladki: uzywaj prawdziwej myszki
#ifdef MH_TEST
static Vector2 gMouse = {0, 0};
static bool gClick = false;
static Vector2 RawMouse() { return gMouse; }
static bool RawClick() { return gClick; }
static bool RawDown() { return false; }
#else
static Vector2 RawMouse() {
    Vector2 m = GetMousePosition();
    return Vector2{(m.x - gOff.x) / gScale, (m.y - gOff.y) / gScale};
}
static bool RawClick() { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
static bool RawDown() { return IsMouseButtonDown(MOUSE_BUTTON_LEFT); }
#endif
static Vector2 MousePos() { return (gBlock && !gUseRaw) ? Vector2{-9999.0f, -9999.0f} : RawMouse(); }
static bool MouseClick() { return (gBlock && !gUseRaw) ? false : RawClick(); }
static bool MouseDown() { return (gBlock && !gUseRaw) ? false : RawDown(); }

// ------------------------------------------------------------------
//  Pomocnicze: matematyka, kolory
// ------------------------------------------------------------------
static const float PI_F = 3.14159265358979f;
static inline float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int ClampI(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }

static inline Color RGBA(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)ClampI(r, 0, 255), (unsigned char)ClampI(g, 0, 255),
                 (unsigned char)ClampI(b, 0, 255), (unsigned char)ClampI(a, 0, 255)};
}
static inline Color Al(Color c, float a) {
    c.a = (unsigned char)(Clamp01(a * c.a / 255.0f) * 255.0f + 0.5f);
    return c;
}
static inline Color Mix(Color a, Color b, float t) {
    t = Clamp01(t);
    return RGBA((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t),
                (int)(a.a + (b.a - a.a) * t));
}
static inline Color Shade(Color c, float k) { return RGBA((int)(c.r * k), (int)(c.g * k), (int)(c.b * k), c.a); }

static const Color C_BLACK = {0, 0, 0, 255}, C_WHITE = {255, 255, 255, 255};
static const Color C_BG = {13, 15, 30, 255}, C_CARD = {26, 30, 56, 255}, C_LINE = {58, 66, 108, 255};
static const Color C_TEXT = {238, 240, 252, 255}, C_MUTED = {146, 154, 188, 255};
static const Color C_GOLD = {255, 205, 80, 255}, C_GREEN = {72, 206, 128, 255}, C_ORANGE = {255, 150, 60, 255};
static const Color C_CYAN = {86, 208, 236, 255}, C_RED = {238, 92, 96, 255}, C_BLUE = {64, 98, 190, 255};

// ------------------------------------------------------------------
//  Easing
// ------------------------------------------------------------------
static float EaseOutCubic(float t) { t = Clamp01(t); return 1.0f - powf(1.0f - t, 3.0f); }
static float EaseInOutCubic(float t) {
    t = Clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
static float EaseOutBack(float t) {
    t = Clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

// ------------------------------------------------------------------
//  Trojkaty i wielokaty (raylib wymaga okreslonej kolejnosci wierzcholkow)
// ------------------------------------------------------------------
static void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cr > 0) swap(b, c);
    DrawTriangle(a, b, c, col);
}
static void FillPoly(const vector<Vector2>& p, Color col) {  // wielokat wypukly
    for (size_t i = 1; i + 1 < p.size(); ++i) Tri(p[0], p[i], p[i + 1], col);
}

// ------------------------------------------------------------------
//  Czcionki (wbudowane, base64) - kazdy rozmiar ladowany osobno, zeby tekst byl ostry
// ------------------------------------------------------------------
enum { FS_REG = 0, FS_BOLD = 1, FS_TITLE = 2 };
static vector<unsigned char> gFontData[3];
static map<pair<int, int>, Font> gFontCache;

static vector<unsigned char> DecodeB64(const char* s) {
    vector<unsigned char> out;
    int val = 0, bits = -8;
    for (; *s; ++s) {
        int c = *s, d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else continue;
        val = (val << 6) + d;
        bits += 6;
        if (bits >= 0) {
            out.push_back((unsigned char)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static void InitFonts() {
    gFontData[FS_REG] = DecodeB64(FONT_REG_B64);
    gFontData[FS_BOLD] = DecodeB64(FONT_BOLD_B64);
    gFontData[FS_TITLE] = DecodeB64(FONT_TITLE_B64);
}

static const Font& GetFont2(int style, int size) {
    pair<int, int> key(style, size);
    auto it = gFontCache.find(key);
    if (it != gFontCache.end()) return it->second;
    static vector<int> cps;
    if (cps.empty()) {
        for (int c = 32; c < 127; ++c) cps.push_back(c);
        const int extra[] = {0x0104, 0x0105, 0x0106, 0x0107, 0x0118, 0x0119, 0x0141, 0x0142, 0x0143, 0x0144,
                             0x00D3, 0x00F3, 0x015A, 0x015B, 0x0179, 0x017A, 0x017B, 0x017C, 0x00D7, 0x2022,
                             0x2026, 0x2014, 0x2013, 0x201E, 0x201D, 0x201C, 0x2019, 0x00B0};
        for (int c : extra) cps.push_back(c);
    }
    Font f = LoadFontFromMemory(".ttf", gFontData[style].data(), (int)gFontData[style].size(), size, cps.data(),
                                (int)cps.size());
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return gFontCache.emplace(key, f).first->second;
}

static inline float SpaceW(int size) { return size * 0.32f; }
static inline float LetterSp(int size) { return size * 0.02f; }

// Tekst rysowany slowo po slowie, zeby odstepy miedzy slowami byly czytelne
static Vector2 TextSize(int st, const string& s, int size) {
    const Font& f = GetFont2(st, size);
    float w = 0;
    string word;
    auto flush = [&]() {
        if (!word.empty()) {
            w += MeasureTextEx(f, word.c_str(), (float)size, LetterSp(size)).x;
            word.clear();
        }
    };
    for (char c : s) {
        if (c == ' ') { flush(); w += SpaceW(size); }
        else word.push_back(c);
    }
    flush();
    return Vector2{w, (float)size};
}
static void Txt(int st, const string& s, float x, float y, int size, Color c) {
    const Font& f = GetFont2(st, size);
    float cx = roundf(x), cy = roundf(y);
    string word;
    auto flush = [&]() {
        if (!word.empty()) {
            DrawTextEx(f, word.c_str(), Vector2{roundf(cx), cy}, (float)size, LetterSp(size), c);
            cx += MeasureTextEx(f, word.c_str(), (float)size, LetterSp(size)).x;
            word.clear();
        }
    };
    for (char ch : s) {
        if (ch == ' ') { flush(); cx += SpaceW(size); }
        else word.push_back(ch);
    }
    flush();
}
static void TxtC(int st, const string& s, float cx, float y, int size, Color c) {
    Txt(st, s, cx - TextSize(st, s, size).x / 2.0f, y, size, c);
}
static void TxtR(int st, const string& s, float rx, float y, int size, Color c) {
    Txt(st, s, rx - TextSize(st, s, size).x, y, size, c);
}
static int FitSize(int st, const string& s, int size, float maxW) {
    while (size > 9 && TextSize(st, s, size).x > maxW) --size;
    return size;
}
static void TxtFit(int st, const string& s, float x, float y, int size, float maxW, Color c) {
    Txt(st, s, x, y, FitSize(st, s, size, maxW), c);
}
static void TxtShadow(int st, const string& s, float cx, float y, int size, Color c, Color sh) {
    TxtC(st, s, cx + 2, y + 3, size, sh);
    TxtC(st, s, cx, y, size, c);
}

static vector<string> Wrap(int st, const string& s, int size, float maxW) {
    vector<string> lines;
    string cur, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        string t = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && TextSize(st, t, size).x > maxW) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = t;
        }
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flushWord();
        else word.push_back(ch);
    }
    flushWord();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

static int Utf8Len(const string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}
static string Utf8Prefix(const string& s, int n) {
    int cnt = 0;
    size_t i = 0;
    while (i < s.size() && cnt < n) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : ((c >> 5) == 6 ? 2 : ((c >> 4) == 14 ? 3 : 4));
        i += len;
        ++cnt;
    }
    return s.substr(0, min(i, s.size()));
}

static string FmtInt(long long v) {
    string s = to_string(v), out;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out.push_back(s[i]);
        if (++c % 3 == 0 && i > 0) out.push_back(' ');
    }
    reverse(out.begin(), out.end());
    return out;
}
static string FmtPct(double p, int dec = 1) {
    char b[32];
    snprintf(b, sizeof(b), "%.*f", dec, p);
    string s = b;
    for (char& c : s)
        if (c == '.') c = ',';
    return s + "%";
}

// ------------------------------------------------------------------
//  Szum i tekstura drewna
// ------------------------------------------------------------------
static float Hash2(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFF) / 65535.0f;
}
static float VNoise(float x, float y, unsigned seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = Hash2(xi, yi, seed), b = Hash2(xi + 1, yi, seed), c = Hash2(xi, yi + 1, seed),
          d = Hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float Fbm(float x, float y, unsigned seed) {
    float s = 0, a = 0.5f;
    for (int o = 0; o < 4; ++o) {
        s += a * VNoise(x, y, seed + o);
        x *= 2;
        y *= 2;
        a *= 0.5f;
    }
    return s;
}

// Tekstura skrzydla drzwi: drewno z usek i dwoma wypuklymi panelami
static Texture2D MakeDoorTexture(int w, int h, unsigned seed) {
    Image img = GenImageColor(w, h, Color{0, 0, 0, 255});
    Color* px = (Color*)img.data;
    const Color dark = {88, 48, 22, 255}, light = {182, 120, 64, 255};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float n = Fbm(x * 0.020f, y * 0.0045f, seed);
            float rings = sinf(x * 0.17f + n * 17.0f);
            float v = 0.5f + 0.5f * rings;
            v = v * v * 0.55f + n * 0.55f;
            v += (Hash2(x, y, seed + 9) - 0.5f) * 0.07f;
            Color c = Mix(dark, light, Clamp01(v));
            float edge = 1.0f - 0.22f * (float)x / w;  // lewa strona jasniejsza
            px[y * w + x] = Shade(c, edge);
        }
    }
    auto panel = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                int dl = x - x0, dr = x1 - 1 - x, dt = y - y0, db = y1 - 1 - y;
                int d = min(min(dl, dr), min(dt, db));
                Color c = px[y * w + x];
                if (d < 5) {
                    bool lightSide = min(dl, dt) < min(dr, db);
                    c = lightSide ? Shade(c, 1.30f - 0.04f * d) : Shade(c, 0.62f + 0.04f * d);
                } else {
                    c = Shade(c, 0.93f);
                }
                if (d == 0) c = Shade(c, 0.55f);
                px[y * w + x] = c;
            }
        }
    };
    panel(14, 16, w - 14, 106);
    panel(14, 122, w - 14, h - 16);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// ------------------------------------------------------------------
//  Efekty swietlne
// ------------------------------------------------------------------
static void Glow(float x, float y, float r, Color c, float intensity) {
    BeginBlendMode(BLEND_ADDITIVE);
    const int N = 9;
    for (int i = 0; i < N; ++i) {
        float k = (float)i / N;
        DrawCircleV(Vector2{x, y}, r * (1.0f - k * 0.9f), Al(c, intensity / N * 1.6f));
    }
    EndBlendMode();
}
static void Sparkle(float x, float y, float s, Color c) {
    FillPoly({Vector2{x - s, y}, Vector2{x, y - s * 0.16f}, Vector2{x + s, y}, Vector2{x, y + s * 0.16f}}, c);
    FillPoly({Vector2{x, y - s}, Vector2{x + s * 0.16f, y}, Vector2{x, y + s}, Vector2{x - s * 0.16f, y}}, c);
}

// ------------------------------------------------------------------
//  Logika gry
// ------------------------------------------------------------------
static mt19937 gen;
static int los3() { return uniform_int_distribution<int>(0, 2)(gen); }

struct Gra {
    int nagroda, wybor, otwarte, zmieniony;
    bool zostajWygrana, zmianaWygrana;
};

static void UstawProwadzacego(Gra& g) {
    do {
        g.otwarte = los3();
    } while (g.otwarte == g.wybor || g.otwarte == g.nagroda);
    g.zmieniony = 3 - g.wybor - g.otwarte;
    g.zostajWygrana = (g.wybor == g.nagroda);
    g.zmianaWygrana = (g.zmieniony == g.nagroda);
}
static Gra ZagrajLosowo() {
    Gra g;
    g.nagroda = los3();
    g.wybor = los3();
    UstawProwadzacego(g);
    return g;
}

// ------------------------------------------------------------------
//  Statystyki
// ------------------------------------------------------------------
struct Punkt { double n, ps, pm; };
struct Rekord { bool stayWin, switchWin; int decyzja; };  // decyzja: 0 brak, 1 zostaje, 2 zmienia
struct Wpis { int nr; bool reczny; int nagroda, wybor, otwarte, decyzja; bool stayWin, switchWin; };

static long long total = 0, wStay = 0, wSwitch = 0;
static vector<Punkt> hist;
static double nextRecord = 1;
static int pStN = 0, pStW = 0, pSwN = 0, pSwW = 0;  // wyniki gracza w trybie recznym
static deque<Rekord> gLast;                         // ostatnie gry (do kropek)
static vector<Wpis> gLog;                           // dziennik gier z animacja
static long long gFastN = 0, gFastStay = 0, gFastSwitch = 0;

static void DodajWynik(const Gra& g, int decyzja) {
    ++total;
    if (g.zostajWygrana) ++wStay;
    if (g.zmianaWygrana) ++wSwitch;
    if (total <= 100 || (double)total >= nextRecord) {
        hist.push_back({(double)total, 100.0 * wStay / total, 100.0 * wSwitch / total});
        nextRecord = max((double)total + 1.0, (double)total * 1.03);
    }
    gLast.push_back({g.zostajWygrana, g.zmianaWygrana, decyzja});
    if (gLast.size() > 24) gLast.pop_front();
}
static void ResetujWyniki() {
    total = wStay = wSwitch = 0;
    pStN = pStW = pSwN = pSwW = 0;
    gFastN = gFastStay = gFastSwitch = 0;
    hist.clear();
    gLast.clear();
    gLog.clear();
    nextRecord = 1;
}

// ------------------------------------------------------------------
//  Konfetti
// ------------------------------------------------------------------
struct Konf { Vector2 p, v; float rot, vr, size, life, phase; Color c; };
static vector<Konf> gKonf;
static mt19937 gAnim(777);
static float RandF(float a, float b) { return uniform_real_distribution<float>(a, b)(gAnim); }

static void Wystrzel(int n, const vector<Color>& pal) {
    for (int i = 0; i < n; ++i) {
        Konf k;
        int typ = i % 3;
        if (typ == 0) {  // dzialo z lewej
            k.p = Vector2{ST_X + 14, FLOOR_Y + 60};
            float a = RandF(-80.f, -35.f) * PI_F / 180.0f, sp = RandF(420.f, 900.f);
            k.v = Vector2{cosf(a) * sp, sinf(a) * sp};
        } else if (typ == 1) {  // dzialo z prawej
            k.p = Vector2{ST_X + ST_W - 14, FLOOR_Y + 60};
            float a = RandF(-145.f, -100.f) * PI_F / 180.0f, sp = RandF(420.f, 900.f);
            k.v = Vector2{cosf(a) * sp, sinf(a) * sp};
        } else {  // deszcz z gory
            k.p = Vector2{RandF(ST_X + 20, ST_X + ST_W - 20), ST_Y + 20};
            k.v = Vector2{RandF(-60.f, 60.f), RandF(40.f, 220.f)};
        }
        k.rot = RandF(0, 360);
        k.vr = RandF(-540, 540);
        k.size = RandF(7, 13);
        k.life = RandF(2.6f, 4.2f);
        k.phase = RandF(0, 6.28f);
        k.c = pal[(size_t)(RandF(0, (float)pal.size() - 0.01f))];
        gKonf.push_back(k);
    }
    if (gKonf.size() > 900) gKonf.erase(gKonf.begin(), gKonf.begin() + (gKonf.size() - 900));
}
static void UpdateKonfetti(float dt) {
    for (Konf& k : gKonf) {
        k.v.y += 640.0f * dt;
        k.v.x *= (1.0f - 0.7f * dt);
        k.v.y *= (1.0f - 0.45f * dt);
        k.p.x += k.v.x * dt;
        k.p.y += k.v.y * dt;
        k.rot += k.vr * dt;
        k.life -= dt;
    }
    gKonf.erase(remove_if(gKonf.begin(), gKonf.end(),
                          [](const Konf& k) {
                              return k.life <= 0 || k.p.y > ST_Y + ST_H - 6 || k.p.x < ST_X || k.p.x > ST_X + ST_W;
                          }),
                gKonf.end());
}
static void DrawKonfetti() {
    for (const Konf& k : gKonf) {
        float flut = fabsf(sinf((float)gTime * 9.0f + k.phase));
        float w = k.size, h = k.size * (0.18f + 0.55f * flut);
        Color c = Al(k.c, Clamp01(k.life / 0.6f));
        DrawRectanglePro(Rectangle{k.p.x, k.p.y, w, h}, Vector2{w / 2, h / 2}, k.rot, c);
    }
}

// ------------------------------------------------------------------
//  Elementy interfejsu: karty, przyciski, plakietki
// ------------------------------------------------------------------
static Vector2 gShake = {0, 0};

static void ScissorOn(Rectangle r) {
    float x = (r.x + gShake.x) * gScale + gOff.x, y = (r.y + gShake.y) * gScale + gOff.y;
    BeginScissorMode((int)floorf(x), (int)floorf(y), (int)ceilf(r.width * gScale) + 1, (int)ceilf(r.height * gScale) + 1);
}

static void Card(Rectangle r) {
    DrawRectangleRounded(Rectangle{r.x + 2, r.y + 5, r.width, r.height}, 0.06f, 10, Al(C_BLACK, 0.35f));
    DrawRectangleRounded(Rectangle{r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 0.06f, 10, Al(C_LINE, 0.9f));
    DrawRectangleRounded(r, 0.06f, 10, C_CARD);
}

static map<string, float> gBtnAnim;

static bool Button(const string& id, Rectangle r, const string& label, Color base, bool enabled = true,
                   int fs = 17) {
    Vector2 m = MousePos();
    bool hover = enabled && CheckCollisionPointRec(m, r);
    float& a = gBtnAnim[id];
    a += ((hover ? 1.0f : 0.0f) - a) * min(1.0f, gDt * 14.0f);
    if (hover) gHand = true;
    float off = (hover && MouseDown()) ? 2.0f : 0.0f;
    Color c = enabled ? Mix(base, Shade(base, 1.30f), a) : RGBA(52, 56, 78);
    DrawRectangleRounded(Rectangle{r.x + 1, r.y + 5, r.width, r.height}, 0.30f, 10, Al(C_BLACK, 0.40f));
    Rectangle rr = {r.x, r.y + off, r.width, r.height};
    DrawRectangleRounded(rr, 0.30f, 10, Shade(c, 0.72f));
    DrawRectangleRounded(Rectangle{rr.x, rr.y, rr.width, rr.height - 3}, 0.30f, 10, c);
    DrawRectangleRounded(Rectangle{rr.x + 4, rr.y + 3, rr.width - 8, (rr.height - 3) * 0.42f}, 0.5f, 8,
                         Al(C_WHITE, enabled ? 0.10f + 0.07f * a : 0.04f));
    Color tc = enabled ? C_TEXT : RGBA(130, 134, 156);
    int f = FitSize(FS_BOLD, label, fs, r.width - 20);
    Vector2 ts = TextSize(FS_BOLD, label, f);
    Txt(FS_BOLD, label, rr.x + (rr.width - ts.x) / 2.0f, rr.y + (rr.height - 3 - ts.y) / 2.0f, f, tc);
    return enabled && hover && MouseClick();
}

static void Plaque(float cx, float y, const string& text, Color bg, Color fg) {
    int fs = 14;
    Vector2 ts = TextSize(FS_BOLD, text, fs);
    float w = ts.x + 26, h = 24;
    Rectangle r = {cx - w / 2.0f, y, w, h};
    DrawRectangleRounded(Rectangle{r.x, r.y + 3, r.width, r.height}, 0.5f, 8, Al(C_BLACK, 0.45f));
    DrawRectangleRounded(r, 0.5f, 8, Shade(bg, 0.7f));
    DrawRectangleRounded(Rectangle{r.x, r.y, r.width, r.height - 2}, 0.5f, 8, bg);
    Txt(FS_BOLD, text, r.x + 13, r.y + (h - ts.y) / 2.0f - 1, fs, fg);
}

// ------------------------------------------------------------------
//  Samochod
// ------------------------------------------------------------------
static void DrawCar(float cx, float cy, float s) {
    auto P = [&](float x, float y) { return Vector2{cx + x * s, cy + y * s}; };
    auto R = [&](float x, float y, float w, float h) { return Rectangle{cx + x * s, cy + y * s, w * s, h * s}; };
    Color body = {214, 38, 52, 255}, bodyHi = {255, 96, 104, 255}, bodyLo = {132, 18, 34, 255};

    DrawEllipse((int)cx, (int)cy, (int)(64 * s), (int)(6 * s), Al(C_BLACK, 0.45f));

    // spojler
    DrawRectangleRounded(R(-64, -58, 24, 5), 0.5f, 4, bodyLo);
    DrawRectangle((int)(cx - 56 * s), (int)(cy - 54 * s), (int)(3 * s), (int)(14 * s), bodyLo);

    // nadwozie dolne
    DrawRectangleRounded(R(-64, -40, 128, 30), 0.55f, 10, bodyLo);
    DrawRectangleRounded(R(-64, -42, 128, 28), 0.55f, 10, body);
    DrawRectangleRounded(R(-58, -40, 116, 8), 0.8f, 8, Al(bodyHi, 0.55f));

    // kabina
    FillPoly({P(-34, -40), P(-20, -63), P(24, -63), P(46, -40)}, body);
    FillPoly({P(-18, -63), P(22, -63), P(20, -59), P(-16, -59)}, Al(bodyHi, 0.65f));

    // szyby
    Color glass = {96, 164, 214, 255};
    FillPoly({P(-29, -42), P(-18, -59), P(-2, -59), P(-2, -42)}, glass);
    FillPoly({P(2, -42), P(2, -59), P(22, -59), P(39, -42)}, glass);
    FillPoly({P(-25, -45), P(-17, -57), P(-12, -57), P(-20, -45)}, Al(C_WHITE, 0.40f));
    FillPoly({P(6, -45), P(6, -57), P(11, -57), P(11, -45)}, Al(C_WHITE, 0.30f));

    // drzwi, klamka, pas
    DrawLineEx(P(0, -59), P(0, -16), 1.6f * s, Al(bodyLo, 0.9f));
    DrawRectangleRounded(R(7, -36, 11, 3), 0.5f, 4, Al(C_WHITE, 0.7f));
    DrawRectangleRounded(R(-58, -24, 116, 3), 0.5f, 4, Al(C_WHITE, 0.55f));

    // kola
    for (int k = 0; k < 2; ++k) {
        float wx = k == 0 ? -38.0f : 40.0f;
        Vector2 c = P(wx, -12);
        DrawCircleV(c, 17 * s, Color{40, 10, 16, 255});
        DrawCircleV(c, 13 * s, Color{20, 20, 24, 255});
        DrawCircleV(c, 8.5f * s, Color{205, 210, 222, 255});
        for (int sp = 0; sp < 5; ++sp) {
            float a = (float)(gTime * 3.0 + sp * 1.2566);
            DrawLineEx(c, Vector2{c.x + cosf(a) * 8 * s, c.y + sinf(a) * 8 * s}, 1.6f * s, Color{120, 126, 140, 255});
        }
        DrawCircleV(c, 3.2f * s, Color{90, 96, 110, 255});
    }

    // swiatla i zderzaki
    DrawRectangleRounded(R(56, -33, 10, 10), 0.5f, 4, Color{255, 236, 150, 255});
    DrawRectangleRounded(R(-66, -34, 8, 10), 0.5f, 4, Color{255, 60, 60, 255});
    DrawRectangleRounded(R(58, -20, 9, 5), 0.5f, 4, Color{205, 210, 222, 255});
    Glow(cx + 64 * s, cy - 28 * s, 30 * s, Color{255, 230, 140, 255}, 0.55f);
}

// ------------------------------------------------------------------
//  Koza
// ------------------------------------------------------------------
static void DrawGoat(float cx, float cy, float s, float seed) {
    auto P = [&](float x, float y) { return Vector2{cx + x * s, cy + y * s}; };
    auto R = [&](float x, float y, float w, float h) { return Rectangle{cx + x * s, cy + y * s, w * s, h * s}; };
    Color wool = {250, 248, 242, 255}, woolS = {218, 216, 210, 255}, hoof = {60, 52, 54, 255},
          horn = {214, 188, 138, 255}, pink = {238, 170, 168, 255};
    float bob = sinf((float)gTime * 2.4f + seed) * 1.5f;
    float tail = sinf((float)gTime * 7.0f + seed) * 5.0f;
    bool blink = fmodf((float)gTime + seed * 1.7f, 3.4f) < 0.13f;

    DrawEllipse((int)cx, (int)cy, (int)(52 * s), (int)(6 * s), Al(C_BLACK, 0.45f));

    // nogi (dalsze ciemniejsze)
    auto leg = [&](float x, Color c) {
        DrawRectangleRounded(R(x - 4.5f, -40, 9, 38), 0.4f, 4, c);
        DrawRectangleRounded(R(x - 5.0f, -9, 10, 9), 0.3f, 4, hoof);
    };
    leg(-22, woolS);
    leg(24, woolS);
    leg(-32, wool);
    leg(34, wool);

    // tulow
    DrawEllipse((int)(cx), (int)(cy - 47 * s), (int)(48 * s), (int)(27 * s), woolS);
    DrawEllipse((int)(cx), (int)(cy - 52 * s), (int)(48 * s), (int)(27 * s), wool);
    for (int k = 0; k < 7; ++k)  // kosmki welny na grzbiecie
        DrawCircleV(P(-36 + k * 12.0f, -73 + (k % 2) * 2.0f), 8.5f * s, wool);
    DrawEllipse((int)(cx - 6 * s), (int)(cy - 40 * s), (int)(28 * s), (int)(8 * s), Al(woolS, 0.55f));

    // ogon
    Tri(P(-44, -60), P(-58, -68 + tail), P(-45, -50), wool);

    // szyja
    DrawLineEx(P(26, -62), P(40, -84 + bob), 22 * s, wool);

    // broda
    float hx = 46, hy = -88 + bob;
    Tri(P(hx + 10, hy + 10), P(hx + 20, hy + 10), P(hx + 14, hy + 26), woolS);

    // glowa
    DrawEllipse((int)(cx + hx * s), (int)(cy + hy * s), (int)(18 * s), (int)(15 * s), wool);
    DrawEllipse((int)(cx + (hx + 12) * s), (int)(cy + (hy + 6) * s), (int)(12 * s), (int)(9 * s), Color{242, 230, 216, 255});
    DrawCircleV(P(hx + 21, hy + 4), 3.2f * s, pink);
    DrawLineEx(P(hx + 14, hy + 12), P(hx + 22, hy + 11), 1.4f * s, Color{150, 100, 100, 255});

    // ucho
    FillPoly({P(hx - 10, hy - 7), P(hx - 28, hy - 12), P(hx - 25, hy - 2), P(hx - 9, hy - 1)}, woolS);
    FillPoly({P(hx - 12, hy - 6), P(hx - 24, hy - 9), P(hx - 22, hy - 3), P(hx - 11, hy - 2)}, pink);

    // rogi
    DrawLineEx(P(hx - 1, hy - 12), P(hx - 7, hy - 30), 5.5f * s, horn);
    DrawLineEx(P(hx - 7, hy - 30), P(hx - 16, hy - 35), 4.2f * s, horn);
    DrawLineEx(P(hx + 8, hy - 12), P(hx + 10, hy - 30), 5.5f * s, horn);
    DrawLineEx(P(hx + 10, hy - 30), P(hx + 2, hy - 36), 4.2f * s, horn);

    // oko
    if (blink) {
        DrawLineEx(P(hx + 4, hy - 4), P(hx + 11, hy - 4), 2.0f * s, C_BLACK);
    } else {
        DrawCircleV(P(hx + 8, hy - 4), 4.6f * s, C_WHITE);
        DrawEllipse((int)(cx + (hx + 9) * s), (int)(cy + (hy - 4) * s), (int)(2.0f * s + 0.5f), (int)(3.2f * s + 0.5f),
                    C_BLACK);
    }
}

// ------------------------------------------------------------------
//  Wnetrze za drzwiami
// ------------------------------------------------------------------
static void DrawRoom(Rectangle in, int content, float open, float pop, float leafW) {
    DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)in.height, Color{50, 52, 112, 255},
                           Color{22, 22, 54, 255});
    float fy = in.y + in.height - 50;
    DrawRectangleGradientV((int)in.x, (int)fy, (int)in.width, 50, Color{70, 62, 104, 255}, Color{30, 26, 58, 255});
    DrawRectangle((int)in.x, (int)fy, (int)in.width, 2, Al(C_WHITE, 0.12f));
    float cx = in.x + leafW + (in.width - leafW) / 2.0f, base = in.y + in.height - 16;

    if (content == 2) {  // AUTO: promienie, poswiata, iskry
        float ccx = cx, ccy = base - 40;
        BeginBlendMode(BLEND_ADDITIVE);
        for (int k = 0; k < 12; ++k) {
            float a0 = (float)((k * 30.0 + gTime * 18.0) * PI_F / 180.0), da = 7.0f * PI_F / 180.0f;
            Tri(Vector2{ccx, ccy}, Vector2{ccx + cosf(a0 - da) * 260, ccy + sinf(a0 - da) * 260},
                Vector2{ccx + cosf(a0 + da) * 260, ccy + sinf(a0 + da) * 260}, Al(RGBA(255, 214, 90), 0.13f * pop));
        }
        EndBlendMode();
        Glow(ccx, ccy, 90, Color{255, 200, 80, 255}, 0.55f * pop);
        DrawCar(cx, base, 0.86f * pop);
        for (int k = 0; k < 5; ++k) {
            float ph = (float)gTime * 1.6f + k * 1.3f;
            float tw = Clamp01(sinf(ph) * 0.5f + 0.5f);
            Sparkle(in.x + 20 + fmodf(k * 37.0f + 13.0f, in.width - 40), in.y + 40 + fmodf(k * 53.0f, 120.0f),
                    3.0f + 6.0f * tw, Al(RGBA(255, 240, 180), pop * tw));
        }
    } else {  // KOZA
        Glow(cx, base - 50, 70, Color{200, 210, 255, 255}, 0.28f * pop);
        DrawGoat(cx - 6, base, 0.88f * pop, cx);
    }
    // gdy drzwi dopiero sie uchylaja - polmrok
    DrawRectangle((int)in.x, (int)in.y, (int)in.width, (int)in.height, Al(C_BLACK, (1.0f - open) * 0.55f));
}

// ------------------------------------------------------------------
//  Drzwi
// ------------------------------------------------------------------
static void DrawDoor(int idx, const Texture2D& tex, float openRaw, int content, float hover, float selected,
                     float chosen) {
    Rectangle D = DoorRect(idx);
    Rectangle in = {D.x + 10, D.y + 10, D.width - 20, D.height - 10};
    float open = EaseInOutCubic(openRaw);

    // odbicie w podlodze
    DrawRectangleGradientV((int)D.x, (int)(FLOOR_Y + 2), (int)D.width, 64, Al(RGBA(120, 80, 50), 0.22f),
                           Al(RGBA(120, 80, 50), 0.0f));

    // cien pod drzwiami
    DrawEllipse((int)(D.x + D.width / 2), (int)(FLOOR_Y + 3), (int)(D.width * 0.62f), 7, Al(C_BLACK, 0.45f));

    // gzyms (nadproze)
    Color frame = {72, 44, 26, 255};
    DrawRectangleRounded(Rectangle{D.x - 9, D.y - 15, D.width + 18, 17}, 0.35f, 6, Color{104, 66, 38, 255});
    DrawRectangle((int)(D.x - 9), (int)(D.y - 13), (int)(D.width + 18), 2, Al(C_WHITE, 0.2f));
    DrawRectangleRounded(Rectangle{D.x - 4, D.y - 7, D.width + 8, 9}, 0.4f, 6, Color{130, 84, 48, 255});

    // rama
    DrawRectangleGradientH((int)D.x, (int)D.y, (int)D.width, (int)D.height, Shade(frame, 1.35f), Shade(frame, 0.8f));
    DrawRectangleLinesEx(D, 2, Shade(frame, 0.55f));
    DrawRectangleLinesEx(Rectangle{D.x + 4, D.y + 4, D.width - 8, D.height - 4}, 1.5f, Al(C_WHITE, 0.14f));

    // kat otwarcia skrzydla (obraca sie na zawiasach po lewej stronie)
    float th = open * 1.45f, cs = cosf(th), sn = sinf(th);
    float wp = in.width * cs, k = 0.17f * sn;

    // wnetrze
    if (open > 0.004f) {
        ScissorOn(in);
        float reveal = Clamp01((open - 0.35f) / 0.65f);
        DrawRoom(in, content, open, EaseOutBack(reveal), wp);
        EndScissorMode();
    } else {
        DrawRectangleRec(in, Color{10, 10, 18, 255});
    }

    // skrzydlo
    if (open <= 0.002f) {
        DrawTexturePro(tex, Rectangle{0, 0, (float)tex.width, (float)tex.height}, in, Vector2{0, 0}, 0.0f, C_WHITE);
        // delikatny polysk
        DrawRectangleGradientH((int)in.x, (int)in.y, (int)in.width, (int)in.height, Al(C_WHITE, 0.06f + 0.10f * hover),
                               Al(C_BLACK, 0.0f));
    } else {
        int cols = (int)ceilf(wp);
        for (int c = 0; c < cols; ++c) {
            float u = (c + 0.5f) / wp;
            float hf = 1.0f - k * u;
            float dh = in.height * hf;
            float dy = in.y + (in.height - dh) * 0.5f;
            float sh = 1.0f - 0.45f * open - 0.25f * open * u;
            Color tint = RGBA((int)(255 * sh), (int)(255 * sh), (int)(255 * sh));
            float sw = (float)tex.width / wp;
            float sx = min((float)c / wp * tex.width, (float)tex.width - sw);
            DrawTexturePro(tex, Rectangle{sx, 0, sw, (float)tex.height}, Rectangle{in.x + c, dy, 1.7f, dh},
                           Vector2{0, 0}, 0.0f, tint);
        }
        // krawedz skrzydla
        float hfEnd = 1.0f - k;
        DrawLineEx(Vector2{in.x + wp, in.y + (in.height - in.height * hfEnd) * 0.5f},
                   Vector2{in.x + wp, in.y + (in.height + in.height * hfEnd) * 0.5f}, 2.0f, Al(C_BLACK, 0.5f));
    }

    // okucia: zawiasy, tabliczka z numerem i klamka
    auto Map = [&](float u, float v) {
        float hf = 1.0f - k * u;
        return Vector2{in.x + wp * u, in.y + in.height * 0.5f + (v - 0.5f) * in.height * hf};
    };
    for (float v : {0.13f, 0.5f, 0.87f}) {
        Vector2 p = Map(0.0f, v);
        DrawRectangleRounded(Rectangle{p.x - 1, p.y - 6, 5, 12}, 0.4f, 4, Color{190, 150, 60, 255});
    }
    float hf = 1.0f - k * 0.88f;
    Vector2 hp = Map(0.88f, 0.55f);
    DrawEllipse((int)hp.x, (int)hp.y, (int)max(2.0f, 8 * cs), (int)(8 * hf), Color{120, 84, 20, 255});
    DrawEllipse((int)hp.x, (int)hp.y, (int)max(1.0f, 6 * cs), (int)(6 * hf), Color{236, 196, 90, 255});
    DrawEllipse((int)(hp.x - 1), (int)(hp.y - 2), (int)max(1.0f, 2 * cs), 2, Al(C_WHITE, 0.8f));

    if (open < 0.16f) {
        float fade = 1.0f - open / 0.16f;
        Vector2 pp = Map(0.5f, 0.245f);
        DrawCircleV(pp, 24, Al(Color{90, 60, 16, 255}, fade));
        DrawCircleV(pp, 22, Al(Color{222, 178, 76, 255}, fade));
        DrawCircleV(Vector2{pp.x - 6, pp.y - 8}, 8, Al(C_WHITE, 0.10f * fade));
        DrawCircleV(pp, 17, Al(Color{120, 80, 22, 255}, fade));
        TxtC(FS_BOLD, to_string(idx + 1), pp.x, pp.y - 15, 30, Al(Color{255, 232, 160, 255}, fade));
    }

    // podswietlenia: najechanie myszka (cyjan) i wybor (zlote)
    if (hover > 0.01f) {
        for (int i = 3; i >= 1; --i)
            DrawRectangleLinesEx(Rectangle{D.x - i * 2.5f, D.y - i * 2.5f, D.width + i * 5.0f, D.height + i * 5.0f}, 2.5f,
                                 Al(C_CYAN, hover * (0.55f - i * 0.12f)));
    }
    if (selected > 0.01f) {
        float pulse = 0.65f + 0.35f * sinf((float)gTime * 4.0f);
        for (int i = 4; i >= 1; --i)
            DrawRectangleLinesEx(Rectangle{D.x - i * 2.5f, D.y - i * 2.5f, D.width + i * 5.0f, D.height + i * 5.0f}, 2.5f,
                                 Al(C_GOLD, selected * pulse * (0.85f - i * 0.15f)));
        DrawRectangleLinesEx(Rectangle{D.x - 2, D.y - 2, D.width + 4, D.height + 4}, 3.0f, Al(C_GOLD, selected * (0.75f + 0.25f * pulse)));
    }
    (void)chosen;
}

// ------------------------------------------------------------------
//  Tlo sceny: sciana, podloga, kurtyny, reflektory, szyld
// ------------------------------------------------------------------
static void DrawWallAndFloor() {
    float xL = ST_X, xR = ST_X + ST_W;
    // sciana
    DrawRectangleGradientV((int)xL, (int)ST_Y, (int)ST_W, (int)(FLOOR_Y - ST_Y), Color{48, 28, 92, 255},
                           Color{24, 14, 50, 255});
    // tapeta w romby
    ScissorOn(Rectangle{xL, ST_Y, ST_W, FLOOR_Y - ST_Y});
    for (float x = xL - 400; x < xR + 40; x += 62) {
        DrawLineEx(Vector2{x, FLOOR_Y}, Vector2{x + 400, FLOOR_Y - 400}, 1.0f, Al(C_WHITE, 0.035f));
        DrawLineEx(Vector2{x, FLOOR_Y - 400}, Vector2{x + 400, FLOOR_Y}, 1.0f, Al(C_WHITE, 0.035f));
    }
    EndScissorMode();
    // winieta
    DrawRectangleGradientH((int)xL, (int)ST_Y, 220, (int)(FLOOR_Y - ST_Y), Al(C_BLACK, 0.55f), Al(C_BLACK, 0.0f));
    DrawRectangleGradientH((int)(xR - 220), (int)ST_Y, 220, (int)(FLOOR_Y - ST_Y), Al(C_BLACK, 0.0f), Al(C_BLACK, 0.55f));

    // podloga
    float fh = ST_Y + ST_H - FLOOR_Y;
    DrawRectangleGradientV((int)xL, (int)FLOOR_Y, (int)ST_W, (int)fh, Color{66, 40, 36, 255}, Color{22, 13, 18, 255});
    ScissorOn(Rectangle{xL, FLOOR_Y, ST_W, fh});
    Vector2 V = {430, 230};
    float ratio = (ST_Y + ST_H - V.y) / (FLOOR_Y - V.y);
    for (int i = -16; i <= 16; ++i) {
        float x = 430 + i * 64.0f;
        float x2 = V.x + (x - V.x) * ratio;
        DrawLineEx(Vector2{x, FLOOR_Y}, Vector2{x2, ST_Y + ST_H}, 1.2f, Al(C_BLACK, 0.32f));
    }
    for (float y : {412.0f, 428.0f, 448.0f, 474.0f, 506.0f})
        DrawLineEx(Vector2{xL, y}, Vector2{xR, y}, 1.0f, Al(C_BLACK, 0.22f));
    EndScissorMode();
    DrawRectangleGradientV((int)xL, (int)FLOOR_Y, (int)ST_W, 22, Al(C_WHITE, 0.09f), Al(C_WHITE, 0.0f));
    // listwa przypodlogowa
    DrawRectangle((int)xL, (int)(FLOOR_Y - 7), (int)ST_W, 7, Color{40, 24, 28, 255});
    DrawRectangle((int)xL, (int)(FLOOR_Y - 2), (int)ST_W, 2, Al(C_GOLD, 0.55f));
}

static void DrawCurtains() {
    const float cw = 58, bottom = FLOOR_Y + 4;
    for (int side = 0; side < 2; ++side) {
        float x0 = side == 0 ? ST_X : ST_X + ST_W - cw;
        int n = 14;
        for (int i = 0; i < n; ++i) {
            float w = cw / n;
            float f = 0.5f + 0.5f * sinf(i * 1.35f + (float)gTime * 0.45f + side * 2.0f);
            Color c = Mix(Color{96, 10, 24, 255}, Color{186, 28, 46, 255}, f);
            DrawRectangleGradientV((int)(x0 + i * w), (int)ST_Y, (int)ceilf(w) + 1, (int)(bottom - ST_Y), c, Shade(c, 0.62f));
        }
        DrawRectangle((int)x0, (int)(bottom - 5), (int)cw, 5, Color{210, 170, 70, 255});
        // wewnetrzny cien
        float shx = side == 0 ? x0 + cw : x0 - 18;
        DrawRectangleGradientH((int)shx, (int)ST_Y, 18, (int)(bottom - ST_Y), Al(C_BLACK, side == 0 ? 0.35f : 0.0f),
                               Al(C_BLACK, side == 0 ? 0.0f : 0.35f));
    }
    // lambrekin z girlandami
    DrawRectangleGradientV((int)ST_X, (int)ST_Y, (int)ST_W, 24, Color{150, 20, 38, 255}, Color{104, 12, 28, 255});
    for (int k = 0; k < 16; ++k) {
        float cx = ST_X + 26 + k * 52.0f;
        DrawEllipse((int)cx, (int)(ST_Y + 22), 27, 20, (k % 2) ? Color{126, 14, 32, 255} : Color{150, 20, 38, 255});
        DrawCircleV(Vector2{cx, ST_Y + 41}, 3.2f, Color{236, 196, 90, 255});
    }
    DrawRectangle((int)ST_X, (int)(ST_Y + 22), (int)ST_W, 2, Color{236, 196, 90, 255});
}

static void DrawMarquee() {
    float cx = DOOR_X0 + (3 * DOOR_W + 2 * DOOR_GAP) / 2.0f;
    Rectangle M = {cx - 215, 64, 430, 64};
    DrawRectangleRounded(Rectangle{M.x + 3, M.y + 6, M.width, M.height}, 0.25f, 10, Al(C_BLACK, 0.5f));
    DrawRectangleRounded(Rectangle{M.x - 3, M.y - 3, M.width + 6, M.height + 6}, 0.25f, 10, Color{150, 108, 30, 255});
    DrawRectangleRounded(M, 0.25f, 10, Color{26, 12, 44, 255});
    DrawRectangleGradientV((int)(M.x + 8), (int)(M.y + 6), (int)(M.width - 16), (int)(M.height * 0.45f),
                           Al(C_WHITE, 0.07f), Al(C_WHITE, 0.0f));
    TxtShadow(FS_TITLE, "MONTY HALL SHOW", cx, M.y + 6, 31, C_GOLD, Color{110, 60, 10, 255});
    TxtC(FS_BOLD, "ZOSTAĆ CZY ZMIENIĆ?", cx, M.y + 43, 12, Color{255, 214, 140, 255});

    // zarowki
    int phase = (int)(gTime * 2.2);
    vector<Vector2> pts;
    float inset = 9;
    for (float x = M.x + inset; x <= M.x + M.width - inset + 0.1f; x += 22) pts.push_back(Vector2{x, M.y + 5});
    for (float y = M.y + inset + 8; y <= M.y + M.height - inset - 4; y += 18) pts.push_back(Vector2{M.x + M.width - 5, y});
    for (float x = M.x + M.width - inset; x >= M.x + inset - 0.1f; x -= 22) pts.push_back(Vector2{x, M.y + M.height - 5});
    for (float y = M.y + M.height - inset - 8; y >= M.y + inset + 4; y -= 18) pts.push_back(Vector2{M.x + 5, y});
    for (size_t i = 0; i < pts.size(); ++i) {
        bool on = ((int)i + phase) % 2 == 0;
        DrawCircleV(pts[i], 4.0f, on ? Color{255, 240, 170, 255} : Color{96, 70, 40, 255});
        if (on) Glow(pts[i].x, pts[i].y, 13, Color{255, 220, 120, 255}, 0.55f);
    }
}

static void DrawSpot(float cx, float intensity) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (int layer = 0; layer < 3; ++layer) {
        float wTop = 16 + layer * 8, wBot = 150 + layer * 52;
        float a = intensity * (layer == 0 ? 0.085f : 0.040f);
        for (float y = 140; y < FLOOR_Y; y += 5) {
            float t = (y - 140) / (FLOOR_Y - 140);
            float w = wTop + (wBot - wTop) * t;
            DrawRectangle((int)(cx - w / 2), (int)y, (int)w, 5, Al(RGBA(255, 236, 180), a * (1.0f - t * 0.55f)));
        }
    }
    for (int i = 0; i < 6; ++i)
        DrawEllipse((int)cx, (int)(FLOOR_Y + 18), 118 - i * 15, 24 - i * 3, Al(RGBA(255, 230, 160), intensity * 0.05f));
    EndBlendMode();
}
static void DrawSpotFixture(float cx) {
    DrawRectangleRounded(Rectangle{cx - 16, 134, 32, 14}, 0.4f, 6, Color{18, 18, 26, 255});
    DrawEllipse((int)cx, 148, 11, 4, Color{255, 240, 190, 255});
    Glow(cx, 148, 22, Color{255, 230, 160, 255}, 0.7f);
}

// ------------------------------------------------------------------
//  Prowadzacy
// ------------------------------------------------------------------
static void DrawPresenter(float x, float y, Vector2 dir, float mouth, bool blink, bool cheer) {
    float bob = sinf((float)gTime * 2.1f) * 1.4f;
    Color suit = {26, 28, 46, 255}, suit2 = {52, 56, 86, 255}, skin = {244, 204, 166, 255}, skinD = {214, 168, 132, 255};

    DrawEllipse((int)x, (int)y, 36, 7, Al(C_BLACK, 0.5f));
    // nogi i buty
    DrawRectangleRounded(Rectangle{x - 18, y - 72, 15, 66}, 0.3f, 6, suit);
    DrawRectangleRounded(Rectangle{x + 3, y - 72, 15, 66}, 0.3f, 6, suit);
    DrawEllipse((int)(x - 10), (int)(y - 4), 14, 5, Color{14, 14, 20, 255});
    DrawEllipse((int)(x + 11), (int)(y - 4), 14, 5, Color{14, 14, 20, 255});

    float ty = y - 134 + bob;
    // reka z mikrofonem (po prawej)
    Vector2 sR = {x + 27, ty + 11}, eR = {x + 42, ty + 40}, hR = {x + 31, ty + 20};
    DrawLineEx(sR, eR, 13, suit);
    DrawLineEx(eR, hR, 12, suit);
    // tulow
    DrawRectangleRounded(Rectangle{x - 32, ty, 64, 76}, 0.28f, 8, suit);
    DrawRectangleRounded(Rectangle{x - 32, ty, 20, 76}, 0.28f, 8, Al(suit2, 0.5f));
    Tri(Vector2{x - 11, ty}, Vector2{x + 11, ty}, Vector2{x, ty + 36}, Color{246, 246, 252, 255});
    Tri(Vector2{x - 11, ty}, Vector2{x - 24, ty + 40}, Vector2{x - 1, ty + 36}, suit2);
    Tri(Vector2{x + 11, ty}, Vector2{x + 1, ty + 36}, Vector2{x + 24, ty + 40}, suit2);
    Tri(Vector2{x - 10, ty + 2}, Vector2{x - 10, ty + 14}, Vector2{x - 1, ty + 8}, Color{210, 40, 56, 255});
    Tri(Vector2{x + 10, ty + 2}, Vector2{x + 1, ty + 8}, Vector2{x + 10, ty + 14}, Color{210, 40, 56, 255});
    DrawCircleV(Vector2{x, ty + 8}, 3.4f, Color{150, 20, 36, 255});
    DrawCircleV(Vector2{x, ty + 48}, 2.2f, Color{200, 170, 80, 255});
    DrawCircleV(Vector2{x, ty + 60}, 2.2f, Color{200, 170, 80, 255});
    // mikrofon
    DrawRectangleRounded(Rectangle{hR.x - 3, hR.y - 22, 6, 24}, 0.5f, 4, Color{40, 40, 52, 255});
    DrawCircleV(Vector2{hR.x, hR.y - 26}, 6.5f, Color{70, 74, 90, 255});
    DrawCircleV(Vector2{hR.x - 2, hR.y - 28}, 2.2f, Al(C_WHITE, 0.4f));
    DrawCircleV(hR, 6.5f, skin);

    // reka wskazujaca
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    Vector2 d = len > 0.001f ? Vector2{dir.x / len, dir.y / len} : Vector2{-1, 0};
    Vector2 sL = {x - 27, ty + 11};
    Vector2 hL = {sL.x + d.x * 54, sL.y + d.y * 54};
    DrawLineEx(sL, hL, 14, suit);
    DrawCircleV(Vector2{hL.x - d.x * 7, hL.y - d.y * 7}, 7.5f, Color{246, 246, 252, 255});
    DrawCircleV(hL, 7.0f, skin);
    DrawLineEx(hL, Vector2{hL.x + d.x * 9, hL.y + d.y * 9}, 4.5f, skin);
    DrawCircleV(sL, 7, suit);

    // szyja i glowa
    float hy = ty - 23;
    DrawRectangle((int)(x - 6), (int)(ty - 8), 12, 10, skinD);
    DrawCircleV(Vector2{x - 21, hy + 3}, 4.5f, skinD);
    DrawCircleV(Vector2{x + 21, hy + 3}, 4.5f, skinD);
    DrawCircleV(Vector2{x, hy}, 22, skin);
    DrawCircleSector(Vector2{x, hy - 1}, 23, 180, 360, 24, Color{54, 36, 28, 255});
    DrawEllipse((int)(x - 12), (int)(hy - 10), 13, 6, Color{54, 36, 28, 255});
    // oczy
    float gx = d.x > 0 ? 1.4f : -1.4f;
    for (int e = -1; e <= 1; e += 2) {
        float ex = x + e * 8.5f, ey = hy + 1;
        if (blink) {
            DrawLineEx(Vector2{ex - 4, ey}, Vector2{ex + 4, ey}, 2, Color{50, 30, 24, 255});
        } else {
            DrawCircleV(Vector2{ex, ey}, 4.8f, C_WHITE);
            DrawCircleV(Vector2{ex + gx, ey + 0.5f}, 2.5f, Color{40, 28, 24, 255});
        }
        DrawLineEx(Vector2{ex - 5, ey - 7}, Vector2{ex + 5, ey - 8 + (cheer ? -2.0f : 0.0f)}, 2.0f, Color{54, 36, 28, 255});
    }
    DrawEllipse((int)(x + 1), (int)(hy + 7), 2, 2, skinD);
    DrawCircleV(Vector2{x - 14, hy + 8}, 4, Al(C_RED, 0.22f));
    DrawCircleV(Vector2{x + 14, hy + 8}, 4, Al(C_RED, 0.22f));
    // usta
    Vector2 mc = {x, hy + 11};
    if (mouth > 0.15f || cheer) {
        float o = cheer ? 4.5f : 1.5f + 4.0f * mouth;
        DrawEllipse((int)mc.x, (int)(mc.y + 1), 6, (int)o, Color{120, 24, 40, 255});
        DrawEllipse((int)mc.x, (int)(mc.y + 1 + o * 0.5f), 4, (int)(o * 0.5f), Color{226, 100, 110, 255});
    } else {
        DrawRing(Vector2{mc.x, mc.y - 4}, 6.0f, 7.6f, 25, 155, 14, Color{150, 50, 60, 255});
    }
}

static void DrawBubble(const string& full, float shownChars, float bx, float bottom, float bw, float tailX) {
    const int fs = 15;
    vector<string> lines = Wrap(FS_REG, full, fs, bw - 26);
    float bh = lines.size() * 20.0f + 20;
    Rectangle r = {bx, bottom - bh, bw, bh};
    DrawRectangleRounded(Rectangle{r.x + 2, r.y + 5, r.width, r.height}, 0.2f, 10, Al(C_BLACK, 0.40f));
    DrawRectangleRounded(r, 0.2f, 10, Color{252, 252, 255, 255});
    Tri(Vector2{tailX - 12, bottom - 2}, Vector2{tailX + 10, bottom - 2}, Vector2{tailX + 4, bottom + 18},
        Color{252, 252, 255, 255});
    int remain = (int)shownChars;
    float y = r.y + 10;
    for (const string& ln : lines) {
        int n = Utf8Len(ln);
        string part = Utf8Prefix(ln, min(n, max(remain, 0)));
        Txt(FS_REG, part, r.x + 13, y, fs, Color{34, 30, 52, 255});
        remain -= n + 1;
        y += 20;
    }
}

// ------------------------------------------------------------------
//  Karty ze statystykami (prawa kolumna)
// ------------------------------------------------------------------
static double gDispTotal = 0;
static float gDispStay = 0, gDispSwitch = 0;

static void UpdateDisp(float dt) {
    double k = min(1.0, (double)dt * 9.0);
    gDispTotal += ((double)total - gDispTotal) * k;
    if (fabs((double)total - gDispTotal) < 0.6) gDispTotal = (double)total;
    float ts = total > 0 ? 100.0f * wStay / total : 0.0f, tm = total > 0 ? 100.0f * wSwitch / total : 0.0f;
    gDispStay += (ts - gDispStay) * min(1.0f, dt * 8.0f);
    gDispSwitch += (tm - gDispSwitch) * min(1.0f, dt * 8.0f);
}

static void DrawStatBar(Rectangle r, float y0, const string& name, Color col, long long wins, float pct,
                        double theory, const string& tag) {
    float x = r.x + 18, w = r.width - 36;
    Txt(FS_BOLD, name, x, y0, 15, C_TEXT);
    TxtR(FS_BOLD, FmtPct(pct, 1), r.x + r.width - 18, y0 - 5, 22, col);
    Rectangle track = {x, y0 + 28, w, 16};
    DrawRectangleRounded(track, 0.5f, 8, Color{16, 18, 36, 255});
    float fw = max(0.0f, pct / 100.0f * w);
    if (fw > 3) {
        float ww = max(fw, 12.0f);
        DrawRectangleRounded(Rectangle{x, track.y, ww, track.height}, 0.5f, 8, Shade(col, 0.75f));
        DrawRectangleRounded(Rectangle{x, track.y, ww, track.height - 3}, 0.5f, 8, col);
        DrawRectangleRounded(Rectangle{x + 3, track.y + 2, max(4.0f, ww - 6), 4}, 0.5f, 4, Al(C_WHITE, 0.30f));
    }
    float mx = x + (float)(theory / 100.0) * w;
    DrawRectangle((int)(mx - 1), (int)(track.y - 4), 2, (int)(track.height + 8), Al(C_WHITE, 0.85f));
    TxtC(FS_REG, tag, mx, y0 + 49, 11, C_MUTED);
    TxtR(FS_REG, "wygranych: " + FmtInt(wins), x + w, y0 + 49, 12, C_MUTED);
}

static void DrawStatsCard(Rectangle r) {
    Card(r);
    Txt(FS_BOLD, "WYNIKI SYMULACJI", r.x + 18, r.y + 14, 13, C_MUTED);
    string tot = FmtInt((long long)llround(gDispTotal));
    Txt(FS_TITLE, tot, r.x + 18, r.y + 32, 32, C_GOLD);
    float tw = TextSize(FS_TITLE, tot, 32).x;
    Txt(FS_REG, "gier", r.x + 18 + tw + 8, r.y + 48, 15, C_MUTED);

    DrawStatBar(r, r.y + 84, "ZOSTAJE przy wyborze", C_ORANGE, wStay, gDispStay, 100.0 / 3.0, "teoria 1/3");
    DrawStatBar(r, r.y + 158, "ZMIENIA drzwi", C_GREEN, wSwitch, gDispSwitch, 200.0 / 3.0, "teoria 2/3");

    string foot;
    Color fc = C_MUTED;
    if (wStay > 0 && total >= 10) {
        double ratio = (double)wSwitch / (double)wStay;
        char b[64];
        snprintf(b, sizeof(b), "%.2f", ratio);
        for (char* p = b; *p; ++p)
            if (*p == '.') *p = ',';
        foot = string("Zmiana wygrywa ") + b + "× częściej (teoria: 2×)";
        fc = C_TEXT;
    } else {
        foot = "Rozegraj kilka gier, aby zobaczyć przewagę zmiany.";
    }
    TxtFit(FS_REG, foot, r.x + 18, r.y + r.height - 28, 14, r.width - 36, fc);
}

static void DrawChart(Rectangle card) {
    Card(card);
    Txt(FS_BOLD, "ZBIEŻNOŚĆ DO TEORII", card.x + 18, card.y + 14, 13, C_MUTED);
    float px = card.x + 48, py = card.y + 44, pw = card.width - 48 - 20, ph = card.height - 44 - 82;
    Rectangle plot = {px, py, pw, ph};
    DrawRectangleRounded(plot, 0.02f, 6, Color{16, 18, 36, 255});

    for (int k = 0; k <= 4; ++k) {
        float yy = py + ph - ph * k / 4.0f;
        DrawLineEx(Vector2{px, yy}, Vector2{px + pw, yy}, 1.0f, Al(C_LINE, 0.55f));
        TxtR(FS_REG, to_string(k * 25) + "%", px - 8, yy - 8, 12, C_MUTED);
    }
    double maxLog = max(1.0, log10((double)max(total, 10LL)));
    for (int k = 0; k <= (int)maxLog; ++k) {
        float xx = px + (float)(k / maxLog) * pw;
        DrawLineEx(Vector2{xx, py}, Vector2{xx, py + ph}, 1.0f, Al(C_LINE, 0.45f));
        static const char* lab[] = {"1", "10", "100", "1k", "10k", "100k", "1M", "10M", "100M", "1G"};
        TxtC(FS_REG, lab[min(k, 9)], xx, py + ph + 6, 12, C_MUTED);
    }
    TxtC(FS_REG, "liczba gier (skala logarytmiczna)", px + pw / 2.0f, py + ph + 24, 12, C_MUTED);

    auto dashed = [&](double proc, Color c) {
        float yy = py + ph - (float)(proc / 100.0) * ph;
        for (float xx = px; xx < px + pw; xx += 12) DrawLineEx(Vector2{xx, yy}, Vector2{min(xx + 6, px + pw), yy}, 1.5f, c);
    };
    dashed(100.0 / 3.0, Al(C_ORANGE, 0.55f));
    dashed(200.0 / 3.0, Al(C_GREEN, 0.55f));

    auto series = [&](bool stay, Color c) {
        vector<Vector2> pts;
        for (const Punkt& p : hist) {
            float xx = px + (float)(log10(p.n) / maxLog) * pw;
            float yy = py + ph - (float)((stay ? p.ps : p.pm) / 100.0) * ph;
            pts.push_back(Vector2{xx, yy});
        }
        if (total > 0) {
            double pr = 100.0 * (stay ? wStay : wSwitch) / total;
            pts.push_back(Vector2{px + (float)(log10((double)total) / maxLog) * pw, py + ph - (float)(pr / 100.0) * ph});
        }
        for (size_t i = 1; i < pts.size(); ++i) DrawLineEx(pts[i - 1], pts[i], 6.0f, Al(c, 0.12f));
        for (size_t i = 1; i < pts.size(); ++i) DrawLineEx(pts[i - 1], pts[i], 2.4f, c);
        if (!pts.empty()) {
            DrawCircleV(pts.back(), 5.0f, Al(c, 0.35f));
            DrawCircleV(pts.back(), 3.2f, c);
        }
    };
    series(true, C_ORANGE);
    series(false, C_GREEN);

    if (total == 0) TxtC(FS_REG, "Rozegraj grę, aby zobaczyć wykres", px + pw / 2.0f, py + ph / 2.0f - 8, 14, C_MUTED);

    // legenda
    float ly = card.y + card.height - 26;
    DrawRectangleRounded(Rectangle{card.x + 18, ly + 5, 18, 4}, 0.5f, 4, C_ORANGE);
    Txt(FS_REG, "zostaje", card.x + 42, ly, 13, C_TEXT);
    DrawRectangleRounded(Rectangle{card.x + 112, ly + 5, 18, 4}, 0.5f, 4, C_GREEN);
    Txt(FS_REG, "zmienia", card.x + 136, ly, 13, C_TEXT);
    TxtR(FS_REG, "linie przerywane: teoria", card.x + card.width - 18, ly, 12, C_MUTED);

    // podpowiedz po najechaniu
    Vector2 m = MousePos();
    if (!hist.empty() && CheckCollisionPointRec(m, plot)) {
        double n = pow(10.0, (m.x - px) / pw * maxLog);
        size_t best = 0;
        double bd = 1e18;
        for (size_t i = 0; i < hist.size(); ++i) {
            double d = fabs(log10(hist[i].n) - log10(max(n, 1.0)));
            if (d < bd) { bd = d; best = i; }
        }
        const Punkt& p = hist[best];
        float xx = px + (float)(log10(p.n) / maxLog) * pw;
        DrawLineEx(Vector2{xx, py}, Vector2{xx, py + ph}, 1.2f, Al(C_WHITE, 0.5f));
        float ys = py + ph - (float)(p.ps / 100.0) * ph, ym = py + ph - (float)(p.pm / 100.0) * ph;
        DrawCircleV(Vector2{xx, ys}, 4, C_ORANGE);
        DrawCircleV(Vector2{xx, ym}, 4, C_GREEN);
        string l1 = "gier: " + FmtInt((long long)p.n), l2 = "zostaje " + FmtPct(p.ps), l3 = "zmienia " + FmtPct(p.pm);
        float bw = 120, bh = 62;
        float bx = xx + 10 + bw > px + pw ? xx - 10 - bw : xx + 10;
        float by = py + 8;
        DrawRectangleRounded(Rectangle{bx, by, bw, bh}, 0.15f, 8, Color{10, 12, 26, 235});
        Txt(FS_BOLD, l1, bx + 10, by + 6, 13, C_TEXT);
        Txt(FS_REG, l2, bx + 10, by + 24, 13, C_ORANGE);
        Txt(FS_REG, l3, bx + 10, by + 41, 13, C_GREEN);
    }
}

static void DrawHistoryCard(Rectangle r, bool reczny, bool* saveClicked) {
    Card(r);
    Txt(FS_BOLD, reczny ? "TWOJE WYNIKI" : "OSTATNIE 24 GRY", r.x + 18, r.y + 14, 13, C_MUTED);
    float y = r.y + 38;
    {
        auto row = [&](const string& name, Color col, int n, int w) {
            Txt(FS_BOLD, name, r.x + 18, y, 14, C_TEXT);
            Rectangle track = {r.x + 108, y + 5, 120, 10};
            DrawRectangleRounded(track, 0.5f, 6, Color{16, 18, 36, 255});
            float fw = n > 0 ? 120.0f * w / n : 0.0f;
            if (fw > 3) DrawRectangleRounded(Rectangle{track.x, track.y, max(fw, 8.0f), 10}, 0.5f, 6, col);
            string t = n > 0 ? (to_string(w) + " z " + to_string(n) + " (" + FmtPct(100.0 * w / n, 0) + ")") : "brak gier";
            TxtR(FS_REG, t, r.x + r.width - 18, y + 1, 13, n > 0 ? C_TEXT : C_MUTED);
            y += 26;
        };
        if (reczny) {
            row("Zostajesz", C_ORANGE, pStN, pStW);
            row("Zmieniasz", C_GREEN, pSwN, pSwW);
        } else {
            int n = (int)gLast.size(), ws = 0, wm = 0;
            for (const Rekord& g : gLast) { ws += g.stayWin ? 1 : 0; wm += g.switchWin ? 1 : 0; }
            row("Zostaje", C_ORANGE, n, ws);
            row("Zmienia", C_GREEN, n, wm);
        }
        y += 6;
    }
    // kropki: ostatnie 24 gry
    TxtFit(FS_REG, reczny ? "ostatnie gry (zielone = wygrana, biały okrąg = Twoja decyzja)" : "ostatnie gry (zielone = wygrana, czerwone = koza)", r.x + 18, y, 12, r.width - 36, C_MUTED);
    y += 20;
    for (int rowi = 0; rowi < 2; ++rowi) {
        bool stay = rowi == 0;
        Txt(FS_BOLD, stay ? "zostaje" : "zmienia", r.x + 18, y + 1, 12, stay ? C_ORANGE : C_GREEN);
        for (int i = 0; i < 24; ++i) {
            float cx = r.x + 82 + i * 11.4f, cy = y + 9;
            int idx = (int)gLast.size() - 24 + i;
            if (idx < 0) {
                DrawCircleV(Vector2{cx, cy}, 3.0f, Al(C_LINE, 0.5f));
                continue;
            }
            const Rekord& g = gLast[(size_t)idx];
            bool win = stay ? g.stayWin : g.switchWin;
            DrawCircleV(Vector2{cx, cy}, 4.6f, win ? C_GREEN : Color{150, 50, 60, 255});
            if (g.decyzja == (stay ? 1 : 2)) DrawCircleLines((int)cx, (int)cy, 7.0f, C_WHITE);
        }
        y += 24;
    }

    *saveClicked = Button("save", Rectangle{r.x + 18, r.y + r.height - 52, r.width - 36, 38}, "ZAPISZ WYNIKI DO PLIKU",
                          Color{58, 84, 160, 255}, true, 15);
}

// ------------------------------------------------------------------
//  Zapis wynikow do pliku
// ------------------------------------------------------------------
static string ZapiszWyniki() {
    string path = string(GetApplicationDirectory()) + "wyniki_monty_hall.txt";
    ofstream f(path, ios::binary);
    if (!f) {
        path = "wyniki_monty_hall.txt";
        f.open(path, ios::binary);
        if (!f) return "";
    }
    f << "\xEF\xBB\xBF";
    f << "PARADOKS MONTY HALLA - WYNIKI\n";
    f << "=============================\n\n";
    f << "Wszystkie gry:                 " << total << "\n";
    f << "Zostajac przy wyborze wygrano: " << wStay << " (" << (total ? 100.0 * wStay / total : 0.0) << "%)\n";
    f << "Zmieniajac drzwi wygrano:      " << wSwitch << " (" << (total ? 100.0 * wSwitch / total : 0.0) << "%)\n";
    f << "Teoria: zostajac 33.33%, zmieniajac 66.67%\n\n";
    f << "Gry z animacja: " << gLog.size() << ", gry z szybkiej symulacji (bez dziennika): " << gFastN << "\n";
    f << "Twoje decyzje: zostajesz " << pStN << "x (wygranych " << pStW << "), zmieniasz " << pSwN << "x (wygranych "
      << pSwW << ")\n\n";
    f << "DZIENNIK GIER Z ANIMACJA (rozdzielone srednikami)\n";
    f << "nr;tryb;auto_za_drzwiami;pierwszy_wybor;otwarte_przez_prowadzacego;decyzja;wynik_zostaje;wynik_zmienia\n";
    for (const Wpis& w : gLog) {
        f << w.nr << ";" << (w.reczny ? "recznie" : "losowo") << ";" << (w.nagroda + 1) << ";" << (w.wybor + 1) << ";"
          << (w.otwarte + 1) << ";" << (w.decyzja == 1 ? "zostaje" : (w.decyzja == 2 ? "zmienia" : "-")) << ";"
          << (w.stayWin ? "wygrana" : "koza") << ";" << (w.switchWin ? "wygrana" : "koza") << "\n";
    }
    return path;
}

// ------------------------------------------------------------------
//  Fazy gry i teksty prowadzacego
// ------------------------------------------------------------------
enum Faza { F_START, F_CZEKA_NA_WYBOR, F_WYBOR, F_PROWADZACY, F_DECYZJA, F_ODSLONIECIE, F_KONIEC };

static string TekstProwadzacego(Faza f, const Gra& g, bool reczny, int decyzja, bool fast) {
    string w = to_string(g.wybor + 1), o = to_string(g.otwarte + 1), z = to_string(g.zmieniony + 1);
    if (fast) return "Liczę tysiące gier naraz… Patrz na wykres!";
    switch (f) {
        case F_START:
            return reczny ? "Witaj w teleturnieju! Za jednymi drzwiami jest auto. Kliknij NOWA GRA."
                          : "Zobaczmy, jak gra losowy gracz. Kliknij NOWA GRA.";
        case F_CZEKA_NA_WYBOR: return "Za jednymi drzwiami jest auto, za dwoma kozy. Wybierz drzwi, klikając je!";
        case F_WYBOR:
            return reczny ? "Wybrałeś drzwi nr " + w + ". Zobaczmy, co jest za pozostałymi…"
                          : "Gracz wybiera drzwi nr " + w + ".";
        case F_PROWADZACY: return "Ja wiem, gdzie jest auto. Otwieram drzwi nr " + o + " — jest tam koza!";
        case F_DECYZJA: return "Zostajesz przy drzwiach nr " + w + " czy zmieniasz na nr " + z + "?";
        default:
            if (reczny) {
                bool win = decyzja == 2 ? g.zmianaWygrana : g.zostajWygrana;
                return win ? "Brawo! Wygrywasz auto!" : "Koza… Niestety! Spróbuj jeszcze raz!";
            }
            return string("Zostając: ") + (g.zostajWygrana ? "auto!" : "koza.") + " Zmieniając: " +
                   (g.zmianaWygrana ? "auto!" : "koza.");
    }
}

// ------------------------------------------------------------------
//  WYJASNIENIE PARADOKSU - nakladka z krokami i animacjami
// ------------------------------------------------------------------
static Texture2D gDoorTex[3];

struct ExplainState { bool open = false; bool just = false; int step = 0; float t = 0; float fade = 0; };
static ExplainState gEx;
static const int EX_STEPS = 7;
static const Rectangle EX_M = {60, 36, 1120, 678};
static int gExAction = 0;  // 1 = uzytkownik chce uruchomic szybka symulacje

static void Pill(float cx, float y, const string& text, Color bg, Color fg, int fs) {
    Vector2 ts = TextSize(FS_BOLD, text, fs);
    float w = ts.x + fs * 1.6f, h = fs + 11.0f;
    Rectangle r = {cx - w / 2.0f, y, w, h};
    DrawRectangleRounded(Rectangle{r.x, r.y + 3, r.width, r.height}, 0.5f, 8, Al(C_BLACK, 0.45f));
    DrawRectangleRounded(r, 0.5f, 8, Shade(bg, 0.72f));
    DrawRectangleRounded(Rectangle{r.x, r.y, r.width, r.height - 2}, 0.5f, 8, bg);
    Txt(FS_BOLD, text, r.x + fs * 0.8f, r.y + (h - ts.y) / 2.0f - 1, fs, fg);
}

static void MiniRoom(Rectangle in, int content) {
    DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)in.height, Color{50, 52, 112, 255}, Color{22, 22, 54, 255});
    float fy = in.y + in.height * 0.80f;
    DrawRectangleGradientV((int)in.x, (int)fy, (int)in.width, (int)(in.y + in.height - fy), Color{70, 62, 104, 255},
                           Color{30, 26, 58, 255});
    float cx = in.x + in.width / 2.0f, base = in.y + in.height - in.height * 0.07f;
    if (content == 2) {
        float s = in.width * 0.84f / 134.0f;
        Glow(cx, base - 30 * s, 50 * s + 10, Color{255, 200, 80, 255}, 0.5f);
        DrawCar(cx, base, s);
    } else {
        float s = in.width * 0.80f / 124.0f;
        DrawGoat(cx - 4 * s, base, s, cx);
    }
}

// Drzwi do wyjasnien: open 0..1, content 1 = koza, 2 = auto
static void MiniDoor(Rectangle r, int idx, float open, int content, Color ring, float ringA) {
    float pad = r.width * 0.06f;
    Rectangle in = {r.x + pad, r.y + pad, r.width - 2 * pad, r.height - pad};
    float o = EaseInOutCubic(open);
    Color frame = {72, 44, 26, 255};
    DrawRectangleRounded(Rectangle{r.x - 4, r.y - 6, r.width + 8, 10}, 0.4f, 4, Color{110, 70, 40, 255});
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)r.height, frame);
    DrawRectangleLinesEx(r, 2, Shade(frame, 0.55f));
    if (o > 0.004f) {
        ScissorOn(in);
        MiniRoom(in, content);
        DrawRectangle((int)in.x, (int)in.y, (int)in.width, (int)in.height, Al(C_BLACK, (1.0f - o) * 0.5f));
        EndScissorMode();
    } else {
        DrawRectangleRec(in, Color{10, 10, 18, 255});
    }
    float lw = in.width * (1.0f - 0.86f * o), dh = in.height * (1.0f - 0.14f * o), dy = in.y + (in.height - dh) / 2.0f;
    if (lw > 1.0f) {
        float sh = 1.0f - 0.40f * o;
        DrawTexturePro(gDoorTex[idx % 3], Rectangle{0, 0, (float)gDoorTex[idx % 3].width, (float)gDoorTex[idx % 3].height},
                       Rectangle{in.x, dy, lw, dh}, Vector2{0, 0}, 0.0f, RGBA((int)(255 * sh), (int)(255 * sh), (int)(255 * sh)));
        if (o < 0.4f) {
            float a = 1.0f - o / 0.4f;
            DrawCircleV(Vector2{in.x + lw - r.width * 0.09f, in.y + in.height * 0.56f}, max(2.5f, r.width * 0.035f), Al(Color{236, 196, 90, 255}, a));
            float pr = max(8.0f, r.width * 0.17f);
            Vector2 pc = {in.x + lw / 2.0f, in.y + in.height * 0.27f};
            DrawCircleV(pc, pr + 1.5f, Al(Color{90, 60, 16, 255}, a));
            DrawCircleV(pc, pr, Al(Color{222, 178, 76, 255}, a));
            int fs = max(10, (int)(pr * 1.15f));
            TxtC(FS_BOLD, to_string(idx + 1), pc.x, pc.y - fs * 0.62f, fs, Al(Color{70, 40, 10, 255}, a));
        }
    }
    if (ringA > 0.01f) {
        for (int i = 3; i >= 1; --i)
            DrawRectangleLinesEx(Rectangle{r.x - i * 3.0f, r.y - i * 3.0f, r.width + i * 6.0f, r.height + i * 6.0f}, 2.5f,
                                 Al(ring, ringA * (0.85f - i * 0.2f)));
        DrawRectangleLinesEx(Rectangle{r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 3.0f, Al(ring, ringA));
    }
}

static void ProbBar(Rectangle r, float anim, float f1, const string& l1, const string& l2, Color c1, Color c2) {
    DrawRectangleRounded(Rectangle{r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 0.4f, 8, Al(C_LINE, 0.9f));
    DrawRectangleRounded(r, 0.4f, 8, Color{16, 18, 36, 255});
    float k = Clamp01(anim);
    float w1 = (r.width - 6) * f1 * Clamp01(k * 1.6f), w2 = (r.width - 6) * (1.0f - f1) * Clamp01(k * 1.6f - 0.6f);
    if (w1 > 1) DrawRectangle((int)(r.x + 3), (int)(r.y + 3), (int)w1, (int)(r.height - 6), c1);
    if (w2 > 1) DrawRectangle((int)(r.x + 3 + (r.width - 6) * f1), (int)(r.y + 3), (int)w2, (int)(r.height - 6), c2);
    DrawRectangle((int)(r.x + 3), (int)(r.y + 3), (int)(w1 + w2), (int)((r.height - 6) * 0.35f), Al(C_WHITE, 0.14f));
    int fs = 15;
    if (w1 > TextSize(FS_BOLD, l1, fs).x + 12) TxtC(FS_BOLD, l1, r.x + 3 + w1 / 2.0f, r.y + (r.height - fs) / 2.0f - 1, fs, Color{40, 20, 0, 255});
    if (w2 > TextSize(FS_BOLD, l2, fs).x + 12)
        TxtC(FS_BOLD, l2, r.x + 3 + (r.width - 6) * f1 + w2 / 2.0f, r.y + (r.height - fs) / 2.0f - 1, fs, Color{4, 40, 20, 255});
}

static void Callout(Rectangle r, const string& title, const string& text, Color accent, float a) {
    if (a <= 0.01f) return;
    DrawRectangleRounded(Rectangle{r.x, r.y + 4, r.width, r.height}, 0.08f, 8, Al(C_BLACK, 0.35f * a));
    DrawRectangleRounded(r, 0.08f, 8, Al(Color{30, 36, 66, 255}, a));
    DrawRectangleRounded(Rectangle{r.x, r.y, 6, r.height}, 0.5f, 4, Al(accent, a));
    Txt(FS_BOLD, title, r.x + 20, r.y + 12, 16, Al(accent, a));
    vector<string> lines = Wrap(FS_REG, text, 15, r.width - 36);
    float y = r.y + 40;
    for (const string& l : lines) {
        Txt(FS_REG, l, r.x + 20, y, 15, Al(C_TEXT, a));
        y += 21;
    }
}

static void ExPara(const string& s, float x, float y, float w) {
    vector<string> lines = Wrap(FS_REG, s, 18, w);
    for (size_t i = 0; i < lines.size(); ++i) Txt(FS_REG, lines[i], x, y + i * 26.0f, 18, C_TEXT);
}

static Rectangle ExDoor(int i, float yOff = 0) { return Rectangle{374.0f + i * 184.0f, 300.0f + yOff, 124, 176}; }

static void ExDoorLabels(float t, bool showPills) {
    for (int i = 0; i < 3; ++i) {
        Rectangle d = ExDoor(i);
        if (showPills) Pill(d.x + d.width / 2, d.y + d.height + 14, "DRZWI " + to_string(i + 1), Color{46, 36, 76, 255}, C_TEXT, 13);
    }
    (void)t;
}

// ---------- Krok 1: zasady ----------
static void ExSlide1(float t) {
    ExPara("Za jednymi z trzech drzwi stoi auto, za pozostałymi dwojgiem — kozy. Wybierasz jedne drzwi. Prowadzący, który wie, gdzie jest auto, "
           "otwiera inne drzwi z kozą i pyta: zostajesz przy swoim wyborze czy zmieniasz?",
           90, 152, 1060);
    for (int i = 0; i < 3; ++i) {
        float a = EaseOutBack(Clamp01((t - 0.15f * i) / 0.55f));
        MiniDoor(ExDoor(i, (1.0f - a) * 40.0f), i, 0.0f, 1, C_GOLD, 0.0f);
    }
    ExDoorLabels(t, t > 0.8f);
    float a1 = Clamp01((t - 0.6f) / 0.4f), a2 = Clamp01((t - 1.0f) / 0.4f);
    Rectangle l = {90, 300, 240, 176}, r = {920, 300, 230, 176};
    if (a1 > 0.01f) {
        Callout(l, "WYGRANA", "", C_GREEN, a1);
        DrawCar(l.x + l.width / 2 + 4, l.y + 142, 1.0f);
        TxtC(FS_BOLD, "1 auto", l.x + l.width / 2, l.y + 148, 15, C_GREEN);
    }
    if (a2 > 0.01f) {
        Callout(r, "PRZEGRANA", "", C_RED, a2);
        DrawGoat(r.x + r.width / 2 - 6, r.y + 142, 0.95f, 3.0f);
        TxtC(FS_BOLD, "2 kozy", r.x + r.width / 2, r.y + 148, 15, C_RED);
    }
}

// ---------- Krok 2: pierwszy wybor ----------
static void ExSlide2(float t) {
    ExPara("Wybierasz drzwi nr 1. Szansa, że auto jest właśnie tam, wynosi tylko 1/3. Za to szansa, że auto jest za którymiś z pozostałych "
           "dwojga drzwi, to aż 2/3.",
           90, 152, 1060);
    float ring = Clamp01((t - 0.3f) / 0.4f);
    for (int i = 0; i < 3; ++i) MiniDoor(ExDoor(i), i, 0.0f, 1, C_GOLD, i == 0 ? ring * (0.7f + 0.3f * sinf((float)gTime * 4.0f)) : 0.0f);
    ExDoorLabels(t, true);
    float a1 = Clamp01((t - 0.6f) / 0.4f);
    Rectangle d0 = ExDoor(0), d1 = ExDoor(1), d2 = ExDoor(2);
    if (a1 > 0.01f) Pill(d0.x + d0.width / 2, 258, "TWÓJ WYBÓR: 1/3", C_GOLD, Color{60, 36, 6, 255}, 15);
    float a2 = Clamp01((t - 1.0f) / 0.5f);
    if (a2 > 0.01f) {
        float x0 = d1.x, x1 = d2.x + d2.width;
        DrawLineEx(Vector2{x0, 288}, Vector2{x1, 288}, 3, Al(C_CYAN, a2));
        DrawLineEx(Vector2{x0, 288}, Vector2{x0, 296}, 3, Al(C_CYAN, a2));
        DrawLineEx(Vector2{x1, 288}, Vector2{x1, 296}, 3, Al(C_CYAN, a2));
        Pill((x0 + x1) / 2, 258, "POZOSTAŁE DWOJE DRZWI RAZEM: 2/3", C_CYAN, Color{8, 40, 56, 255}, 15);
    }
    Rectangle bar = {374, 532, 492, 34};
    ProbBar(bar, Clamp01((t - 1.3f) / 1.0f), 1.0f / 3.0f, "1/3", "2/3", C_GOLD, C_CYAN);
    if (t > 2.4f) {
        TxtC(FS_REG, "Twoje drzwi", bar.x + bar.width / 6.0f, bar.y + 42, 14, C_GOLD);
        TxtC(FS_REG, "drzwi nr 2 i nr 3", bar.x + bar.width * 2.0f / 3.0f, bar.y + 42, 14, C_CYAN);
    }
}

// ---------- Krok 3: prowadzacy otwiera drzwi ----------
static void ExSlide3(float t) {
    ExPara("Prowadzący wie, gdzie jest auto. Zawsze otwiera drzwi, które nie są Twoim wyborem i nie kryją auta, czyli drzwi z kozą. "
           "To nie jest losowe! Dzięki temu dostajesz ważną wskazówkę.",
           90, 152, 1060);
    float open3 = Clamp01((t - 0.8f) / 0.9f);
    MiniDoor(ExDoor(0), 0, 0.0f, 1, C_GOLD, 0.85f);
    MiniDoor(ExDoor(1), 1, 0.0f, 1, C_GOLD, 0.0f);
    MiniDoor(ExDoor(2), 2, open3, 1, C_GOLD, 0.0f);
    ExDoorLabels(t, true);
    Rectangle d0 = ExDoor(0);
    Pill(d0.x + d0.width / 2, 258, "TWÓJ WYBÓR", C_GOLD, Color{60, 36, 6, 255}, 15);
    if (t > 1.8f) Pill(ExDoor(2).x + ExDoor(2).width / 2, 258, "KOZA! (otwiera prowadzący)", Color{150, 60, 70, 255}, C_TEXT, 14);
    Callout(Rectangle{90, 300, 250, 190}, "NIGDY NIE OTWIERA", "Twoich drzwi oraz drzwi z autem. Zawsze pokazuje kozę.", C_RED, Clamp01((t - 0.3f) / 0.4f));
    Callout(Rectangle{900, 300, 250, 190}, "TERAZ DECYZJA", "Zostały drzwi nr 1 (Twoje) i nr 2. Zostać czy zmienić? Który wybór jest lepszy?", C_CYAN,
            Clamp01((t - 2.2f) / 0.4f));
    ProbBar(Rectangle{374, 532, 492, 34}, 1.0f, 1.0f / 3.0f, "1/3", "2/3", C_GOLD, C_CYAN);
    TxtC(FS_REG, "Szanse po pierwszym wyborze (jeszcze się nie zmieniły)", 374 + 246, 574, 14, C_MUTED);
}

// ---------- Krok 4: cala szansa przechodzi ----------
static void ExSlide4(float t) {
    ExPara("Szansa 1/3 dla Twoich drzwi się nie zmieniła, bo prowadzący ich nie dotyka. Drzwi nr 3 odpadły, więc cała szansa 2/3, która była "
           "rozłożona na dwoje drzwi, przeszła na drzwi nr 2. Zmieniając, wygrywasz w 2 przypadkach na 3.",
           90, 152, 1060);
    float cy = Clamp01((t - 2.0f) / 0.4f);
    MiniDoor(ExDoor(0), 0, 0.0f, 1, C_GOLD, 0.85f);
    MiniDoor(ExDoor(1), 1, 0.0f, 1, C_CYAN, cy * (0.7f + 0.3f * sinf((float)gTime * 4.0f)));
    MiniDoor(ExDoor(2), 2, 1.0f, 1, C_GOLD, 0.0f);
    ExDoorLabels(t, true);
    Rectangle d0 = ExDoor(0), d1 = ExDoor(1), d2 = ExDoor(2);
    Pill(d0.x + d0.width / 2, 258, "ZOSTAJĘ: 1/3", C_GOLD, Color{60, 36, 6, 255}, 15);
    // "kulka" szansy 2/3 przesuwa sie z drzwi nr 3 na drzwi nr 2
    float k = EaseInOutCubic((t - 0.6f) / 1.4f);
    if (t > 0.3f && t < 2.4f) {
        float x = Lerpf(d2.x + d2.width / 2, d1.x + d1.width / 2, k);
        float y = 270.0f - sinf(k * PI_F) * 24.0f;
        Glow(x, y + 10, 40, Color{86, 208, 236, 255}, 0.8f);
        DrawCircleV(Vector2{x, y + 10}, 21, Color{20, 110, 140, 255});
        DrawCircleV(Vector2{x, y + 10}, 18, C_CYAN);
        TxtC(FS_BOLD, "2/3", x, y + 1, 16, Color{4, 40, 56, 255});
    }
    if (t > 2.0f) Pill(d1.x + d1.width / 2, 258, "ZMIENIAM: 2/3", C_CYAN, Color{8, 40, 56, 255}, 15);
    if (t > 0.5f) Pill(d2.x + d2.width / 2, 258, "0 (odpadły)", Color{70, 60, 96, 255}, C_MUTED, 14);
    ProbBar(Rectangle{374, 532, 492, 34}, Clamp01((t - 2.2f) / 0.8f), 1.0f / 3.0f, "ZOSTAJĘ 1/3", "ZMIENIAM 2/3", C_ORANGE, C_GREEN);
    Callout(Rectangle{900, 300, 250, 190}, "WNIOSEK", "Zmiana drzwi daje dwa razy większą szansę na auto niż zostanie przy pierwszym wyborze.", C_GREEN,
            Clamp01((t - 3.0f) / 0.4f));
}

// ---------- Krok 5: wszystkie mozliwosci ----------
static void ExSlide5(float t) {
    Txt(FS_REG, "Wybrałeś drzwi nr 1 (złoty obrys). Auto może być w jednym z trzech miejsc, każde jest tak samo prawdopodobne:", 90, 152, 18, C_TEXT);
    const int carAt[3] = {0, 1, 2}, opened[3] = {1, 2, 1};
    const bool swWin[3] = {false, true, true};
    const char* title[3] = {"Przypadek A", "Przypadek B", "Przypadek C"};
    for (int r = 0; r < 3; ++r) {
        float ts = 0.4f + r * 1.5f;
        float e = EaseOutCubic((t - ts) / 0.5f);
        if (e <= 0.01f) continue;
        float ox = (1.0f - e) * 70.0f;
        float y = 200.0f + r * 126.0f;
        DrawRectangleRounded(Rectangle{90 - ox, y, 1060, 112}, 0.08f, 8, Al(Color{30, 36, 66, 255}, e));
        Txt(FS_BOLD, title[r], 108 - ox, y + 18, 18, Al(C_GOLD, e));
        Txt(FS_REG, "auto jest za drzwiami nr " + to_string(carAt[r] + 1), 108 - ox, y + 46, 15, Al(C_TEXT, e));
        Txt(FS_REG, r == 0 ? "(prowadzący może otworzyć 2 albo 3)" : "(prowadzący musi otworzyć jedyne drzwi z kozą)", 108 - ox, y + 70, 13,
            Al(C_MUTED, e));
        float open = Clamp01((t - (ts + 0.6f)) / 0.5f);
        for (int d = 0; d < 3; ++d) {
            Rectangle dr = {360.0f + d * 96.0f - ox, y + 16, 66, 90};
            MiniDoor(dr, d, d == opened[r] ? open : 0.0f, 1, C_GOLD, d == 0 ? 0.9f * e : 0.0f);
            if (d == carAt[r] && t > ts + 0.2f) Pill(dr.x + dr.width / 2, y - 6, "auto", C_GREEN, Color{8, 50, 24, 255}, 11);
        }
        float ro = Clamp01((t - (ts + 1.2f)) / 0.4f);
        if (ro > 0.01f) {
            Txt(FS_REG, "Prowadzący otwiera drzwi nr " + to_string(opened[r] + 1), 680 - ox, y + 14, 15, Al(C_TEXT, ro));
            bool sw = swWin[r];
            Pill(780 - ox, y + 44, string("Zostaję: ") + (!sw ? "WYGRANA" : "PRZEGRANA"), !sw ? C_GREEN : Color{170, 60, 70, 255}, C_TEXT, 14);
            Pill(1000 - ox, y + 44, string("Zmieniam: ") + (sw ? "WYGRANA" : "PRZEGRANA"), sw ? C_GREEN : Color{170, 60, 70, 255}, C_TEXT, 14);
        }
    }
    float as = Clamp01((t - 4.9f) / 0.5f);
    if (as > 0.01f) {
        TxtC(FS_BOLD, "Zostając wygrywasz w 1 z 3 przypadków, zmieniając w 2 z 3.", 620, 590, 22, Al(C_GOLD, as));
    }
}

// ---------- Krok 6: 100 drzwi ----------
static void MiniDoorIcon(float x, float y, float w, float h, float open, int seedIdx) {
    float o = EaseInOutCubic(open);
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Color{72, 44, 26, 255});
    Rectangle in = {x + 3, y + 3, w - 6, h - 3};
    if (o > 0.01f) {
        DrawRectangleRec(in, Color{34, 36, 84, 255});
        float cx = in.x + in.width / 2, cy = in.y + in.height - 6;
        DrawEllipse((int)cx, (int)(cy - 6), 7, 4, Color{245, 245, 240, 255});
        DrawCircleV(Vector2{cx + 6, cy - 11}, 3.2f, Color{245, 245, 240, 255});
        DrawRectangle((int)(cx - 5), (int)(cy - 3), 2, 4, Color{210, 210, 205, 255});
        DrawRectangle((int)(cx + 3), (int)(cy - 3), 2, 4, Color{210, 210, 205, 255});
    } else {
        DrawRectangleRec(in, Color{10, 10, 18, 255});
    }
    float lw = in.width * (1.0f - 0.82f * o);
    if (lw > 1) {
        DrawRectangle((int)in.x, (int)in.y, (int)lw, (int)in.height, Shade(Color{160, 104, 56, 255}, 1.0f - 0.35f * o));
        if (lw > 10) {
            DrawRectangleLinesEx(Rectangle{in.x + 3, in.y + 4, lw - 6, in.height * 0.4f}, 1.2f, Color{110, 70, 36, 255});
            DrawRectangleLinesEx(Rectangle{in.x + 3, in.y + in.height * 0.52f, lw - 6, in.height * 0.40f}, 1.2f, Color{110, 70, 36, 255});
            DrawCircleV(Vector2{in.x + lw - 4, in.y + in.height * 0.55f}, 1.8f, Color{236, 196, 90, 255});
        }
    }
    (void)seedIdx;
}

static void ExSlide6(float t) {
    ExPara("Wyobraź sobie 100 drzwi i jedno auto. Wybierasz jedne drzwi, szansa to 1/100. Prowadzący, który wie, gdzie jest auto, otwiera 98 drzwi "
           "z kozami. Zostają Twoje drzwi i jedne, które celowo ominął. Zostać przy swoim 1% czy zmienić? Zmiana wygrywa w 99 przypadkach na 100.",
           90, 152, 1060);
    const int chosen = 26, other = 71;
    static int order[98];
    static bool init = false;
    if (!init) {
        int n = 0;
        for (int i = 0; i < 100; ++i)
            if (i != chosen && i != other) order[n++] = i;
        unsigned s = 12345u;
        for (int i = 97; i > 0; --i) {
            s = s * 1664525u + 1013904223u;
            int j = (int)((s >> 8) % (unsigned)(i + 1));
            swap(order[i], order[j]);
        }
        init = true;
    }
    float openAt[100];
    for (int i = 0; i < 100; ++i) openAt[i] = -1;
    for (int k = 0; k < 98; ++k) openAt[order[k]] = 1.0f + k * 0.03f;
    for (int i = 0; i < 100; ++i) {
        int col = i % 20, row = i / 20;
        float x = 108 + col * 52.0f, y = 258 + row * 62.0f;
        float open = openAt[i] < 0 ? 0.0f : Clamp01((t - openAt[i]) / 0.3f);
        float pop = EaseOutBack(Clamp01((t - 0.005f * i) / 0.4f));
        MiniDoorIcon(x, y + (1.0f - pop) * 16.0f, 34, 48, open, i);
    }
    // oznaczenia
    float cx = 108 + (chosen % 20) * 52.0f, cy = 258 + (chosen / 20) * 62.0f;
    float ox = 108 + (other % 20) * 52.0f, oy = 258 + (other / 20) * 62.0f;
    float g = Clamp01((t - 0.5f) / 0.4f) * (0.75f + 0.25f * sinf((float)gTime * 4.0f));
    DrawRectangleLinesEx(Rectangle{cx - 4, cy - 4, 42, 56}, 3, Al(C_GOLD, g));
    DrawRectangleLinesEx(Rectangle{cx - 7, cy - 7, 48, 62}, 2, Al(C_GOLD, g * 0.5f));
    float c2 = Clamp01((t - 4.4f) / 0.4f) * (0.75f + 0.25f * sinf((float)gTime * 4.0f));
    if (c2 > 0.01f) {
        DrawRectangleLinesEx(Rectangle{ox - 4, oy - 4, 42, 56}, 3, Al(C_CYAN, c2));
        DrawRectangleLinesEx(Rectangle{ox - 7, oy - 7, 48, 62}, 2, Al(C_CYAN, c2 * 0.5f));
    }
    if (t > 0.5f) Pill(cx + 17, cy - 28, "TWÓJ WYBÓR", C_GOLD, Color{60, 36, 6, 255}, 12);
    if (t > 4.4f) Pill(ox + 17, oy + 66, "ZMIANA", C_CYAN, Color{8, 40, 56, 255}, 12);
    float bb = Clamp01((t - 4.8f) / 0.8f);
    if (bb > 0.01f) {
        ProbBar(Rectangle{108, 596, 1040, 30}, bb, 0.01f, "", "ZMIENIAM: 99%", C_GOLD, C_CYAN);
        TxtC(FS_BOLD, "ZOSTAJĘ: 1%", 108 + 90, 570, 15, C_GOLD);
    }
}

// ---------- Krok 7: dowod - symulacja ----------
static void ExSlide7(float t) {
    ExPara("Teoria mówi: zostając wygrywasz w 1/3 gier, a zmieniając w 2/3. Ten program może to sprawdzić na dowolnej liczbie losowych gier. "
           "Poniżej Twoje dotychczasowe wyniki (z gier ręcznych, losowych i z szybkiej symulacji).",
           90, 152, 1060);
    float anim = EaseOutCubic((t - 0.3f) / 1.0f);
    float ps = total > 0 ? (float)(100.0 * wStay / total) : 0.0f, pm = total > 0 ? (float)(100.0 * wSwitch / total) : 0.0f;
    auto bar = [&](float y, const string& name, Color col, float pct, long long wins, double theory) {
        Txt(FS_BOLD, name, 130, y, 20, C_TEXT);
        TxtR(FS_BOLD, FmtPct(pct * anim, 1), 1110, y - 6, 28, col);
        Rectangle track = {130, y + 36, 980, 26};
        DrawRectangleRounded(Rectangle{track.x - 2, track.y - 2, track.width + 4, track.height + 4}, 0.5f, 8, Al(C_LINE, 0.9f));
        DrawRectangleRounded(track, 0.5f, 8, Color{16, 18, 36, 255});
        float fw = pct / 100.0f * track.width * anim;
        if (fw > 4) {
            DrawRectangleRounded(Rectangle{track.x, track.y, max(fw, 14.0f), track.height}, 0.5f, 8, Shade(col, 0.75f));
            DrawRectangleRounded(Rectangle{track.x, track.y, max(fw, 14.0f), track.height - 4}, 0.5f, 8, col);
        }
        float mx = track.x + (float)(theory / 100.0) * track.width;
        DrawRectangle((int)(mx - 1.5f), (int)(track.y - 6), 3, (int)(track.height + 12), Al(C_WHITE, 0.9f));
        TxtC(FS_REG, theory < 50 ? "teoria: 33,3%" : "teoria: 66,7%", mx, track.y + track.height + 10, 13, C_MUTED);
        Txt(FS_REG, "wygranych: " + FmtInt(wins) + " z " + FmtInt(total), 130, track.y + track.height + 10, 13, C_MUTED);
    };
    if (total >= 5) {
        bar(262, "ZOSTAJE przy pierwszym wyborze", C_ORANGE, ps, wStay, 100.0 / 3.0);
        bar(372, "ZMIENIA drzwi", C_GREEN, pm, wSwitch, 200.0 / 3.0);
        Callout(Rectangle{130, 478, 980, 56}, "", "", C_GOLD, 0.0f);
        TxtC(FS_BOLD, "Im więcej gier, tym bliżej teorii. Wniosek: ZMIENIAJ drzwi, wygrywasz średnio 2 razy częściej.", 620, 490, 21, C_GOLD);
    } else {
        TxtC(FS_BOLD, "Masz jeszcze za mało rozegranych gier.", 620, 300, 24, C_TEXT);
        TxtC(FS_REG, "Zagraj kilka gier albo uruchom szybką symulację — wynik pojawi się tutaj.", 620, 340, 18, C_MUTED);
    }
    if (Button("ex_run", Rectangle{420, 560, 400, 46}, "URUCHOM SZYBKĄ SYMULACJĘ (10 000 GIER)", Color{40, 140, 90, 255}, true, 17)) gExAction = 1;
}

// ---------- ramka nakladki ----------
static int DrawExplain() {
    gExAction = 0;
    float target = gEx.open ? 1.0f : 0.0f;
    gEx.fade += (target - gEx.fade) * min(1.0f, gDt * 14.0f);
    if (gEx.fade < 0.01f && !gEx.open) return 0;
    gEx.t += gDt;
    gUseRaw = gEx.open && !gEx.just;
    bool live = gUseRaw;
    gEx.just = false;
    Vector2 savedShake = gShake;
    gShake = Vector2{0, 0};

    DrawRectangle(0, 0, WIN_W, WIN_H, Al(C_BLACK, 0.80f * gEx.fade));
    if (gEx.fade > 0.35f) {
        Rectangle M = EX_M;
        DrawRectangleRounded(Rectangle{M.x + 4, M.y + 10, M.width, M.height}, 0.03f, 12, Al(C_BLACK, 0.5f));
        DrawRectangleRounded(Rectangle{M.x - 2, M.y - 2, M.width + 4, M.height + 4}, 0.03f, 12, Color{150, 108, 30, 255});
        DrawRectangleRounded(M, 0.03f, 12, Color{20, 24, 48, 255});

        static const char* heads[EX_STEPS] = {"1. Zasady gry", "2. Pierwszy wybór: szansa tylko 1/3", "3. Prowadzący otwiera drzwi z kozą",
                                              "4. Cała szansa 2/3 przechodzi na drugie drzwi", "5. Sprawdźmy wszystkie możliwości",
                                              "6. Intuicja: 100 drzwi zamiast 3", "7. Dowód: symulacja"};
        Txt(FS_BOLD, "JAK DZIAŁA PARADOKS MONTY HALLA", M.x + 30, M.y + 16, 22, C_GOLD);
        TxtR(FS_REG, "krok " + to_string(gEx.step + 1) + " z " + to_string(EX_STEPS), M.x + M.width - 170, M.y + 22, 15, C_MUTED);
        if (Button("ex_close", Rectangle{M.x + M.width - 140, M.y + 14, 112, 34}, "ZAMKNIJ", Color{128, 56, 70, 255}, true, 15)) gEx.open = false;
        DrawLineEx(Vector2{M.x + 30, M.y + 62}, Vector2{M.x + M.width - 30, M.y + 62}, 1.5f, Al(C_LINE, 0.8f));
        Txt(FS_BOLD, heads[gEx.step], 90, 110, 26, C_TEXT);

        float t = gEx.t;
        switch (gEx.step) {
            case 0: ExSlide1(t); break;
            case 1: ExSlide2(t); break;
            case 2: ExSlide3(t); break;
            case 3: ExSlide4(t); break;
            case 4: ExSlide5(t); break;
            case 5: ExSlide6(t); break;
            default: ExSlide7(t); break;
        }
        // krotkie "rozjasnienie" przy zmianie kroku
        DrawRectangle(80, 100, 1080, 540, Al(Color{20, 24, 48, 255}, 1.0f - EaseOutCubic(t / 0.28f)));

        // nawigacja
        auto go = [&](int s) {
            s = ClampI(s, 0, EX_STEPS - 1);
            if (s != gEx.step) { gEx.step = s; gEx.t = 0; }
        };
        float ny = M.y + M.height - 58;
        if (Button("ex_prev", Rectangle{M.x + 30, ny, 150, 42}, "WSTECZ", C_BLUE, gEx.step > 0, 17)) go(gEx.step - 1);
        if (Button("ex_replay", Rectangle{M.x + 196, ny, 220, 42}, "POWTÓRZ ANIMACJĘ", Color{80, 70, 140, 255}, true, 15)) gEx.t = 0;
        if (Button("ex_next", Rectangle{M.x + M.width - 180, ny, 150, 42}, "DALEJ", Color{40, 140, 90, 255}, gEx.step < EX_STEPS - 1, 17)) go(gEx.step + 1);
        // kropki krokow
        for (int i = 0; i < EX_STEPS; ++i) {
            Vector2 c = {M.x + M.width / 2.0f - (EX_STEPS - 1) * 14.0f + i * 28.0f + 10.0f, ny + 21};
            bool hov = live && CheckCollisionPointRec(RawMouse(), Rectangle{c.x - 11, c.y - 11, 22, 22});
            if (hov) gHand = true;
            DrawCircleV(c, i == gEx.step ? 8.0f : 5.5f, i == gEx.step ? C_GOLD : (hov ? C_TEXT : Al(C_MUTED, 0.7f)));
            if (hov && RawClick()) go(i);
        }
        if (gEx.open && live) {
            if (IsKeyPressed(KEY_RIGHT)) go(gEx.step + 1);
            if (IsKeyPressed(KEY_LEFT)) go(gEx.step - 1);
            if (IsKeyPressed(KEY_ESCAPE)) gEx.open = false;
        }
    }
    gUseRaw = false;
    gShake = savedShake;
    return gExAction;
}

// ------------------------------------------------------------------
//  Program glowny
// ------------------------------------------------------------------
#ifdef MH_TEST
struct Step { int f; int kind; float x, y; const char* name; };  // kind: 0 klik, 1 zrzut, 2 ruch myszy
static void Shot(const char* name) {
    Image img = LoadImageFromScreen();
    Color* p = (Color*)img.data;
    int w = img.width, h = img.height;
    for (int y = 0; y < h / 2; ++y)
        for (int x = 0; x < w; ++x) swap(p[y * w + x], p[(h - 1 - y) * w + x]);
    for (int i = 0; i < w * h; ++i) swap(p[i].r, p[i].b);
    ExportImage(img, TextFormat("/tmp/shots/%s.png", name));
    UnloadImage(img);
}
#endif

int main() {
    {
        random_device rd;
        unsigned a = rd();
        unsigned b = (unsigned)chrono::steady_clock::now().time_since_epoch().count();
        seed_seq ss{a, b};
        gen.seed(ss);
    }
#ifdef MH_TEST
    gen.seed(12345u);
#endif

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
#ifdef MH_TEST
    InitWindow(getenv("MH_W") ? atoi(getenv("MH_W")) : WIN_W, getenv("MH_H") ? atoi(getenv("MH_H")) : WIN_H,
               "Paradoks Monty Halla - symulacja");
#else
    InitWindow(WIN_W, WIN_H, "Paradoks Monty Halla - symulacja");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    {
        // na malych ekranach zmniejsz okno tak, zeby calosc sie miescila
        int mon = GetCurrentMonitor();
        int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
        if (mw > 0 && mh > 0) {
            float k = min(1.0f, min((mw - 40) / (float)WIN_W, (mh - 120) / (float)WIN_H));
            if (k < 1.0f && k > 0.3f) {
                SetWindowSize((int)(WIN_W * k), (int)(WIN_H * k));
                SetWindowPosition(20, 30);
            }
        }
        SetWindowMinSize(620, 380);
    }
#endif
    InitFonts();
    Texture2D* doorTex = gDoorTex;
    for (int i = 0; i < 3; ++i) doorTex[i] = MakeDoorTexture((int)(DOOR_W - 20), (int)(DOOR_H - 10), 11u + (unsigned)i * 37u);

    // ---------------- stan gry ----------------
    Faza faza = F_START;
    Gra gra = {0, -1, -1, -1, false, false};
    bool maGre = false, policzona = false;
    int decyzja = 0;
    float t = 0.0f;
    float otw[3] = {0, 0, 0};

    bool autoPlay = false, reczny = true, pokazAuto = true;
    int predkoscIdx = 0;
    const float predkosci[4] = {1.0f, 2.0f, 4.0f, 8.0f};

    bool fastRun = false;
    long long fastCel = 0, fastZrobione = 0;
    string wpis = "50000";
    bool fokus = false;

    float hoverA[3] = {0, 0, 0}, selA[3] = {0, 0, 0};
    bool fxPending = false, lastWin = false;
    float bannerT = 0, shakeAmp = 0, flashA = 0;
    Vector2 gPoint = {-1, 0.2f};
    string bubbleFull;
    float bubbleChars = 0;
    string toast;
    float toastT = 0;

    // ---------------- uklad interfejsu ----------------
    const Rectangle rPanel = {10, 522, 840, 62};
    const Rectangle rNew = {10, 594, 210, 40}, rMode = {230, 594, 300, 40}, rSpeed = {540, 594, 160, 40},
                    rReset = {710, 594, 130, 40};
    const Rectangle rStay = {10, 642, 300, 40}, rSwitch = {320, 642, 300, 40}, rExplain = {630, 642, 210, 40};
    const Rectangle rAuto = {10, 642, 200, 40}, rShowAuto = {218, 642, 200, 40};
    const Rectangle rInput = {10, 706, 170, 36}, rSim = {544, 706, 200, 36};
    const Rectangle cardA = {866, 10, 364, 252}, cardB = {866, 272, 364, 236}, cardC = {866, 518, 364, 224};

    // ---------------- akcje ----------------
    auto nowaGra = [&]() {
        if (reczny) {
            gra = Gra{los3(), -1, -1, -1, false, false};
            faza = F_CZEKA_NA_WYBOR;
        } else {
            gra = ZagrajLosowo();
            faza = F_WYBOR;
        }
        maGre = true;
        policzona = false;
        decyzja = 0;
        t = 0.0f;
        for (float& o : otw) o = 0.0f;
        fxPending = false;
        bannerT = 0;
    };
    auto wybierz = [&](int i) {
        gra.wybor = i;
        UstawProwadzacego(gra);
        faza = F_WYBOR;
        t = 0.0f;
    };
    auto zapisz = [&](int dec) {
        decyzja = dec;
        if (reczny) {
            if (dec == 2) { ++pSwN; if (gra.zmianaWygrana) ++pSwW; }
            else { ++pStN; if (gra.zostajWygrana) ++pStW; }
        }
        DodajWynik(gra, dec);
        gLog.push_back({(int)gLog.size() + 1, reczny, gra.nagroda, gra.wybor, gra.otwarte, dec, gra.zostajWygrana,
                        gra.zmianaWygrana});
        policzona = true;
        fxPending = true;
        lastWin = reczny ? (dec == 2 ? gra.zmianaWygrana : gra.zostajWygrana) : gra.zmianaWygrana;
        faza = F_ODSLONIECIE;
        t = 0.0f;
    };

#ifdef MH_TEST
    vector<Step> script;
    int tf = 0, lastFrame = 0;
    {
        const char* scn = getenv("MH_SCN") ? getenv("MH_SCN") : "loss";
        auto C = [&](Rectangle r) { return Vector2{r.x + r.width / 2, r.y + r.height / 2}; };
        auto add = [&](int f, int kind, Vector2 p, const char* n = "") { script.push_back({f, kind, p.x, p.y, n}); };
        Vector2 d1 = C(DoorRect(0)), d2 = C(DoorRect(1)), d3 = C(DoorRect(2));
        (void)d1; (void)d3;
        if (string(scn) == "loss") {
            add(10, 0, C(rNew));
            add(20, 2, d2);
            add(40, 1, {0, 0}, "L0_hover");
            add(45, 0, d2);
            add(110, 1, {0, 0}, "L1_prowadzacy");
            add(185, 3, {0, 0});
            add(205, 1, {0, 0}, "L2_odslonieciecie");
            add(222, 1, {0, 0}, "L3_shake");
            add(300, 1, {0, 0}, "L4_koniec");
            lastFrame = 302;
        } else if (string(scn) == "ex") {
            const float tt[7] = {2.2f, 3.0f, 3.0f, 4.0f, 6.5f, 7.0f, 3.0f};
            for (int i = 0; i < 7; ++i) {
                add(10 + i * 4, 4, Vector2{(float)i, tt[i] - 1.0f / 60.0f * 0});
                { static const char* nm[7] = {"X1","X2","X3","X4","X5","X6","X7"}; add(12 + i * 4, 1, {0, 0}, nm[i]); }
            }
            lastFrame = 40;
        } else {  // "rand"
            add(5, 1, {0, 0}, "R0_start");
            add(10, 0, C(rMode));
            add(14, 0, C(rNew));
            add(100, 1, {0, 0}, "R1_wybor");
            add(160, 1, {0, 0}, "R2_prowadzacy");
            add(290, 1, {0, 0}, "R3_reveal");
            add(340, 0, Vector2{362, 724});
            add(345, 0, C(rSim));
            add(380, 1, {0, 0}, "R4_fast_progress");
            add(560, 2, Vector2{1090, 380});
            add(565, 1, {0, 0}, "R5_fast_done");
            lastFrame = 567;
        }
    }
#endif

    while (!WindowShouldClose()) {
#ifdef MH_TEST
        ++tf;
        gClick = false;
        for (const Step& s : script)
            if (s.f == tf) {
                if (s.kind == 0) { gMouse = Vector2{s.x, s.y}; gClick = true; }
                else if (s.kind == 2) gMouse = Vector2{s.x, s.y};
                else if (s.kind == 3 && faza == F_DECYZJA) zapisz(gra.zostajWygrana ? 2 : 1);
                else if (s.kind == 4) { gEx.open = true; gEx.step = (int)s.x; gEx.t = s.y; gEx.just = false; }
            }
        if (tf > lastFrame) break;
        gDt = 1.0f / 60.0f;
#else
        gDt = min(GetFrameTime(), 0.05f);
#endif
        gTime += gDt;
        gHand = false;
        gBlock = gEx.open;
        if (gEx.open) fokus = false;
        {
            int sw = GetScreenWidth(), sh = GetScreenHeight();
            gScale = min(sw / (float)WIN_W, sh / (float)WIN_H);
            if (gScale <= 0.01f) gScale = 1.0f;
            gOff = Vector2{(sw - WIN_W * gScale) / 2.0f, (sh - WIN_H * gScale) / 2.0f};
        }

        // ================= logika =================
        bool mozeStartowac = !fastRun && (faza == F_START || faza == F_KONIEC);

        if (MouseClick()) fokus = !fastRun && CheckCollisionPointRec(MousePos(), rInput);
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (fokus && ch >= '0' && ch <= '9' && wpis.size() < 10) wpis.push_back((char)ch);
        if (fokus && IsKeyPressed(KEY_BACKSPACE) && !wpis.empty()) wpis.pop_back();
        if (!fokus && IsKeyPressed(KEY_SPACE) && mozeStartowac) nowaGra();

        Vector2 mp = MousePos();
        int doorHover = -1;
        for (int i = 0; i < 3; ++i)
            if (CheckCollisionPointRec(mp, DoorRect(i))) doorHover = i;
        bool clickable[3];
        for (int i = 0; i < 3; ++i)
            clickable[i] = !fastRun && (faza == F_CZEKA_NA_WYBOR || (faza == F_DECYZJA && (i == gra.wybor || i == gra.zmieniony)));
        auto clickDoor = [&](int d) {
            if (!clickable[d]) return;
            if (faza == F_CZEKA_NA_WYBOR) wybierz(d);
            else if (faza == F_DECYZJA) zapisz(d == gra.wybor ? 1 : 2);
        };
        if (doorHover >= 0 && clickable[doorHover]) {
            gHand = true;
            if (MouseClick()) clickDoor(doorHover);
        }
        if (!fokus)
            for (int k = 0; k < 3; ++k)
                if (IsKeyPressed(KEY_ONE + k)) clickDoor(k);

        if (!fastRun) {
            t += gDt * predkosci[predkoscIdx];
            switch (faza) {
                case F_START:
                    if (autoPlay && !reczny && t > 0.4f) nowaGra();
                    break;
                case F_WYBOR:
                    if (t > (reczny ? 0.8f : 1.3f)) { faza = F_PROWADZACY; t = 0.0f; }
                    break;
                case F_PROWADZACY:
                    otw[gra.otwarte] = min(1.0f, t / 0.8f);
                    if (t > (reczny ? 1.3f : 2.0f)) {
                        if (reczny) { faza = F_DECYZJA; t = 0.0f; }
                        else zapisz(0);
                    }
                    break;
                case F_ODSLONIECIE: {
                    float p = min(1.0f, t / 0.8f);
                    for (int i = 0; i < 3; ++i)
                        if (i != gra.otwarte) otw[i] = p;
                    if (fxPending && t > 0.55f) {
                        fxPending = false;
                        if (reczny) {
                            if (lastWin) Wystrzel(190, {C_GOLD, C_RED, C_CYAN, C_GREEN, C_WHITE, Color{230, 90, 210, 255}});
                            else { shakeAmp = 9.0f; flashA = 0.35f; }
                        } else if (lastWin) {
                            Wystrzel(80, {C_GREEN, Color{150, 240, 190, 255}, C_WHITE, C_GOLD});
                        } else {
                            Wystrzel(50, {C_ORANGE, Color{255, 200, 120, 255}, C_WHITE});
                        }
                    }
                    if (t > 1.0f) { faza = F_KONIEC; t = 0.0f; }
                    break;
                }
                case F_KONIEC:
                    if (autoPlay && !reczny && t > 2.0f) nowaGra();
                    break;
                default: break;
            }
        } else {
            long long batch = max(1LL, fastCel / 180);
            for (long long k = 0; k < batch && fastZrobione < fastCel; ++k) {
                Gra g = ZagrajLosowo();
                DodajWynik(g, 0);
                ++gFastN;
                if (g.zostajWygrana) ++gFastStay;
                if (g.zmianaWygrana) ++gFastSwitch;
                ++fastZrobione;
            }
            if (fastZrobione >= fastCel) fastRun = false;
        }

        // ---------- animacje pomocnicze ----------
        UpdateKonfetti(gDt);
        UpdateDisp(gDt);
        shakeAmp *= expf(-7.0f * gDt);
        gShake = shakeAmp > 0.4f ? Vector2{roundf(RandF(-1, 1) * shakeAmp), roundf(RandF(-1, 1) * shakeAmp)} : Vector2{0, 0};
        flashA = max(0.0f, flashA - gDt * 0.9f);
        toastT -= gDt;
        if (reczny && faza >= F_ODSLONIECIE && !fastRun) bannerT += gDt;

        bool stayHoverBtn = faza == F_DECYZJA && !fastRun && CheckCollisionPointRec(mp, rStay);
        bool switchHoverBtn = faza == F_DECYZJA && !fastRun && CheckCollisionPointRec(mp, rSwitch);
        for (int i = 0; i < 3; ++i) {
            float th = (doorHover == i && clickable[i]) || (stayHoverBtn && i == gra.wybor) || (switchHoverBtn && i == gra.zmieniony) ? 1.0f : 0.0f;
            float ts = (maGre && faza >= F_WYBOR && i == gra.wybor && !fastRun) ? 1.0f : 0.0f;
            hoverA[i] += (th - hoverA[i]) * min(1.0f, gDt * 12.0f);
            selA[i] += (ts - selA[i]) * min(1.0f, gDt * 8.0f);
        }

        // prowadzacy: gdzie wskazuje, mowi, mruga
        int pd = 1;
        switch (faza) {
            case F_CZEKA_NA_WYBOR: pd = (int)(gTime * 0.7) % 3; break;
            case F_WYBOR: pd = gra.wybor; break;
            case F_PROWADZACY: pd = gra.otwarte; break;
            case F_DECYZJA: pd = ((int)(gTime * 0.9) % 2) ? gra.wybor : gra.zmieniony; break;
            case F_ODSLONIECIE:
            case F_KONIEC: pd = gra.nagroda; break;
            default: pd = 1;
        }
        if (pd < 0 || pd > 2) pd = 1;
        bool cheer = reczny && lastWin && faza >= F_ODSLONIECIE && !fastRun;
        Vector2 want;
        {
            Rectangle dr = DoorRect(pd);
            want = Vector2{dr.x + dr.width / 2 - (PRES_X - 27), dr.y + 90 - (PRES_Y - 123)};
            if (cheer) want = Vector2{-0.45f, -1.0f};
            float l = sqrtf(want.x * want.x + want.y * want.y);
            want = Vector2{want.x / l, want.y / l};
        }
        gPoint.x += (want.x - gPoint.x) * min(1.0f, gDt * 6.0f);
        gPoint.y += (want.y - gPoint.y) * min(1.0f, gDt * 6.0f);

        string btxt = TekstProwadzacego(faza, gra, reczny, decyzja, fastRun);
        if (btxt != bubbleFull) { bubbleFull = btxt; bubbleChars = 0; }
        bubbleChars += 46.0f * gDt;
        bool speaking = bubbleChars < Utf8Len(bubbleFull) + 1;
        float mouth = speaking ? 0.5f + 0.5f * sinf((float)gTime * 13.0f) : 0.0f;
        bool blink = fmodf((float)gTime, 3.8f) < 0.12f;

        // ================= rysowanie =================
        BeginDrawing();
        ClearBackground(C_BG);

        Camera2D cam = {};
        cam.offset = Vector2{gOff.x + gShake.x * gScale, gOff.y + gShake.y * gScale};
        cam.zoom = gScale;
        BeginMode2D(cam);

        DrawWallAndFloor();
        for (int i = 0; i < 3; ++i) {
            float inten = 0.55f + 0.55f * hoverA[i] + 0.50f * selA[i] + 0.35f * otw[i];
            DrawSpot(DoorRect(i).x + DOOR_W / 2, inten);
        }
        DrawCurtains();
        DrawMarquee();
        for (int i = 0; i < 3; ++i) DrawSpotFixture(DoorRect(i).x + DOOR_W / 2);

        for (int i = 0; i < 3; ++i) {
            int content = (maGre && i == gra.nagroda) ? 2 : 1;
            DrawDoor(i, doorTex[i], otw[i], content, hoverA[i], selA[i], 0.0f);
        }
        for (int i = 0; i < 3; ++i) {
            float cx = DoorRect(i).x + DOOR_W / 2;
            string pt = "DRZWI " + to_string(i + 1);
            Color bg = Color{46, 36, 76, 255}, fg = C_TEXT;
            if (maGre && !fastRun && faza >= F_WYBOR && i == gra.wybor) {
                pt = reczny ? "TWÓJ WYBÓR" : "WYBÓR GRACZA";
                bg = C_GOLD;
                fg = Color{60, 36, 6, 255};
            } else if (maGre && !fastRun && i == gra.zmieniony && (faza >= F_DECYZJA || (!reczny && faza >= F_PROWADZACY))) {
                pt = "ZMIANA";
                bg = C_CYAN;
                fg = Color{8, 40, 56, 255};
            } else if (maGre && !fastRun && pokazAuto && !reczny && i == gra.nagroda && faza < F_ODSLONIECIE) {
                pt = "tu jest AUTO";
                bg = C_GREEN;
                fg = Color{8, 50, 24, 255};
            }
            Plaque(cx, FLOOR_Y + 14, pt, bg, fg);
        }

        DrawPresenter(PRES_X, PRES_Y, gPoint, mouth, blink, cheer);
        float bw = 190;
        DrawBubble(bubbleFull, bubbleChars, 652, 252, bw, PRES_X);

        DrawKonfetti();

        if (reczny && faza >= F_ODSLONIECIE && !fastRun) {
            float a = EaseOutCubic((bannerT - 0.7f) / 0.35f);
            if (a > 0.01f) {
                Color col = lastWin ? C_GOLD : Color{255, 130, 130, 255};
                float cx = DOOR_X0 + (3 * DOOR_W + 2 * DOOR_GAP) / 2.0f;
                TxtShadow(FS_TITLE, lastWin ? "WYGRANA!" : "PRZEGRANA", cx, FLOOR_Y + 52 + (1 - a) * 12, 40, Al(col, a),
                          Al(C_BLACK, a * 0.6f));
            }
        }
        if (flashA > 0.01f) DrawRectangle((int)ST_X, (int)ST_Y, (int)ST_W, (int)ST_H, Al(C_RED, flashA));
        EndMode2D();

        Camera2D camUI = {};
        camUI.offset = gOff;
        camUI.zoom = gScale;
        BeginMode2D(camUI);

        // maski: zakrywaja wychodzenie sceny poza ramke przy drzganiu
        DrawRectangle(0, 0, (int)ST_X, WIN_H, C_BG);
        DrawRectangle(0, 0, 866, (int)ST_Y, C_BG);
        DrawRectangle(0, (int)(ST_Y + ST_H), 866, WIN_H - (int)(ST_Y + ST_H), C_BG);
        DrawRectangle((int)(ST_X + ST_W), 0, 16, WIN_H, C_BG);
        DrawRectangleLinesEx(Rectangle{ST_X - 3, ST_Y - 3, ST_W + 6, ST_H + 6}, 3, Color{150, 108, 30, 255});
        DrawRectangleLinesEx(Rectangle{ST_X, ST_Y, ST_W, ST_H}, 1, Al(C_GOLD, 0.35f));

        // ---------- panel komentarza ----------
        DrawRectangleRounded(Rectangle{rPanel.x - 1, rPanel.y - 1, rPanel.width + 2, rPanel.height + 2}, 0.16f, 10, Al(C_LINE, 0.9f));
        DrawRectangleRounded(rPanel, 0.16f, 10, C_CARD);
        string l1, l2;
        Color c1 = C_TEXT, c2 = C_MUTED;
        string W = to_string(gra.wybor + 1), O = to_string(gra.otwarte + 1), Z = to_string(gra.zmieniony + 1);
        if (fastRun) {
            l1 = "Szybka symulacja: " + FmtInt(fastZrobione) + " z " + FmtInt(fastCel) + " gier";
            l2 = "Wykres po prawej pokazuje, jak wyniki zbiegają do 1/3 i 2/3.";
        } else {
            switch (faza) {
                case F_START:
                    l1 = reczny ? "Tryb: TY wybierasz drzwi. Kliknij NOWA GRA." : "Tryb: losowy gracz. Kliknij NOWA GRA.";
                    l2 = reczny ? "Potem kliknij drzwi (lub klawisz 1, 2, 3), a na końcu zdecyduj: zostać czy zmienić."
                                : "Możesz też włączyć AUTO-GRĘ albo uruchomić szybką symulację poniżej.";
                    break;
                case F_CZEKA_NA_WYBOR:
                    l1 = "Wybierz drzwi: kliknij jedne z trzech (lub naciśnij 1, 2, 3).";
                    l2 = "Auto jest ukryte — nie wiesz, gdzie jest.";
                    c1 = C_GOLD;
                    break;
                case F_WYBOR:
                    l1 = reczny ? "Wybrałeś drzwi nr " + W + "." : "Gracz losowo wybiera drzwi nr " + W + ".";
                    l2 = reczny ? "Prowadzący (wie, gdzie jest auto) zaraz otworzy drzwi z kozą."
                                : (pokazAuto ? "Auto jest za drzwiami nr " + to_string(gra.nagroda + 1) + " — gracz tego nie wie."
                                             : "Gracz nie wie, gdzie jest auto.");
                    break;
                case F_PROWADZACY:
                    l1 = "Prowadzący wie, gdzie jest auto, i otwiera drzwi nr " + O + " z kozą.";
                    l2 = "Zostały drzwi nr " + W + " (pierwszy wybór) i nr " + Z + ".";
                    break;
                case F_DECYZJA:
                    l1 = "Prowadzący otworzył drzwi nr " + O + " z kozą. Co robisz?";
                    l2 = "Zostań przy drzwiach nr " + W + " albo zmień na nr " + Z + " (przyciski poniżej lub klik w drzwi).";
                    c1 = C_GOLD;
                    break;
                default:
                    l1 = "Zostając przy drzwiach nr " + W + ": " + (gra.zostajWygrana ? "WYGRANA" : "przegrana");
                    l2 = "Zmieniając na drzwi nr " + Z + ": " + (gra.zmianaWygrana ? "WYGRANA" : "przegrana");
                    if (reczny) {
                        if (decyzja == 2) l2 += "   <- Twoja decyzja";
                        else l1 += "   <- Twoja decyzja";
                    }
                    c1 = gra.zostajWygrana ? C_GREEN : C_RED;
                    c2 = gra.zmianaWygrana ? C_GREEN : C_RED;
                    break;
            }
        }
        TxtFit(FS_BOLD, l1, rPanel.x + 18, rPanel.y + 10, 19, rPanel.width - 36, c1);
        TxtFit(FS_REG, l2, rPanel.x + 18, rPanel.y + 36, 16, rPanel.width - 36, c2);

        // ---------- przyciski ----------
        if (Button("new", rNew, "NOWA GRA (SPACJA)", C_BLUE, mozeStartowac, 16)) nowaGra();
        if (Button("mode", rMode, reczny ? "TRYB: TY WYBIERASZ DRZWI" : "TRYB: LOSOWY GRACZ",
                   reczny ? Color{160, 104, 28, 255} : C_BLUE, mozeStartowac, 16)) {
            reczny = !reczny;
            if (reczny) autoPlay = false;
            faza = F_START;
            maGre = false;
            t = 0.0f;
            for (float& o : otw) o = 0.0f;
        }
        if (Button("speed", rSpeed, "SZYBKOŚĆ ×" + to_string((int)predkosci[predkoscIdx]), C_BLUE, true, 16))
            predkoscIdx = (predkoscIdx + 1) % 4;
        if (Button("reset", rReset, "RESET WYNIKÓW", Color{128, 56, 70, 255}, true, 15)) {
            fastRun = false;
            ResetujWyniki();
            faza = F_START;
            maGre = false;
            policzona = false;
            t = 0.0f;
            for (float& o : otw) o = 0.0f;
        }
        if (faza == F_DECYZJA && !fastRun) {
            if (Button("stay", rStay, "ZOSTAJĘ (drzwi nr " + W + ")", Color{176, 108, 24, 255}, true, 18)) zapisz(1);
            if (Button("switch", rSwitch, "ZMIENIAM (drzwi nr " + Z + ")", Color{28, 128, 160, 255}, true, 18)) zapisz(2);
        } else {
            if (Button("auto", rAuto, autoPlay ? "AUTO-GRA: WŁĄCZONA" : "AUTO-GRA: WYŁĄCZONA",
                       autoPlay ? Color{40, 140, 90, 255} : C_BLUE, !fastRun && !reczny, 16))
                autoPlay = !autoPlay;
            if (Button("showauto", rShowAuto, pokazAuto ? "PODPOWIEDŹ „AUTO”: TAK" : "PODPOWIEDŹ „AUTO”: NIE", C_BLUE,
                       !reczny, 16))
                pokazAuto = !pokazAuto;
            if (reczny) {
                Txt(FS_REG, "Tryb ręczny: sam decydujesz.", 430, 654, 13, C_MUTED);
            }
        }

        if (Button("explain", rExplain, "WYJAŚNIENIE PARADOKSU", Color{128, 78, 196, 255}, true, 15)) {
            gEx.open = true; gEx.step = 0; gEx.t = 0; gEx.just = true; gBlock = true;
        }

        // ---------- szybka symulacja ----------
        Txt(FS_BOLD, "SZYBKA SYMULACJA", 12, 688, 12, C_MUTED);
        Txt(FS_REG, "  wpisz liczbę gier i kliknij SYMULUJ (bez animacji, do 100 000 000)", 142, 688, 12, C_MUTED);
        DrawRectangleRounded(Rectangle{rInput.x - 2, rInput.y - 2, rInput.width + 4, rInput.height + 4}, 0.25f, 8,
                             fokus ? C_GOLD : C_LINE);
        DrawRectangleRounded(rInput, 0.25f, 8, Color{16, 18, 36, 255});
        string poleTxt = wpis;
        if (fokus && ((int)(gTime * 2) % 2 == 0)) poleTxt += "|";
        if (CheckCollisionPointRec(mp, rInput)) gHand = true;
        Txt(FS_BOLD, poleTxt, rInput.x + 14, rInput.y + 6, 20, C_TEXT);

        if (!fastRun) {
            struct Preset { const char* n; const char* v; };
            const Preset pr[5] = {{"100", "100"}, {"1 000", "1000"}, {"10 000", "10000"}, {"50 000", "50000"}, {"1 mln", "1000000"}};
            for (int i = 0; i < 5; ++i)
                if (Button(string("pre") + to_string(i), Rectangle{190.0f + i * 70.0f, 706, 64, 36}, pr[i].n, Color{50, 58, 100, 255}, true, 14))
                    wpis = pr[i].v;
        } else {
            Rectangle pb = {190, 716, 344, 16};
            DrawRectangleRounded(pb, 0.5f, 8, Color{16, 18, 36, 255});
            float fw = (float)((double)fastZrobione / (double)max(1LL, fastCel)) * pb.width;
            if (fw > 6) DrawRectangleRounded(Rectangle{pb.x, pb.y, fw, pb.height}, 0.5f, 8, C_GREEN);
        }
        if (Button("sim", rSim, fastRun ? "STOP" : "SYMULUJ", fastRun ? Color{170, 60, 70, 255} : Color{40, 140, 90, 255},
                   fastRun || mozeStartowac, 17)) {
            if (fastRun) {
                fastRun = false;
            } else {
                long long n = wpis.empty() ? 1 : stoll(wpis);
                n = max(1LL, min(n, 100000000LL));
                wpis = to_string(n);
                fastCel = n;
                fastZrobione = 0;
                fastRun = true;
                fokus = false;
            }
        }

        // ---------- prawa kolumna ----------
        DrawStatsCard(cardA);
        DrawChart(cardB);
        bool sv = false;
        DrawHistoryCard(cardC, reczny, &sv);
        if (sv) {
            string p = ZapiszWyniki();
            toast = p.empty() ? string("Nie udało się zapisać pliku.") : "Zapisano: " + p;
            toastT = 4.5f;
        }

        if (toastT > 0.0f && !toast.empty()) {
            float a = Clamp01(toastT / 0.8f);
            int fs = FitSize(FS_BOLD, toast, 14, 900);
            float w = TextSize(FS_BOLD, toast, fs).x + 40;
            Rectangle r = {WIN_W / 2.0f - w / 2.0f, 18, w, 34};
            DrawRectangleRounded(r, 0.4f, 8, Al(Color{10, 12, 26, 245}, a));
            Txt(FS_BOLD, toast, r.x + 20, r.y + 8, fs, Al(C_GOLD, a));
        }

        {
            int act = DrawExplain();
            if (act == 1 && !fastRun) {
                gEx.open = false;
                wpis = "10000";
                fastCel = 10000; fastZrobione = 0; fastRun = true; fokus = false;
            }
        }

        EndMode2D();

        SetMouseCursor(gHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
        EndDrawing();

#ifdef MH_TEST
        for (const Step& s : script)
            if (s.f == tf && s.kind == 1) Shot(s.name);
#endif
    }

    for (int i = 0; i < 3; ++i) UnloadTexture(gDoorTex[i]);
    CloseWindow();
    return 0;
}

// ---- Wbudowane czcionki (Poppins Regular/Bold, Bungee; licencja SIL OFL), zakodowane w base64 ----
extern const char FONT_REG_B64[] =
    "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"
    "AAsAADYmNTQ2MzIWFRQGI5pHRzM0SEg000czNEdHNDNHAP//ACz/+gIdAHYAIwAPALsAAAAjAA8BdwAAAAIADwAAAAEATAFIAdsBlQADAAABFSE1Adv+cQGV"
    "TU0AAQAAAIAAlQAMAHYABgABAAAAAAAAAAAAAAAAAAMAAgAAAAAAAAAeADEAYACuAP0BSAFVAXIBkAGyAccB1QHdAfMCAwIvAj4CZQKjAr8C8QMqAz4DiQPC"
    "A84D2gPqA/4EDgRDBKcEwQT5BScFSgVgBXQFqAXBBc4F6gYEBhMGMAZGBncGlwbPBvcHOAdKB2sHgAeeB7oH0QfmB/gIBwgYCCoINwhECHoIsAjbCREJRQli"
    "CagJywnoCg4KJgozCmkKigq7CvELJws9C30Lmwu+C9AL7gwIDB4MMwx1DIEMwwzqDRINHw0rDUUNUQ2BDckN1Q3hDgsOUw5uDoYOkg6eDqoOtg7CDs0O2Q7l"
    "DvsPCA8VDyIPLw9CD1UPZw99D40PmgAAAAEAAAAEAQbECengXw889QADA+gAAAAA2KSpvgAAAADbFjbM/e39xAoHBCkAAAAHAAIAAAAAAAAB9AAAAQsAAAEq"
    "AFgBJAAjA0gAIQJuADwC9wAqAuMAJQCfACMBxgBsAcYAIQHmAD0CqwBXAMb//gInADoA0gAsAdwANQJ0AD4BQAAmAj8AMwJNADECdQAoAnQASQJ7AEkCIgAh"
    "AncAPAJ2AEkA1QAsAQgAFwIrAFQC0wBlAhsAXgIMACUD9QBIAqIAIQJlAE0DBAArAsMATQIBAE0B+ABNAwoAKwK0AE0A9gBNAhIAKQJXAE0BsABNA10ATQK/"
    "AE0DEgArAkMATQMUACsCYABNAksAOQIdACICowBLAqQAFgPQABcCbQAtAkgAEwIdAC4BpwCFApIAgwGnAG4CdQAlAt0AaQEBABECpAArAqQATQJfACsCpAAr"
    "AmwAKwFJABcCpAArAoAATQD2AD4A+P/lAgMATQD2AE0EBgBNAoAATQKAACsCpABNAqQAKwF1AE0CCgAvAWwAGgKAAEgCMQAMAzQADAHfAA0CMwAMAccAKQHO"
    "AGkBIwBkAc4ARAIHACUBmgAcAPcAEQMSACsCgwBnAoAAKwKiACECpAArAwQAKwJfACsCAQBNAmwAKwG1AAkA+wAJAr8ATQKAAE0CSwA5AgoALwIdAC4BxwAp"
    "Ah0ALgHHACkAjwATAqUATAN1AEwA2wAYANsAIAF8ABgBfAAgAV7//gGcAFMCRQAsAicATAABAAAEGv6iAGQJ7f3t+W4KBwABAAAAAAAAAAAAAAAAAAAAgAAE"
    "A1MBkAAFAAACigJYAAAASwKKAlgAAAFeADIBSAAAAAAFAAAAAAAAAAAAAAcAAAAAAAAAAAAAAABJVEZPAMAAICAmBBr+ogBkBG8CcwAAAAEAAAAAAiQCugAA"
    "ACAABAAAAAIAAAADAAAAFAADAAEAAAAUAAQAkAAAACAAIAAEAAAAfgCwANMA1wDzAQcBGQFEAVsBfCAUIBkgHiAiICb//wAAACAAsADTANcA8wEEARgBQQFa"
    "AXkgEyAYIBwgIiAm////4f+w/4//jP9x/2H/Uf8q/xX++OBj4GDgXuBb4FgAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABwBaAAMAAQQJ"
    "AAAAogAAAAMAAQQJAAEADgCiAAMAAQQJAAIADgCwAAMAAQQJAAMAPAC+AAMAAQQJAAQAHgD6AAMAAQQJAAUACgEYAAMAAQQJAAYAHgEiAEMAbwBwAHkAcgBp"
    "AGcAaAB0ACAAMgAwADIAMAAgAFQAaABlACAAUABvAHAAcABpAG4AcwAgAFAAcgBvAGoAZQBjAHQAIABBAHUAdABoAG8AcgBzACAAKABoAHQAdABwAHMAOgAv"
    "AC8AZwBpAHQAaAB1AGIALgBjAG8AbQAvAGkAdABmAG8AdQBuAGQAcgB5AC8AUABvAHAAcABpAG4AcwApAFAAbwBwAHAAaQBuAHMAUgBlAGcAdQBsAGEAcgBJ"
    "AFQARgBPADsAIABQAG8AcABwAGkAbgBzACAAUgBlAGcAdQBsAGEAcgA7ACAANAAuADAAMAA0AGIAOABQAG8AcABwAGkAbgBzACAAUgBlAGcAdQBsAGEAcgA0"
    "AC4AMAAwADQAUABvAHAAcABpAG4AcwAtAFIAZQBnAHUAbABhAHIAAAADAAAAAAAA/7UAMgAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAKABwAHgABREZMVAAI"
    "AAQAAAAA//8AAAAAAAAAAQAAAAoALAAuAANERkxUABRkZXYyAB5kZXZhAB4ABAAAAAD//wAAAAAAAAAAAAA=";

extern const char FONT_BOLD_B64[] =
    "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"
    "AAAAHwAzAGMArgD3AUIBTwFsAYoBrAHBAc8B1wHtAf0CJgI2Al8CnQK3AukDIwM2A38DuQPFA9ED4QP1BAUEOwSbBLUE6AUTBTcFTQVhBZEFqgW3BdMF7AX7"
    "BhcGLQZZBn0GrwbYBxYHKAdJB10HeweVB6sHwAfSB+EH8wgFCBIIHwhPCH8IpwjXCQkJKQlpCYsJqAnOCeUJ8gomCkcKcgqiCtgK8AsvC0wLbwuBC58LuQvP"
    "C+MMKAw0DHkMoAzIDNUM4Qz7DQcNNw18DYgNlA3BDgcOIQ46DkYOUg5eDmoOdg6BDo0OmQ6vDrwOyQ7WDuMO9w8MDyAPPA9MD1kAAAABAAAABAEG5rYWvl8P"
    "PPUAAwPoAAAAANikqcgAAAAA2xY2yf3O/aIJrQRUAAEABwACAAAAAAAAAfQAAADUAAABiABfAZsAKAOIABQCkgAsA2YAHwMeACQA3QAoAd0AVwHdAAcCFgBK"
    "AnQAOwEfABoCRAA0ARoAKAHFAAoCjAAtAXgAGwI7ACUCXQAqAqUAMQKKAFQCfQA+AhcAHAKIADYCZwA0ARwAKAFbADICJwA5ArgARAIdAD8CGgAaBDgASQLh"
    "ABACkwA+AvoAIQLXAD4CHQA+AiMAPgL6ACEC2wA+AScAPgJCABoCuQA+Ad0APgOWAD4C8AA+AxIAIQJwAD4DFAAhAowAPgJnACoCTwAYAsEAOwLaAAkEHAAZ"
    "AssAGAKfAAcCVAAyAf4AeAMiALcB/gBaAsUAHgMMAFUBIwAKAqcAHAKnAD4CXQAcAqcAHAJoABwBaAARAqcAHAKiAD4BJwAuASb/4QJqAD4BJwA+BCMAPgKi"
    "AD4CfQAcAqcAPgKnABwBrAA+Ai4AIAGWABUCogA5AnIACQNiAAQCTAAFAnj//wHwAB8B8wBbASMAPAHzAEcCfAAcAc8AGQEFAAoDEgAhAn0ARQJ9ABwC4QAQ"
    "AqcAHAL6ACECXQAcAh0APgJoABwB5QAKAUEACgLwAD4CogA+AmcAKgIuACACVAAyAfAAHwJUADIB8AAfAKcACgK3AEYDogBGAToAJgE6ADECHQAmAh0AMQII"
    "ABoB9QBkAvgAKAIfADQAAQAABBr+ogBkCZ39zvkYCa0AAQAAAAAAAAAAAAAAAAAAAIAABANpArwABQAAAooCWAAAAEsCigJYAAABXgAyAU4AAAAACAAAAAAA"
    "AAAAAAAHAAAAAAAAAAAAAAAASVRGTwCgACAgJgQa/qIAZARvAnMAAAABAAAAAAIuAsEAAAAgAAQAAAACAAAAAwAAABQAAwABAAAAFAAEAJAAAAAgACAABAAA"
    "AH4AsADTANcA8wEHARkBRAFbAXwgFCAZIB4gIiAm//8AAAAgALAA0wDXAPMBBAEYAUEBWgF5IBMgGCAcICIgJv///+H/sP+P/4z/cf9h/1H/Kv8V/vjgY+Bg"
    "4F7gW+BYAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAcAWgADAAEECQAAAKIAAAADAAEECQABAA4AogADAAEECQACAAgAsAADAAEECQAD"
    "ADYAuAADAAEECQAEABgA7gADAAEECQAFAAoBBgADAAEECQAGABgBEABDAG8AcAB5AHIAaQBnAGgAdAAgADIAMAAyADAAIABUAGgAZQAgAFAAbwBwAHAAaQBu"
    "AHMAIABQAHIAbwBqAGUAYwB0ACAAQQB1AHQAaABvAHIAcwAgACgAaAB0AHQAcABzADoALwAvAGcAaQB0AGgAdQBiAC4AYwBvAG0ALwBpAHQAZgBvAHUAbgBk"
    "AHIAeQAvAFAAbwBwAHAAaQBuAHMAKQBQAG8AcABwAGkAbgBzAEIAbwBsAGQASQBUAEYATwA7ACAAUABvAHAAcABpAG4AcwAgAEIAbwBsAGQAOwAgADQALgAw"
    "ADAANABiADgAUABvAHAAcABpAG4AcwAgAEIAbwBsAGQANAAuADAAMAA0AFAAbwBwAHAAaQBuAHMALQBCAG8AbABkAAAAAwAAAAAAAP+1ADIAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAABAAAACgAcAB4AAURGTFQACAAEAAAAAP//AAAAAAAAAAEAAAAKACwALgADREZMVAAUZGV2MgAeZGV2YQAeAAQAAAAA//8AAAAAAAAAAAAA";

extern const char FONT_TITLE_B64[] =
    "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"
    "AgYAGwAAAAH/LgLtANEDxQANAAATJSYmNTU0FwUWFhUVFK/+oxMRIgFdExEC8SsCDxNgJQQrAg8TYCUAAAEAAABhAH0ABwBlAAQAAQAAAAAAAAAAAAAAAAAD"
    "AAEAAAAAAAAANQBpAIwAsADWAPcBHwFEAWkBkQG/Ad0CFAI5Am8CmwLWAw8DRwNkA48DzwQYBIMEvgTkBRoFPAV0BbMF2QYGBkUGbgbCBwEHFQcqB1QHfged"
    "B7sH/whDCJwJCwkpCV8JqQm9CdwJ6An0ChsKVApuCokKnwr+C0oLbguRC8ML9QwnDG4MtAy9DMUMzQzVDN0M5QztDPUM/Q0FDQ0NFQ0dDSUNLQ01DT0NRQ1N"
    "DVUNXQ1lDW0NdQ19DYUNjQ2oAAEAAAACAADFmHPHXw889QADA+gAAAAA4n2/agAAAADifb90/aD8bAeYBmQAAAAGAAIAAAAAAAAD6ABkAOEAAALaADYC1QBF"
    "AnQAOgLqAEUCjgBFAmoARQLEADoC9wBFAl0ANQKwACkC6gBFArcARQNRAEUC8QBFAuEAOgKqAEUC4QA6AucARQKKADMCkAAcAuoAQALaADYDPwA8AuEAPALB"
    "AC0ClAAuAtcAOgJZADACfwAoAn0AMQLFAD8ChwA3ArUAOgJzACYCxwAvArUALwLeAEYBpAAoAa0AMAGtAC0BrQAwAa0ALQHHABQBxwAtAu4AIwQHACoBZAA3"
    "AoYALQLOADYBgAA3AYAANwGAADcBgAA3AagAUQKQAC0BWwABAVsAAQFbAEEDYwA8Av4AJQLeAEYC3gBGAsoAUALKAFoCygAhAt4ARgKKADMB9AAoAtoANgLV"
    "AEUCdAA6AuoARQKOAEUCagBFAsQAOgL3AEUCXQA1ArAAKQLqAEUCtwBFA1EARQLxAEUC4QA6AqoARQLhADoC5wBFAooAMwKQABwC6gBAAtoANgM/ADwC4QA8"
    "AsEALQKUAC4AAP8uAAEAAAP8/tQAAAWJ/aD9nweYA+gAAAAAAAAAAAAAAAAAAABhAAQDIQGQAAUAAAKKAlgAAABLAooCWAAAAV4AMgEsAAAAAAAAAAAAAAAA"
    "AAAAAQAAAAAAAAAAAAAAAGRqciAAwAAgAH4D/P7UAAAGbwOfAAAAAQAAAAAB9ALQAAAAIAADAAAAAgAAAAMAAAAUAAMAAQAAABQABACQAAAAEAAQAAMAAAAv"
    "ADkAQABaAF8AegB+//8AAAAgADAAOgBBAFsAYAB7//8AAP/sAAD/wQAA/+UAAAABABAAAAAsAAAANgAAADwAAAABADcAMQAuAEQALwA9ADAAKAApADIAPgA0"
    "ACcAMwA5ADUANgBAAD8AQQA4ADwAKgA6ACsAQgAmACwAOwAtAEMAAAAHAFoAAwABBAkAAACWAAAAAwABBAkAAQAMAJYAAwABBAkAAgAOAKIAAwABBAkAAwAy"
    "ALAAAwABBAkABAAcAOIAAwABBAkABQAaAP4AAwABBAkABgAcARgAQwBvAHAAeQByAGkAZwBoAHQAIAAyADAAMgAzACAAVABoAGUAIABCAHUAbgBnAGUAZQAg"
    "AFAAcgBvAGoAZQBjAHQAIABBAHUAdABoAG8AcgBzACAAKABoAHQAdABwAHMAOgAvAC8AZwBpAHQAaAB1AGIALgBjAG8AbQAvAGQAagByAHIAYgAvAEIAdQBu"
    "AGcAZQBlACkAQgB1AG4AZwBlAGUAUgBlAGcAdQBsAGEAcgAyAC4AMAAwADAAOwBkAGoAcgAgADsAQgB1AG4AZwBlAGUALQBSAGUAZwB1AGwAYQByAEIAdQBu"
    "AGcAZQBlACAAUgBlAGcAdQBsAGEAcgBWAGUAcgBzAGkAbwBuACAAMgAuADAAMAAwAEIAdQBuAGcAZQBlAC0AUgBlAGcAdQBsAGEAcgAAAAMAAAAAAAD/tQAy"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAB//8ADwABAAAADAAAAAAAAAACAAMAAgAcAAEARABEAAEARgBfAAEAAAABAAAACgAcAB4AAURGTFQACAAEAAAAAP//"
    "AAAAAAAAAAEAAAAKACYAKAACREZMVAAObGF0bgAYAAQAAAAA//8AAAAAAAAAAAAAAAAD6ABkA1wAjACMAIwAjACMAIwAjACMAIwAjACMAIwAjACMAH0AjAB9"
    "AIwAjACMAIwAjACMAIwAjACMAH0AjACMAIwAjACMAIwAjAB9AH0C9QGNADwAPAA8ADwAPAA8AIwAhwCMAIwAjAJJAkkA5gDmAIwAjAAyADIAMgAxAIwAywEB"
    "AMIAwgCPAXgAMv+XAIwAjACMAIwAjACMAIwAjACMAIwAjACMAIwAjAB9AIwAfQCMAIwAjACMAIwAjACMAIwAjP+XAAEQAAH0/gwAAAWb/Pj8+AbwAAAAAQAA"
    "AAAAAAAAAAAAAAAB";

