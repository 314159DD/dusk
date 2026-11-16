#include <windows.h>
#include <delayimp.h>
#include <math.h>
#include <gdiplus.h>
#include "dusk.h"

using namespace Gdiplus;

static const DWORD C_PAPER = 0xFF0B0D12, C_INK = 0xFFEEF0F4, C_SOFT = 0xFFB4BAC6, C_MUTED = 0xFF8E95A3;
static const DWORD C_STRONG = 0x24FFFFFF, C_WELL = 0x0FFFFFFF, C_GLOW = 0xFF8FA2FF, C_BRAND = 0xFF2B3A8C;
static const DWORD C_GLASS = 0xA31A1E27, C_CHROME = 0xC70E1116, C_WARN = 0xFFF0B26B, C_SURF = 0xFF1A1E27;
static const DWORD C_SECOND = 0xB81A1E27, C_SWITCHOFF = 0x24EEF0F4, C_RING = 0x33000000;

enum { K_NONE = 0, K_ENABLED, K_TILE, K_SAVE, K_FILTER, K_NOTE, K_KSL, K_BSL, K_AUTO, K_QUIT, K_RESET };

#define WM_ENDEDIT (WM_APP + 2)
#define WM_CHECKACTIVE (WM_APP + 3)

struct Sel { int kind, mon, idx; };
struct Hit { float x, y, w, h; Sel s; };

struct Surf {
    HDC dc;
    HBITMAP bm;
    HGDIOBJ old;
    BYTE* bits;
    int w, h;
    Bitmap* bmp;
};

struct Fonts {
    FontFamily *sans, *semi, *med, *mono;
    Font *t20, *n14, *r13, *r12, *m12, *m13;
    wchar_t monoName[40];
};

static ULONG_PTR gToken;
static int gRef;
static Bitmap* gMeasureBmp;
static Graphics* gMeasure;

static void gdipAcquire()
{
    if (gRef++ == 0) {
        GdiplusStartupInput in;
        GdiplusStartup(&gToken, &in, NULL);
        gMeasureBmp = new Bitmap(1, 1, PixelFormat32bppPARGB);
        gMeasure = new Graphics(gMeasureBmp);
    }
}

static void gdipRelease()
{
    if (--gRef == 0) {
        delete gMeasure;
        delete gMeasureBmp;
        gMeasure = NULL;
        gMeasureBmp = NULL;
        GdiplusShutdown(gToken);
        __FUnloadDelayLoadedDLL2("gdiplus.dll");
    }
}

static FontFamily* fam(const wchar_t* n)
{
    FontFamily* f = new FontFamily(n);
    if (f->IsAvailable()) return f;
    delete f;
    return NULL;
}

static FontFamily* firstFamily(const wchar_t* const* names, int n, const wchar_t** which)
{
    for (int i = 0; i < n; i++) {
        FontFamily* f = fam(names[i]);
        if (f) { *which = names[i]; return f; }
    }
    return NULL;
}

static Font* mk(FontFamily* f, float size, bool bold)
{
    int style = FontStyleRegular;
    if (bold && f->IsStyleAvailable(FontStyleBold)) style = FontStyleBold;
    return new Font(f, size, style, UnitPixel);
}

static void fontsInit(Fonts& F)
{
    static const wchar_t* const sn[] = { L"Geist", L"Segoe UI Variable Text", L"Segoe UI" };
    static const wchar_t* const mn[] = { L"Geist Mono", L"Cascadia Mono", L"Consolas" };
    const wchar_t* base = L"";
    F.sans = firstFamily(sn, 3, &base);
    if (!F.sans) { F.sans = FontFamily::GenericSansSerif()->Clone(); base = L"Arial"; }
    wchar_t b[96];
    wsprintfW(b, L"%s SemiBold", base);
    F.semi = fam(b);
    wsprintfW(b, L"%s Medium", base);
    F.med = fam(b);
    const wchar_t* mb = L"Consolas";
    F.mono = firstFamily(mn, 3, &mb);
    if (!F.mono) F.mono = FontFamily::GenericMonospace()->Clone();
    lstrcpynW(F.monoName, mb, 40);
    F.t20 = F.semi ? mk(F.semi, 20, false) : mk(F.sans, 20, true);
    F.n14 = F.semi ? mk(F.semi, 14, false) : mk(F.sans, 14, true);
    F.r13 = F.med ? mk(F.med, 13, false) : mk(F.sans, 13, false);
    F.r12 = mk(F.sans, 12, false);
    F.m12 = mk(F.mono, 12, false);
    F.m13 = mk(F.mono, 13, false);
}

static void fontsFree(Fonts& F)
{
    delete F.t20; delete F.n14; delete F.r13; delete F.r12; delete F.m12; delete F.m13;
    delete F.sans; delete F.semi; delete F.med; delete F.mono;
    ZeroMemory(&F, sizeof(F));
}

static bool surfNew(Surf& s, int w, int h)
{
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    s.w = w;
    s.h = h;
    s.dc = CreateCompatibleDC(NULL);
    s.bits = NULL;
    s.bm = CreateDIBSection(s.dc, &bi, DIB_RGB_COLORS, (void**)&s.bits, NULL, 0);
    s.old = SelectObject(s.dc, s.bm);
    s.bmp = new Bitmap(w, h, w * 4, PixelFormat32bppPARGB, s.bits);
    return s.bits != NULL;
}

static void surfFree(Surf& s)
{
    delete s.bmp;
    if (s.dc) {
        SelectObject(s.dc, s.old);
        DeleteObject(s.bm);
        DeleteDC(s.dc);
    }
    ZeroMemory(&s, sizeof(s));
}

static void rrPath(GraphicsPath& p, float x, float y, float w, float h, float r)
{
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static void fillRR(Graphics& g, DWORD c, float x, float y, float w, float h, float r)
{
    GraphicsPath p;
    rrPath(p, x, y, w, h, r);
    SolidBrush b((Color)c);
    g.FillPath(&b, &p);
}

static void strokeRR(Graphics& g, DWORD c, float x, float y, float w, float h, float r)
{
    GraphicsPath p;
    rrPath(p, x + 0.5f, y + 0.5f, w - 1, h - 1, r - 0.5f);
    Pen pen((Color)c, 1.0f);
    g.DrawPath(&pen, &p);
}

static void edgeRR(Graphics& g, float x, float y, float w, float h, float r)
{
    LinearGradientBrush lb(PointF(x, y), PointF(x, y + h), Color(0x29FFFFFF), Color(0x0FFFFFFF));
    Color cols[3] = { Color(0x29FFFFFF), Color(0x0FFFFFFF), Color(0x0FFFFFFF) };
    REAL pos[3] = { 0.0f, 0.35f, 1.0f };
    lb.SetInterpolationColors(cols, pos, 3);
    Pen pen(&lb, 1.0f);
    GraphicsPath p;
    rrPath(p, x + 0.5f, y + 0.5f, w - 1, h - 1, r - 0.5f);
    g.DrawPath(&pen, &p);
}

static void radial(Graphics& g, float cx, float cy, float rx, float ry, DWORD c0, DWORD c1)
{
    GraphicsPath ep;
    ep.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
    PathGradientBrush pb(&ep);
    pb.SetCenterColor((Color)c0);
    Color sc((ARGB)c1);
    int n = 1;
    pb.SetSurroundColors(&sc, &n);
    g.FillPath(&pb, &ep);
}

static float tw(const wchar_t* t, Font* f)
{
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF lay(0, 0, 4000, 100), bb;
    gMeasure->MeasureString(t, -1, f, lay, &sf, &bb);
    return bb.Width;
}

static void txt(Graphics& g, const wchar_t* t, Font* f, DWORD c, float x, float y, float w, float h, StringAlignment al, bool trim = false)
{
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    sf.SetAlignment(al);
    sf.SetLineAlignment(StringAlignmentCenter);
    if (trim) { sf.SetTrimming(StringTrimmingEllipsisCharacter); sf.SetFormatFlags(StringFormatFlagsNoWrap); }
    SolidBrush b((Color)c);
    g.DrawString(t, -1, f, RectF(x, y, w, h), &sf, &b);
}

static struct PanelState {
    HWND hwnd, edit;
    WNDPROC oldEdit;
    int editMon;
    float s;
    int W, H;
    float logH;
    Surf sf;
    BYTE* bg;
    Fonts F;
    wchar_t hint[12][160];
    int nhint;
    wchar_t warn[6][160];
    int nwarn;
    bool save[MAXMON];
    Sel hover, press;
    bool drag;
    Sel dragSel;
    bool tracking;
    bool autostart;
    DWORD lastHidden;
    DWORD animStart;
    bool anim;
    int baseX, baseY;
    int curAlpha, curDy;
    HBRUSH editBrush;
    HFONT editFont;
} P;

static Hit hits[256];
static int nhits;

static bool selEq(const Sel& a, const Sel& b) { return a.kind == b.kind && a.mon == b.mon && a.idx == b.idx; }
static bool hov(int kind, int mon, int idx) { Sel s = { kind, mon, idx }; return selEq(P.hover, s) && kind != K_NONE; }

static void addHit(float x, float y, float w, float h, int kind, int mon, int idx)
{
    if (nhits >= 256) return;
    Hit& t = hits[nhits++];
    t.x = x; t.y = y; t.w = w; t.h = h;
    t.s.kind = kind; t.s.mon = mon; t.s.idx = idx;
}

static const wchar_t* hotkeyOf(const wchar_t* action)
{
    for (int i = 0; i < g_set.nhk; i++)
        if (lstrcmpW(g_set.hk[i].action, action) == 0) return g_set.hk[i].keys;
    return L"";
}

static int wrapPara(const wchar_t* t, Font* f, float width, wchar_t out[][160], int n, int cap)
{
    wchar_t cur[160];
    cur[0] = 0;
    const wchar_t* p = t;
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) break;
        wchar_t word[100];
        int k = 0;
        while (*p && *p != ' ') { if (k < 99) word[k++] = *p; p++; }
        word[k] = 0;
        wchar_t test[260];
        if (cur[0]) wsprintfW(test, L"%s %s", cur, word);
        else lstrcpynW(test, word, 260);
        if (cur[0] && tw(test, f) > width) {
            if (n < cap) lstrcpynW(out[n++], cur, 160);
            lstrcpynW(cur, word, 160);
        } else {
            lstrcpynW(cur, test, 160);
        }
    }
    if (n < cap) lstrcpynW(out[n++], cur, 160);
    return n;
}

static void panelLayoutText()
{
    wchar_t b[300];
    P.nhint = 0;
    wsprintfW(b, L"Brightness: %s, %s", hotkeyOf(L"BrightnessUp"), hotkeyOf(L"BrightnessDown"));
    P.nhint = wrapPara(b, P.F.r12, 340, P.hint, P.nhint, 12);
    wsprintfW(b, L"Temperature: %s, %s", hotkeyOf(L"Warmer"), hotkeyOf(L"Cooler"));
    P.nhint = wrapPara(b, P.F.r12, 340, P.hint, P.nhint, 12);
    wsprintfW(b, L"On or off: %s", hotkeyOf(L"Toggle"));
    P.nhint = wrapPara(b, P.F.r12, 340, P.hint, P.nhint, 12);
    P.nhint = wrapPara(L"Keys act on the screen under the mouse. Add Ctrl for all screens.", P.F.r12, 340, P.hint, P.nhint, 12);
    P.nwarn = 0;
    if (g_nfailed > 0) {
        wchar_t w[600];
        lstrcpyW(w, L"Could not register: ");
        for (int i = 0; i < g_nfailed; i++) {
            if (i) lstrcatW(w, L", ");
            lstrcatW(w, g_failed[i]);
        }
        lstrcatW(w, L". Another program holds these keys.");
        P.nwarn = wrapPara(w, P.F.r12, 340, P.warn, 0, 6);
    }
    P.logH = 170.0f + g_nmon * 245.0f + 18.0f * P.nhint + (P.nwarn ? 8.0f + 18.0f * P.nwarn : 0.0f);
}

static void drawBody(Graphics& g)
{
    float bx = 16, by = 16, bw = 392, bh = P.logH - 32;
    float c = 0;
    for (int d = 26; d >= -24; d--) {
        float A = 0.55f / (1.0f + expf(1.702f * d / 10.0f));
        float a = (A - c) / (1.0f - c);
        if (a <= 0.002f) continue;
        c = A;
        GraphicsPath p;
        rrPath(p, bx - d, by + 8 - d, bw + 2 * d, bh + 2 * d, 28.0f + d);
        SolidBrush br(Color((BYTE)(a * 255.0f + 0.5f), 0, 0, 0));
        g.FillPath(&br, &p);
    }
    fillRR(g, C_PAPER, bx, by, bw, bh, 28);
    GraphicsPath clip;
    rrPath(clip, bx, by, bw, bh, 28);
    g.SetClip(&clip);
    radial(g, bx + 0.12f * bw, by, 0.9f * bw, 0.55f * bh, 0x3B8FA2FF, 0x008FA2FF);
    radial(g, bx + bw, by + bh, 0.8f * bw, 0.5f * bh, 0x693A4150, 0x003A4150);
    g.ResetClip();
    edgeRR(g, bx, by, bw, bh, 28);
}

static void drawSwitch(Graphics& g, float x, float y, bool on, int kind)
{
    fillRR(g, on ? C_BRAND : C_SWITCHOFF, x, y, 46, 28, 14);
    SolidBrush w(Color(255, 255, 255, 255));
    g.FillEllipse(&w, x + 3 + (on ? 18.0f : 0.0f), y + 3, 22.0f, 22.0f);
    addHit(x, y, 46, 28, kind, 0, 0);
}

static void drawTile(Graphics& g, float x, float y, float w, float h, const wchar_t* label, Font* f, bool active, bool armed, bool hover, int kind, int mon, int idx)
{
    DWORD bg = C_WELL, bd = C_STRONG, fg = C_SOFT;
    if (hover) { bg = C_SURF; fg = C_INK; }
    if (active) { bg = C_BRAND; bd = C_BRAND; fg = C_INK; }
    if (armed) { bd = C_GLOW; fg = C_INK; }
    fillRR(g, bg, x, y, w, h, 14);
    strokeRR(g, bd, x, y, w, h, 14);
    txt(g, label, f, fg, x, y, w, h, StringAlignmentCenter);
    addHit(x, y, w, h, kind, mon, idx);
}

static void drawSlider(Graphics& g, float x, float y, float w, float frac, bool dragging, int kind, int mon)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fillRR(g, C_WELL, x, y + 8, w, 6, 3);
    float tx = x + 11 + frac * (w - 22);
    fillRR(g, C_GLOW, x, y + 8, tx - x, 6, 3);
    float r = dragging ? 11.0f * 0.92f : 11.0f;
    SolidBrush wb(Color(255, 255, 255, 255));
    g.FillEllipse(&wb, tx - r, y + 11 - r, r * 2, r * 2);
    Pen pen(Color(C_RING), 1.0f);
    g.DrawEllipse(&pen, tx - r + 0.5f, y + 11 - r + 0.5f, r * 2 - 1, r * 2 - 1);
    addHit(x, y, w, 22, kind, mon, 0);
}

static void drawCard(Graphics& g, int mi, float y0)
{
    const float x = 36, cx = 61, cw = 302;
    fillRR(g, C_GLASS, x, y0, 352, 233, 22);
    edgeRR(g, x, y0, 352, 233, 22);

    MonSetting* st = ctlGet(mi);
    Monitor& m = g_mon[mi];
    float ry = y0 + 25;

    txt(g, m.name, P.F.n14, C_INK, cx, ry, 200, 19, StringAlignmentNear);
    float nameW = tw(m.name, P.F.n14);
    wchar_t detail[48];
    wsprintfW(detail, L"%d \x00D7 %d", m.bounds.right - m.bounds.left, m.bounds.bottom - m.bounds.top);
    float detW = tw(detail, P.F.m12);
    txt(g, detail, P.F.m12, C_MUTED, cx + cw - detW - 20, ry, detW + 20, 19, StringAlignmentFar);

    float nx = cx + nameW + 10;
    float nw = cx + cw - detW - 10 - nx;
    if (nw < 0) nw = 0;
    bool editing = P.edit && P.editMon == mi;
    if (!editing) {
        bool has = st->note[0] != 0;
        txt(g, has ? st->note : L"Add note", P.F.m12, has ? C_MUTED : 0x738E95A3, nx, ry, nw, 19, StringAlignmentNear, true);
    } else {
        SolidBrush ul((Color)C_GLOW);
        g.FillRectangle(&ul, nx, ry + 18, nw, 1.0f);
    }
    addHit(nx, ry, nw, 19, K_NOTE, mi, 0);

    float ty = ry + 19 + 14;
    for (int i = 0; i < 3; i++) {
        wchar_t lab[4] = { (wchar_t)('1' + i), 0 };
        drawTile(g, cx + i * 34.0f, ty, 28, 28, lab, P.F.m12, ctlPresetActive(mi, i), P.save[mi], hov(K_TILE, mi, i), K_TILE, mi, i);
    }
    {
        float sx = cx + 3 * 34.0f;
        bool hv = hov(K_SAVE, mi, 0);
        if (hv) fillRR(g, C_WELL, sx, ty, 32, 28, 14);
        DWORD fg = P.save[mi] ? C_GLOW : (hv ? C_INK : C_SOFT);
        Pen pen((Color)fg, 1.5f);
        pen.SetLineJoin(LineJoinRound);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        float ox = sx + 8, oy = ty + 6;
        GraphicsPath p;
        p.StartFigure();
        p.AddLine(ox + 2.5f, oy + 2.5f, ox + 10.5f, oy + 2.5f);
        p.AddLine(ox + 10.5f, oy + 2.5f, ox + 13.5f, oy + 5.5f);
        p.AddLine(ox + 13.5f, oy + 5.5f, ox + 13.5f, oy + 13.5f);
        p.AddLine(ox + 13.5f, oy + 13.5f, ox + 2.5f, oy + 13.5f);
        p.CloseFigure();
        p.StartFigure();
        p.AddLine(ox + 5.5f, oy + 2.5f, ox + 5.5f, oy + 6);
        p.AddLine(ox + 5.5f, oy + 6, ox + 9.5f, oy + 6);
        p.AddLine(ox + 9.5f, oy + 6, ox + 9.5f, oy + 2.5f);
        p.StartFigure();
        p.AddLine(ox + 5, oy + 13.5f, ox + 5, oy + 9.5f);
        p.AddLine(ox + 5, oy + 9.5f, ox + 11, oy + 9.5f);
        p.AddLine(ox + 11, oy + 9.5f, ox + 11, oy + 13.5f);
        g.DrawPath(&pen, &p);
        addHit(sx, ty, 32, 28, K_SAVE, mi, 0);
    }
    {
        const wchar_t* lab = L"Blue light filter";
        float fw = tw(lab, P.F.r12) + 26;
        drawTile(g, cx + cw - fw, ty, fw, 28, lab, P.F.r12, ctlFilterActive(mi), P.save[mi], hov(K_FILTER, mi, 0), K_FILTER, mi, 0);
    }

    float ly = ty + 28 + 18;
    wchar_t v[32];
    txt(g, L"Temperature", P.F.r13, C_SOFT, cx, ly, 200, 17, StringAlignmentNear);
    wsprintfW(v, L"%d K", st->kelvin);
    txt(g, v, P.F.m13, C_INK, cx + cw - 120, ly, 120, 17, StringAlignmentFar);
    drawSlider(g, cx, ly + 17 + 6, cw, (st->kelvin - 1200) / 7800.0f, P.drag && P.dragSel.kind == K_KSL && P.dragSel.mon == mi, K_KSL, mi);

    float by = ly + 17 + 6 + 22 + 14;
    txt(g, L"Brightness", P.F.r13, C_SOFT, cx, by, 200, 17, StringAlignmentNear);
    wsprintfW(v, L"%d %%", st->brightness);
    txt(g, v, P.F.m13, C_INK, cx + cw - 120, by, 120, 17, StringAlignmentFar);
    drawSlider(g, cx, by + 17 + 6, cw, (st->brightness - 10) / 90.0f, P.drag && P.dragSel.kind == K_BSL && P.dragSel.mon == mi, K_BSL, mi);
}

static void drawCapsule(Graphics& g, float x, float y, float w, float h, const wchar_t* label, bool secondary, bool hover, int kind)
{
    if (secondary) {
        fillRR(g, hover ? C_SURF : C_SECOND, x, y, w, h, 16);
        strokeRR(g, C_STRONG, x, y, w, h, 16);
        txt(g, label, P.F.r13, C_INK, x, y, w, h, StringAlignmentCenter);
    } else {
        if (hover) fillRR(g, C_WELL, x, y, w, h, 16);
        txt(g, label, P.F.r13, hover ? C_INK : C_SOFT, x, y, w, h, StringAlignmentCenter);
    }
    addHit(x, y, w, h, kind, 0, 0);
}

static void panelRender()
{
    CopyMemory(P.sf.bits, P.bg, (size_t)P.W * P.H * 4);
    Graphics g(P.sf.bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.ScaleTransform(P.s, P.s);
    nhits = 0;

    txt(g, L"Dusk", P.F.t20, C_INK, 40, 38, 150, 28, StringAlignmentNear);
    drawSwitch(g, 338, 38, g_set.enabled, K_ENABLED);
    txt(g, g_set.enabled ? L"On" : L"Paused", P.F.r12, C_MUTED, 200, 38, 128, 28, StringAlignmentFar);

    for (int i = 0; i < g_nmon; i++) drawCard(g, i, 82.0f + i * 245.0f);

    float y = 70.0f + g_nmon * 245.0f + 16;
    for (int i = 0; i < P.nhint; i++) {
        txt(g, P.hint[i], P.F.r12, C_MUTED, 42, y, 340, 18, StringAlignmentNear);
        y += 18;
    }
    if (P.nwarn) {
        y += 8;
        for (int i = 0; i < P.nwarn; i++) {
            txt(g, P.warn[i], P.F.r12, C_WARN, 42, y, 340, 18, StringAlignmentNear);
            y += 18;
        }
    }
    y += 16;
    drawSwitch(g, 40, y + 2, P.autostart, K_AUTO);
    txt(g, L"Start with Windows", P.F.r13, C_SOFT, 96, y, 220, 32, StringAlignmentNear);
    float rw = tw(L"Reset", P.F.r13) + 30;
    float qw = tw(L"Quit", P.F.r13) + 28;
    drawCapsule(g, 388 - rw, y, rw, 32, L"Reset", true, hov(K_RESET, 0, 0), K_RESET);
    drawCapsule(g, 388 - rw - 6 - qw, y, qw, 32, L"Quit", false, hov(K_QUIT, 0, 0), K_QUIT);
}

static void panelFree()
{
    if (P.edit) { DestroyWindow(P.edit); P.edit = NULL; }
    if (P.editBrush) { DeleteObject(P.editBrush); P.editBrush = NULL; }
    if (P.editFont) { DeleteObject(P.editFont); P.editFont = NULL; }
    if (P.bg) { HeapFree(GetProcessHeap(), 0, P.bg); P.bg = NULL; }
    surfFree(P.sf);
    fontsFree(P.F);
}

static bool panelAlloc(float s)
{
    if (P.sf.dc || P.F.sans) panelFree();
    P.s = s;
    ZeroMemory(P.save, sizeof(P.save));
    P.hover.kind = P.press.kind = P.dragSel.kind = K_NONE;
    P.drag = false;
    fontsInit(P.F);
    panelLayoutText();
    P.W = (int)ceilf(424 * s);
    P.H = (int)ceilf(P.logH * s);
    if (!surfNew(P.sf, P.W, P.H)) return false;
    {
        Graphics g(P.sf.bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform(s, s);
        drawBody(g);
    }
    size_t bytes = (size_t)P.W * P.H * 4;
    P.bg = (BYTE*)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (P.bg) CopyMemory(P.bg, P.sf.bits, bytes);
    return P.bg != NULL;
}

static void panelPresent(int alpha, int dy)
{
    if (!P.hwnd) return;
    P.curAlpha = alpha;
    P.curDy = dy;
    POINT pt = { P.baseX, P.baseY + dy };
    SIZE sz = { P.W, P.H };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
    HDC sd = GetDC(NULL);
    UpdateLayeredWindow(P.hwnd, sd, &pt, &sz, P.sf.dc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, sd);
}

static int hitAt(float lx, float ly)
{
    for (int i = 0; i < nhits; i++) {
        const Hit& h = hits[i];
        if (lx >= h.x && lx < h.x + h.w && ly >= h.y && ly < h.y + h.h) return i;
    }
    return -1;
}

static void sliderSet(const Sel& s, float lx)
{
    for (int i = 0; i < nhits; i++) {
        const Hit& h = hits[i];
        if (h.s.kind != s.kind || h.s.mon != s.mon) continue;
        float frac = (lx - (h.x + 11)) / (h.w - 22);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        if (s.kind == K_KSL) {
            float v = 1200 + frac * 7800;
            ctlSet(s.mon, ((int)floorf(v / 50.0f + 0.5f)) * 50, NOVAL);
        } else {
            ctlSet(s.mon, NOVAL, (int)floorf(10 + frac * 90 + 0.5f));
        }
        return;
    }
}

static void endEdit(bool commit, bool refocus)
{
    if (!P.edit) return;
    wchar_t t[128];
    GetWindowTextW(P.edit, t, 128);
    HWND e = P.edit;
    int mon = P.editMon;
    P.edit = NULL;
    DestroyWindow(e);
    if (commit && mon < g_nmon) ctlSetNote(mon, t);
    else if (P.hwnd) { panelRender(); panelPresent(P.curAlpha, P.curDy); }
    if (refocus && P.hwnd) SetForegroundWindow(P.hwnd);
}

static LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN && w == VK_RETURN) { if (P.hwnd) PostMessageW(P.hwnd, WM_ENDEDIT, 1, 0); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { if (P.hwnd) PostMessageW(P.hwnd, WM_ENDEDIT, 0, 0); return 0; }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    if (m == WM_KILLFOCUS) {
        LRESULT r = CallWindowProcW(P.oldEdit, h, m, w, l);
        if (P.hwnd) PostMessageW(P.hwnd, WM_ENDEDIT, 1, 1);
        return r;
    }
    return CallWindowProcW(P.oldEdit, h, m, w, l);
}

static void beginEdit(int mi, float nx, float ny, float nw, float nh)
{
    if (P.edit) endEdit(true, false);
    if (!P.hwnd) return;
    if (!P.editFont) {
        LOGFONTW lf;
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -(int)(12.0f * P.s + 0.5f);
        lf.lfQuality = CLEARTYPE_QUALITY;
        lstrcpynW(lf.lfFaceName, P.F.monoName, 32);
        P.editFont = CreateFontIndirectW(&lf);
        P.editBrush = CreateSolidBrush(RGB(0x15, 0x18, 0x20));
    }
    HDC sd = GetDC(NULL);
    HGDIOBJ old = SelectObject(sd, P.editFont);
    TEXTMETRICW tm;
    GetTextMetricsW(sd, &tm);
    SelectObject(sd, old);
    ReleaseDC(NULL, sd);
    RECT wr;
    GetWindowRect(P.hwnd, &wr);
    int h = tm.tmHeight;
    int px = wr.left + (int)(nx * P.s);
    int pw = (int)(nw * P.s);
    int py = wr.top + (int)((ny + nh / 2) * P.s) - h / 2;
    wchar_t cur[128];
    lstrcpynW(cur, ctlGet(mi)->note, 128);
    P.editMon = mi;
    P.edit = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"EDIT", cur, WS_POPUP | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        px, py, pw, h, P.hwnd, NULL, g_inst, NULL);
    if (!P.edit) return;
    SendMessageW(P.edit, WM_SETFONT, (WPARAM)P.editFont, TRUE);
    SendMessageW(P.edit, EM_SETLIMITTEXT, 40, 0);
    P.oldEdit = (WNDPROC)SetWindowLongPtrW(P.edit, GWLP_WNDPROC, (LONG_PTR)editProc);
    SetFocus(P.edit);
    SendMessageW(P.edit, EM_SETSEL, 0, -1);
    panelRender();
    panelPresent(P.curAlpha, P.curDy);
}

static void fire(const Sel& s)
{
    switch (s.kind) {
    case K_ENABLED: ctlSetEnabled(!g_set.enabled); break;
    case K_AUTO:
        autostartSet(!P.autostart);
        P.autostart = autostartGet();
        panelRender();
        panelPresent(P.curAlpha, P.curDy);
        break;
    case K_QUIT: PostMessageW(g_main, WM_CLOSE, 0, 0); break;
    case K_RESET: ctlResetAll(); break;
    case K_SAVE:
        P.save[s.mon] = !P.save[s.mon];
        panelRender();
        panelPresent(P.curAlpha, P.curDy);
        break;
    case K_TILE:
    case K_FILTER:
        if (P.save[s.mon]) {
            P.save[s.mon] = false;
            if (s.kind == K_TILE) ctlSavePreset(s.mon, s.idx);
            else ctlSaveFilter(s.mon);
        } else {
            if (s.kind == K_TILE) ctlApplyPreset(s.mon, s.idx);
            else ctlToggleFilter(s.mon);
        }
        break;
    }
}

static void redraw()
{
    panelRender();
    panelPresent(P.curAlpha, P.curDy);
}

static LRESULT CALLBACK panelProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (!P.hwnd || h != P.hwnd) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) {
            if (l && GetWindow((HWND)l, GW_OWNER) == h) return 0;
            uiClosePanel();
        }
        return 0;
    case WM_ENDEDIT:
        if (!P.edit) return 0;
        endEdit(w != 0, l == 0);
        if (l == 1 && GetForegroundWindow() != P.hwnd) PostMessageW(P.hwnd, WM_CHECKACTIVE, 0, 0);
        return 0;
    case WM_CHECKACTIVE:
        if (GetForegroundWindow() != P.hwnd && !P.edit) uiClosePanel();
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) uiClosePanel();
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, RGB(0xEE, 0xF0, 0xF4));
        SetBkColor((HDC)w, RGB(0x15, 0x18, 0x20));
        return (LRESULT)P.editBrush;
    case WM_TIMER:
        if (w == 1) {
            float t = (GetTickCount() - P.animStart) / 200.0f;
            if (t >= 1.0f) {
                KillTimer(h, 1);
                panelPresent(255, 0);
            } else {
                float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                float a = t / 0.75f;
                if (a > 1.0f) a = 1.0f;
                panelPresent((int)(255 * a), (int)(10.0f * P.s * (1.0f - e)));
            }
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!P.tracking) {
            TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, h, 0 };
            TrackMouseEvent(&te);
            P.tracking = true;
        }
        float lx = (short)LOWORD(l) / P.s, ly = (short)HIWORD(l) / P.s;
        if (P.drag) { sliderSet(P.dragSel, lx); return 0; }
        int i = hitAt(lx, ly);
        Sel n = { K_NONE, 0, 0 };
        if (i >= 0) n = hits[i].s;
        if (!selEq(n, P.hover)) { P.hover = n; redraw(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        P.tracking = false;
        if (P.hover.kind != K_NONE) { P.hover.kind = K_NONE; redraw(); }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        float lx = (short)LOWORD(l) / P.s, ly = (short)HIWORD(l) / P.s;
        int i = hitAt(lx, ly);
        if (i < 0) return 0;
        Hit hit = hits[i];
        if (hit.s.kind == K_KSL || hit.s.kind == K_BSL) {
            SetCapture(h);
            P.drag = true;
            P.dragSel = hit.s;
            sliderSet(hit.s, lx);
            return 0;
        }
        if (hit.s.kind == K_NOTE) {
            beginEdit(hit.s.mon, hit.x, hit.y, hit.w, hit.h);
            return 0;
        }
        SetCapture(h);
        P.press = hit.s;
        return 0;
    }
    case WM_LBUTTONUP: {
        float lx = (short)LOWORD(l) / P.s, ly = (short)HIWORD(l) / P.s;
        if (P.drag) {
            P.drag = false;
            ReleaseCapture();
            redraw();
            return 0;
        }
        if (P.press.kind != K_NONE) {
            Sel s = P.press;
            P.press.kind = K_NONE;
            ReleaseCapture();
            int i = hitAt(lx, ly);
            if (i >= 0 && selEq(hits[i].s, s)) fire(s);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (P.drag) { P.drag = false; redraw(); }
        P.press.kind = K_NONE;
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            int i = hitAt(pt.x / P.s, pt.y / P.s);
            LPCWSTR c = IDC_ARROW;
            if (i >= 0) c = hits[i].s.kind == K_NOTE ? IDC_IBEAM : IDC_HAND;
            SetCursor(LoadCursorW(NULL, c));
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

static struct OsdState {
    HWND hwnd;
    Surf sf;
    Fonts F;
    int W, H, baseX, baseY;
    DWORD t0;
} O;

static void osdClose()
{
    if (!O.hwnd) return;
    HWND h = O.hwnd;
    O.hwnd = NULL;
    DestroyWindow(h);
    surfFree(O.sf);
    fontsFree(O.F);
    gdipRelease();
}

static void osdPresent(int alpha)
{
    POINT pt = { O.baseX, O.baseY };
    SIZE sz = { O.W, O.H };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
    HDC sd = GetDC(NULL);
    UpdateLayeredWindow(O.hwnd, sd, &pt, &sz, O.sf.dc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, sd);
}

static LRESULT CALLBACK osdProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER && O.hwnd == h) {
        if (w == 1) {
            KillTimer(h, 1);
            O.t0 = GetTickCount();
            SetTimer(h, 2, 16, NULL);
        } else if (w == 2) {
            float t = (GetTickCount() - O.t0) / 200.0f;
            if (t >= 1.0f) osdClose();
            else osdPresent((int)(255 * (1.0f - t)));
        }
        return 0;
    }
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

static void registerClasses()
{
    static bool done;
    if (done) return;
    done = true;
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = g_inst;
    wc.lpszClassName = L"DuskPanel";
    wc.lpfnWndProc = panelProc;
    wc.style = CS_DBLCLKS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpszClassName = L"DuskOsd";
    wc.lpfnWndProc = osdProc;
    wc.style = 0;
    RegisterClassW(&wc);
}

void uiFlash(int mi, const wchar_t* name, const wchar_t* value)
{
    if (mi < 0 || mi >= g_nmon) return;
    registerClasses();
    const RECT& wk = g_mon[mi].work;
    if (!O.hwnd) {
        gdipAcquire();
        O.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            L"DuskOsd", L"", WS_POPUP, (wk.left + wk.right) / 2, (wk.top + wk.bottom) / 2, 4, 4, NULL, NULL, g_inst, NULL);
        fontsInit(O.F);
    } else {
        KillTimer(O.hwnd, 1);
        KillTimer(O.hwnd, 2);
        SetWindowPos(O.hwnd, NULL, (wk.left + wk.right) / 2, (wk.top + wk.bottom) / 2, 4, 4, SWP_NOZORDER | SWP_NOACTIVATE);
        surfFree(O.sf);
    }
    if (!O.hwnd) { fontsFree(O.F); gdipRelease(); return; }
    float s = GetDpiForWindow(O.hwnd) / 96.0f;
    float nameW = tw(name, O.F.r13), valW = tw(value, O.F.m13);
    float cw = 18 + nameW + 14 + valW + 18 + 2;
    float ch = 12 + 17 + 12 + 2;
    float lw = cw + 16, lh = ch + 16;
    O.W = (int)ceilf(lw * s);
    O.H = (int)ceilf(lh * s);
    surfNew(O.sf, O.W, O.H);
    {
        Graphics g(O.sf.bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.ScaleTransform(s, s);
        fillRR(g, C_PAPER, 8, 8, cw, ch, 16);
        edgeRR(g, 8, 8, cw, ch, 16);
        float ty = 8 + 1 + 12;
        txt(g, name, O.F.r13, C_INK, 8 + 1 + 18, ty, nameW + 10, 17, StringAlignmentNear);
        txt(g, value, O.F.m13, C_INK, 8 + 1 + 18 + nameW + 14, ty, valW + 10, 17, StringAlignmentNear);
    }
    int inset = (int)(16 * s);
    O.baseX = wk.right - O.W - inset;
    O.baseY = wk.top + inset;
    osdPresent(255);
    ShowWindow(O.hwnd, SW_SHOWNOACTIVATE);
    SetTimer(O.hwnd, 1, 1200, NULL);
}

void uiChanged()
{
    if (P.hwnd) redraw();
}

static void placePanel()
{
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    P.baseX = wa.right - P.W;
    P.baseY = wa.bottom - P.H;
}

void uiRebuild()
{
    if (!P.hwnd) return;
    panelAlloc(P.s);
    placePanel();
    redraw();
}

void uiShowPanel()
{
    if (P.hwnd) {
        SetForegroundWindow(P.hwnd);
        return;
    }
    registerClasses();
    gdipAcquire();
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"DuskPanel", L"Dusk", WS_POPUP,
        wa.right - 20, wa.bottom - 20, 10, 10, NULL, NULL, g_inst, NULL);
    if (!h) { gdipRelease(); return; }
    float s = GetDpiForWindow(h) / 96.0f;
    P.autostart = autostartGet();
    if (!panelAlloc(s)) { DestroyWindow(h); panelFree(); gdipRelease(); return; }
    P.hwnd = h;
    P.tracking = false;
    placePanel();
    panelRender();
    BOOL animOn = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animOn, 0);
    P.anim = animOn != FALSE;
    panelPresent(P.anim ? 0 : 255, P.anim ? (int)(10 * s) : 0);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    if (P.anim) {
        P.animStart = GetTickCount();
        SetTimer(h, 1, 15, NULL);
    }
}

void uiClosePanel()
{
    if (!P.hwnd) return;
    if (P.edit) endEdit(true, false);
    if (!P.hwnd) return;
    HWND h = P.hwnd;
    P.hwnd = NULL;
    P.lastHidden = GetTickCount();
    KillTimer(h, 1);
    DestroyWindow(h);
    panelFree();
    gdipRelease();
}

void uiTogglePanel()
{
    if (P.hwnd) { uiClosePanel(); return; }
    if (GetTickCount() - P.lastHidden < 300) return;
    uiShowPanel();
}

void uiShutdown()
{
    uiClosePanel();
    osdClose();
}

static bool pngClsid(CLSID* c)
{
    UINT n = 0, sz = 0;
    GetImageEncodersSize(&n, &sz);
    if (!sz) return false;
    ImageCodecInfo* ci = (ImageCodecInfo*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!ci) return false;
    GetImageEncoders(n, sz, ci);
    bool ok = false;
    for (UINT i = 0; i < n; i++)
        if (lstrcmpW(ci[i].MimeType, L"image/png") == 0) { *c = ci[i].Clsid; ok = true; break; }
    HeapFree(GetProcessHeap(), 0, ci);
    return ok;
}

bool uiSelfTest(const wchar_t* pngPath)
{
    bool ok = false;
    for (int pass = 0; pass < 2; pass++) {
        gdipAcquire();
        P.autostart = autostartGet();
        ok = false;
        if (panelAlloc(2.0f)) {
            panelRender();
            CLSID c;
            if (pngClsid(&c)) ok = P.sf.bmp->Save(pngPath, &c, NULL) == Ok;
        }
        panelFree();
        gdipRelease();
    }
    return ok;
}
