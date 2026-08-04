#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <objidl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;
using namespace std::chrono_literals;

namespace {
constexpr wchar_t kClassName[] = L"SNEWSWARE_WINDOW_V2";
constexpr ULONG_PTR kSyntheticTag = 0x534E4557;

HWND g_hwnd{};
HHOOK g_keyboardHook{};
ULONG_PTR g_gdiplusToken{};
std::unique_ptr<Image> g_fishermanImage;

std::atomic_bool g_running{true};
std::atomic_bool g_socdEnabled{false};
std::atomic_bool g_fisherEnabled{false};
std::atomic_bool g_chestEnabled{false};
std::atomic_bool g_fishingActive{false};

std::atomic_int g_selectedTab{0};
int g_targetTab = 0;
float g_indicatorY = 102.0f;
float g_contentAlpha = 1.0f;
bool g_animating = false;

bool g_heldA = false;
bool g_heldD = false;
int g_activeKey = 0;
std::mutex g_keyMutex;

const Color kCanvas(255, 13, 14, 18);
const Color kPanel(255, 18, 19, 24);
const Color kRaised(255, 25, 26, 32);
const Color kCard(245, 15, 16, 21);
const Color kText(255, 247, 245, 242);
const Color kMuted(255, 145, 141, 145);
const Color kOrange(255, 255, 105, 43);
const Color kOrangeSoft(255, 255, 139, 74);
const Color kRed(255, 232, 61, 48);
const Color kGreen(255, 70, 207, 126);

bool IsRobloxFocused() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return false;
    wchar_t path[1024]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    const bool loaded = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    if (!loaded) return false;
    std::wstring lowered(path, size);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return lowered.find(L"robloxplayerbeta.exe") != std::wstring::npos;
}

void SendKey(int virtualKey, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(virtualKey);
    input.ki.dwExtraInfo = kSyntheticTag;
    if (!down) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

void SendMouseButton(bool down) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));
}

void SendClickAt(int x, int y, bool down) {
    SetCursorPos(x, y);
    SendMouseButton(down);
}

void ActivateStrafe(int virtualKey) {
    if (g_activeKey == virtualKey) return;
    if (g_activeKey) SendKey(g_activeKey, false);
    g_activeKey = virtualKey;
    if (virtualKey) SendKey(virtualKey, true);
}

void StopAllAutomation() {
    g_socdEnabled = false;
    g_fisherEnabled = false;
    g_chestEnabled = false;
    g_fishingActive = false;
    std::scoped_lock lock(g_keyMutex);
    ActivateStrafe(0);
    g_heldA = false;
    g_heldD = false;
    SendMouseButton(false);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM message, LPARAM data) {
    if (code < 0) return CallNextHookEx(g_keyboardHook, code, message, data);
    auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;

    if (down && key->vkCode == 'Q' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
        StopAllAutomation();
        return 1;
    }
    if (key->dwExtraInfo == kSyntheticTag) return CallNextHookEx(g_keyboardHook, code, message, data);
    if (!g_socdEnabled || !IsRobloxFocused() || (key->vkCode != 'A' && key->vkCode != 'D')) {
        return CallNextHookEx(g_keyboardHook, code, message, data);
    }

    std::scoped_lock lock(g_keyMutex);
    if (key->vkCode == 'A') {
        if (down) { g_heldA = true; ActivateStrafe('A'); }
        if (up) { g_heldA = false; ActivateStrafe(g_heldD ? 'D' : 0); }
    } else {
        if (down) { g_heldD = true; ActivateStrafe('D'); }
        if (up) { g_heldD = false; ActivateStrafe(g_heldA ? 'A' : 0); }
    }
    return 1;
}

struct Screenshot {
    int width = 0;
    int height = 0;
    std::vector<COLORREF> pixels;
    COLORREF At(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return pixels[static_cast<size_t>(y) * width + x];
    }
};

Screenshot CaptureForeground() {
    Screenshot shot;
    HWND foreground = GetForegroundWindow();
    RECT rect{};
    if (!foreground || !GetWindowRect(foreground, &rect)) return shot;
    shot.width = rect.right - rect.left;
    shot.height = rect.bottom - rect.top;
    if (shot.width < 1 || shot.height < 1) return shot;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, shot.width, shot.height);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, shot.width, shot.height, screen, rect.left, rect.top, SRCCOPY);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = shot.width;
    info.bmiHeader.biHeight = -shot.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    shot.pixels.resize(static_cast<size_t>(shot.width) * shot.height);
    GetDIBits(memory, bitmap, 0, shot.height, shot.pixels.data(), &info, DIB_RGB_COLORS);

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return shot;
}

struct DetectionBox {
    int left = 0, top = 0, right = 0, bottom = 0;
    int Area() const { return std::max(0, right - left) * std::max(0, bottom - top); }
};

using PixelMatcher = bool(*)(COLORREF);

DetectionBox FindColorBounds(const Screenshot& shot, PixelMatcher matcher) {
    DetectionBox box{shot.width, shot.height, 0, 0};
    int count = 0;
    for (int y = shot.height / 3; y < shot.height; y += 2) {
        for (int x = 0; x < shot.width; x += 2) {
            if (!matcher(shot.At(x, y))) continue;
            box.left = std::min(box.left, x);
            box.right = std::max(box.right, x);
            box.top = std::min(box.top, y);
            box.bottom = std::max(box.bottom, y);
            ++count;
        }
    }
    return count < 20 ? DetectionBox{} : box;
}

bool IsGreenPixel(COLORREF color) {
    return GetGValue(color) > 145 && GetGValue(color) > GetRValue(color) * 1.25 && GetGValue(color) > GetBValue(color) * 1.05;
}

bool IsPinkPixel(COLORREF color) {
    return GetRValue(color) > 150 && GetBValue(color) > 90 && GetRValue(color) > GetGValue(color) * 1.25;
}

void FisherLoop() {
    bool mouseDown = false;
    auto lastProbe = std::chrono::steady_clock::now() - 1s;
    auto lastSeen = lastProbe;
    while (g_running) {
        if (!g_fisherEnabled || !IsRobloxFocused()) {
            if (mouseDown) { SendMouseButton(false); mouseDown = false; }
            g_fishingActive = false;
            std::this_thread::sleep_for(50ms);
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto interval = g_fishingActive ? 15ms : 500ms;
        if (now - lastProbe < interval) { std::this_thread::sleep_for(5ms); continue; }
        lastProbe = now;

        Screenshot shot = CaptureForeground();
        DetectionBox green = FindColorBounds(shot, IsGreenPixel);
        DetectionBox pink = FindColorBounds(shot, IsPinkPixel);
        const bool found = green.Area() > 250 && pink.Area() > 50 && green.top > shot.height / 2;
        if (found) {
            g_fishingActive = true;
            lastSeen = now;
            const int greenCenter = (green.top + green.bottom) / 2;
            const int targetCenter = (pink.top + pink.bottom) / 2;
            const bool shouldHold = greenCenter > targetCenter + 3;
            if (shouldHold != mouseDown) {
                SendClickAt((green.left + green.right) / 2, greenCenter, shouldHold);
                mouseDown = shouldHold;
            }
        } else if (g_fishingActive && now - lastSeen > 200ms) {
            if (mouseDown) { SendMouseButton(false); mouseDown = false; }
            g_fishingActive = false;
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

bool DetectChest(const Screenshot& shot, DetectionBox& grid) {
    int minX = shot.width, minY = shot.height, maxX = 0, maxY = 0, count = 0;
    for (int y = shot.height / 5; y < shot.height * 4 / 5; y += 3) {
        for (int x = 0; x < shot.width * 55 / 100; x += 3) {
            COLORREF color = shot.At(x, y);
            const int red = GetRValue(color), green = GetGValue(color), blue = GetBValue(color);
            if (red > 65 && red > green * 1.15 && green > blue * 1.25) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y); ++count;
            }
        }
    }
    if (count < 300 || maxX - minX < 220 || maxY - minY < 150) return false;
    grid = {minX, minY, maxX, maxY};
    return true;
}

void ChestLoop() {
    bool latched = false;
    while (g_running) {
        if (!g_chestEnabled || !IsRobloxFocused()) {
            latched = false;
            std::this_thread::sleep_for(80ms);
            continue;
        }
        Screenshot shot = CaptureForeground();
        DetectionBox grid{};
        const bool open = DetectChest(shot, grid);
        if (open && !latched) {
            latched = true;
            RECT windowRect{};
            GetWindowRect(GetForegroundWindow(), &windowRect);
            const double cellWidth = (grid.right - grid.left) / 6.0;
            const double cellHeight = (grid.bottom - grid.top) / 4.0;
            POINT original{};
            GetCursorPos(&original);
            for (int row = 0; row < 4 && g_running && g_chestEnabled; ++row) {
                for (int column = 0; column < 6; ++column) {
                    const int x = windowRect.left + static_cast<int>(grid.left + (column + .5) * cellWidth);
                    const int y = windowRect.top + static_cast<int>(grid.top + (row + .5) * cellHeight);
                    SendClickAt(x, y, true); SendClickAt(x, y, false);
                    std::this_thread::sleep_for(20ms);
                }
            }
            SendKey('E', true); SendKey('E', false);
            SetCursorPos(original.x, original.y);
        }
        if (!open) latched = false;
        std::this_thread::sleep_for(80ms);
    }
}

void FocusSafetyLoop() {
    while (g_running) {
        if (!IsRobloxFocused()) {
            std::scoped_lock lock(g_keyMutex);
            g_heldA = false; g_heldD = false;
            ActivateStrafe(0);
        }
        std::this_thread::sleep_for(25ms);
    }
}

std::unique_ptr<Image> LoadPngResource(HINSTANCE instance, int resourceId) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return nullptr;
    DWORD size = SizeofResource(instance, resource);
    HGLOBAL loaded = LoadResource(instance, resource);
    const void* bytes = LockResource(loaded);
    if (!bytes || size == 0) return nullptr;

    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy) return nullptr;
    void* destination = GlobalLock(copy);
    std::memcpy(destination, bytes, size);
    GlobalUnlock(copy);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(copy, TRUE, &stream) != S_OK) {
        GlobalFree(copy);
        return nullptr;
    }
    std::unique_ptr<Image> source(Image::FromStream(stream));
    if (!source || source->GetLastStatus() != Ok) {
        stream->Release();
        return nullptr;
    }
    std::unique_ptr<Image> image(source->Clone());
    stream->Release();
    return image;
}

void BuildRoundedPath(GraphicsPath& path, const RectF& rect, float radius) {
    const float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

void FillRounded(Graphics& graphics, const RectF& rect, float radius, Brush& brush) {
    GraphicsPath path;
    BuildRoundedPath(path, rect, radius);
    graphics.FillPath(&brush, &path);
}

void StrokeRounded(Graphics& graphics, const RectF& rect, float radius, Pen& pen) {
    GraphicsPath path;
    BuildRoundedPath(path, rect, radius);
    graphics.DrawPath(&pen, &path);
}

void DrawLabel(Graphics& graphics, const wchar_t* text, float x, float y, float size, const Color& color, FontStyle style = FontStyleRegular) {
    FontFamily family(L"Segoe UI Variable");
    Font font(&family, size, style, UnitPixel);
    SolidBrush brush(color);
    graphics.DrawString(text, -1, &font, PointF(x, y), &brush);
}

void DrawLucideIcon(Graphics& graphics, int icon, float x, float y, float size, const Color& color) {
    Pen pen(color, 1.9f);
    pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    const float scale = size / 24.0f;
    auto P = [&](float px, float py) { return PointF(x + px * scale, y + py * scale); };
    if (icon == 0) { // layout-grid
        graphics.DrawRectangle(&pen, x + 3*scale, y + 3*scale, 7*scale, 7*scale);
        graphics.DrawRectangle(&pen, x + 14*scale, y + 3*scale, 7*scale, 7*scale);
        graphics.DrawRectangle(&pen, x + 3*scale, y + 14*scale, 7*scale, 7*scale);
        graphics.DrawRectangle(&pen, x + 14*scale, y + 14*scale, 7*scale, 7*scale);
    } else if (icon == 1) { // package/chest
        PointF top[] = {P(3,7),P(12,2),P(21,7),P(12,12),P(3,7)};
        graphics.DrawLines(&pen, top, 5);
        PointF left[] = {P(3,7),P(3,17),P(12,22),P(12,12)};
        graphics.DrawLines(&pen, left, 4);
        PointF right[] = {P(21,7),P(21,17),P(12,22)};
        graphics.DrawLines(&pen, right, 3);
    } else if (icon == 2) { // fish
        GraphicsPath path;
        path.AddBezier(P(6,7),P(12,2),P(18,5),P(20,8));
        path.AddBezier(P(20,8),P(18,11),P(12,14),P(6,9));
        path.AddLine(P(6,7),P(2,4)); path.AddLine(P(2,4),P(2,12)); path.AddLine(P(2,12),P(6,9));
        graphics.DrawPath(&pen, &path);
        SolidBrush dot(color); graphics.FillEllipse(&dot, x+15.5f*scale, y+6.5f*scale, 1.8f*scale, 1.8f*scale);
    } else if (icon == 3) { // keyboard
        GraphicsPath keyboardPath;
        BuildRoundedPath(keyboardPath, RectF(x+2*scale,y+5*scale,20*scale,14*scale), 2*scale);
        graphics.DrawPath(&pen, &keyboardPath);
        SolidBrush keyDot(color);
        for (int row=0; row<2; ++row) for (int col=0; col<5; ++col) graphics.FillEllipse(&keyDot, x+(5+col*3.5f)*scale,y+(9+row*3.5f)*scale,1.1f*scale,1.1f*scale);
        graphics.DrawLine(&pen, P(7,16), P(17,16));
    } else { // settings
        graphics.DrawEllipse(&pen, x+8*scale,y+8*scale,8*scale,8*scale);
        graphics.DrawEllipse(&pen, x+3*scale,y+3*scale,18*scale,18*scale);
        graphics.DrawLine(&pen,P(12,1),P(12,4));graphics.DrawLine(&pen,P(12,20),P(12,23));
        graphics.DrawLine(&pen,P(1,12),P(4,12));graphics.DrawLine(&pen,P(20,12),P(23,12));
    }
}

void DrawToggle(Graphics& graphics, float x, float y, bool enabled) {
    RectF track(x, y, 48, 26);
    LinearGradientBrush active(track, kOrange, kRed, LinearGradientModeHorizontal);
    SolidBrush inactive(Color(255, 53, 55, 65));
    FillRounded(graphics, track, 13, enabled ? static_cast<Brush&>(active) : static_cast<Brush&>(inactive));
    SolidBrush knob(kText);
    const float knobX = enabled ? x + 25 : x + 3;
    graphics.FillEllipse(&knob, knobX, y + 3, 20, 20);
}

void DrawSidebar(Graphics& graphics, int height) {
    SolidBrush sideBrush(Color(248, 20, 21, 27));
    graphics.FillRectangle(&sideBrush, 0, 0, 88, height);
    Pen border(Color(255, 42, 43, 51), 1);
    graphics.DrawLine(&border, 87, 0, 87, height);

    LinearGradientBrush logoGradient(RectF(20, 18, 48, 38), kOrangeSoft, kRed, LinearGradientModeForwardDiagonal);
    FontFamily logoFamily(L"Segoe UI Variable"); Font logoFont(&logoFamily, 25, FontStyleBold, UnitPixel);
    graphics.DrawString(L"S", -1, &logoFont, PointF(27, 22), &logoGradient);

    const wchar_t* labels[] = {L"HOME", L"CHEST", L"FISHER", L"SOCD", L"SETTINGS"};
    for (int index = 0; index < 5; ++index) {
        const float y = 92.0f + index * 68.0f;
        if (index == g_selectedTab) {
            LinearGradientBrush selected(RectF(10, y, 68, 56), Color(255, 88, 35, 26), Color(255, 40, 24, 26), LinearGradientModeForwardDiagonal);
            FillRounded(graphics, RectF(10, y, 68, 56), 11, selected);
            Pen selectedBorder(Color(255, 135, 57, 35), 1);
            StrokeRounded(graphics, RectF(10, y, 68, 56), 11, selectedBorder);
        }
        const Color iconColor = index == g_selectedTab ? kOrangeSoft : Color(255, 126, 124, 132);
        DrawLucideIcon(graphics, index, 32, y + 9, 24, iconColor);
        DrawLabel(graphics, labels[index], 20, y + 38, 8.5f, iconColor, FontStyleBold);
    }
    LinearGradientBrush indicator(RectF(7, g_indicatorY, 3, 36), kOrangeSoft, kRed, LinearGradientModeVertical);
    FillRounded(graphics, RectF(7, g_indicatorY, 3, 36), 2, indicator);
}

void DrawTopBar(Graphics& graphics, int width) {
    DrawLabel(graphics, L"SNEWSWARE", 116, 22, 15, kText, FontStyleBold);
    DrawLabel(graphics, L"BEDWARS UTILITY", 220, 25, 9, kOrangeSoft, FontStyleBold);
    SolidBrush live(Color(255, 19, 48, 34)); FillRounded(graphics, RectF(width - 172.0f, 18, 126, 30), 8, live);
    SolidBrush dot(kGreen); graphics.FillEllipse(&dot, width - 157.0f, 30, 7, 7);
    DrawLabel(graphics, L"SYSTEM READY", width - 140.0f, 25, 10, Color(255, 153, 215, 178), FontStyleBold);
    Pen border(Color(255, 43, 39, 39), 1); graphics.DrawLine(&border, 88, 66, width, 66);
}

void DrawHeroImage(Graphics& graphics, float alpha = 1.0f) {
    if (!g_fishermanImage) return;
    const UINT width = g_fishermanImage->GetWidth();
    const UINT height = g_fishermanImage->GetHeight();
    ColorMatrix matrix = {
        1,0,0,0,0,
        0,1,0,0,0,
        0,0,1,0,0,
        0,0,0,alpha,0,
        0,0,0,0,1
    };
    ImageAttributes attributes; attributes.SetColorMatrix(&matrix);
    Rect destination(700, 125, 310, 430);
    graphics.DrawImage(g_fishermanImage.get(), destination, 0, 0, width, height, UnitPixel, &attributes);
}

void DrawMasterCard(Graphics& graphics, const wchar_t* title, const wchar_t* subtitle, bool enabled) {
    SolidBrush card(kCard); FillRounded(graphics, RectF(128, 188, 530, 68), 12, card);
    Pen border(Color(255, 58, 40, 35), 1); StrokeRounded(graphics, RectF(128, 188, 530, 68), 12, border);
    DrawLabel(graphics, title, 148, 202, 15, kText, FontStyleBold);
    DrawLabel(graphics, subtitle, 148, 226, 10, kMuted);
    DrawToggle(graphics, 588, 209, enabled);
}

void DrawHome(Graphics& graphics) {
    DrawLabel(graphics, L"AUTOMATION SUITE", 128, 94, 10, kOrangeSoft, FontStyleBold);
    DrawLabel(graphics, L"Designed to feel", 128, 116, 34, kText, FontStyleBold);
    DrawLabel(graphics, L"effortless.", 366, 116, 34, kOrangeSoft, FontStyleBold);
    DrawLabel(graphics, L"Focused tools. Clear states. No clutter.", 128, 158, 13, kMuted);

    const wchar_t* names[] = {L"Fisher", L"Chest Looting", L"SOCD Cleaner"};
    const wchar_t* notes[] = {L"Adaptive visual tracking", L"24-slot intelligent sweep", L"Last-input movement priority"};
    const int icons[] = {2,1,3};
    const bool enabled[] = {g_fisherEnabled.load(), g_chestEnabled.load(), g_socdEnabled.load()};
    for (int index = 0; index < 3; ++index) {
        const float x = 128.0f + index * 216.0f;
        LinearGradientBrush card(RectF(x, 204, 198, 226), Color(255, 25, 25, 31), Color(255, 14, 15, 20), LinearGradientModeVertical);
        FillRounded(graphics, RectF(x, 204, 198, 226), 15, card);
        Pen border(Color(255, 55, 43, 40), 1); StrokeRounded(graphics, RectF(x, 204, 198, 226), 15, border);
        SolidBrush iconSurface(Color(255, 61, 32, 27)); FillRounded(graphics, RectF(x+18, 224, 46, 46), 11, iconSurface);
        DrawLucideIcon(graphics, icons[index], x+29, 235, 24, kOrangeSoft);
        DrawLabel(graphics, names[index], x+18, 292, 17, kText, FontStyleBold);
        DrawLabel(graphics, notes[index], x+18, 322, 10.5f, kMuted);
        DrawLabel(graphics, enabled[index] ? L"ACTIVE" : L"READY", x+18, 390, 9, enabled[index] ? kGreen : kOrangeSoft, FontStyleBold);
    }
    DrawHeroImage(graphics, .18f);
}

void DrawFisher(Graphics& graphics) {
    DrawLabel(graphics, L"KIT AUTOMATION / FISHERMAN", 128, 94, 10, kOrangeSoft, FontStyleBold);
    DrawLabel(graphics, L"Fisher", 128, 116, 34, kText, FontStyleBold);
    DrawLabel(graphics, L"Keeps the catch inside the target zone automatically.", 128, 158, 13, kMuted);
    DrawMasterCard(graphics, L"Fishing Macro", L"Automatic detection · adaptive short clicks", g_fisherEnabled);

    SolidBrush panel(Color(245, 14, 15, 20)); FillRounded(graphics, RectF(128, 276, 530, 226), 14, panel);
    Pen border(Color(255, 49, 38, 35), 1); StrokeRounded(graphics, RectF(128, 276, 530, 226), 14, border);
    DrawLabel(graphics, L"TRACKER", 150, 294, 10, kOrangeSoft, FontStyleBold);
    DrawLabel(graphics, L"CONTROL", 270, 294, 10, kMuted, FontStyleBold);
    DrawLabel(graphics, L"STATUS", 390, 294, 10, kMuted, FontStyleBold);
    DrawLabel(graphics, L"COLORS", 505, 294, 10, kMuted, FontStyleBold);
    LinearGradientBrush underline(RectF(146, 322, 104, 2), kOrangeSoft, kRed, LinearGradientModeHorizontal);
    graphics.FillRectangle(&underline, 146, 322, 104, 2);

    DrawLabel(graphics, g_fishingActive ? L"● TRACKING FISHING UI" : L"● IDLE — WATCHING EVERY 0.5 S", 150, 342, 11, g_fishingActive ? kGreen : kMuted, FontStyleBold);
    SolidBrush preview(Color(255, 8, 9, 13)); FillRounded(graphics, RectF(150, 372, 486, 74), 9, preview);
    Pen gridPen(Color(80, 61, 63, 72), 1); for (int y=380; y<440; y+=6) graphics.DrawLine(&gridPen, 160, y, 626, y);
    DrawLabel(graphics, L"Live detection view", 328, 398, 11, Color(255, 89, 87, 94));
    SolidBrush action(Color(255, 15, 45, 31)); FillRounded(graphics, RectF(150, 460, 486, 30), 8, action);
    Pen actionBorder(Color(255, 31, 111, 70), 1); StrokeRounded(graphics, RectF(150, 460, 486, 30), 8, actionBorder);
    DrawLabel(graphics, L"▶  ADAPTIVE CONTROL READY", 294, 467, 10, kGreen, FontStyleBold);
    DrawHeroImage(graphics, .95f);
}

void DrawChest(Graphics& graphics) {
    DrawLabel(graphics, L"GAMEPLAY AUTOMATION / CHEST", 128, 94, 10, kOrangeSoft, FontStyleBold);
    DrawLabel(graphics, L"Chest Looting", 128, 116, 34, kText, FontStyleBold);
    DrawLabel(graphics, L"Detects the 6 × 4 grid and clears every slot in row order.", 128, 158, 13, kMuted);
    DrawMasterCard(graphics, L"Chest Loot Macro", L"80 ms detection · 20 ms per slot · closes with E", g_chestEnabled);

    SolidBrush panel(Color(245, 14, 15, 20)); FillRounded(graphics, RectF(128, 276, 650, 248), 14, panel);
    Pen border(Color(255, 49, 38, 35), 1); StrokeRounded(graphics, RectF(128, 276, 650, 248), 14, border);
    DrawLabel(graphics, L"● WATCHING FOR CHEST UI", 150, 298, 10, kOrangeSoft, FontStyleBold);
    for (int row=0; row<4; ++row) {
        for (int column=0; column<6; ++column) {
            const float x=150.0f+column*98.0f, y=334.0f+row*42.0f;
            LinearGradientBrush slot(RectF(x,y,88,34), Color(255,83,49,27), Color(255,53,33,23), LinearGradientModeVertical);
            FillRounded(graphics, RectF(x,y,88,34), 7, slot);
            Pen slotBorder(Color(255,108,62,31),1); StrokeRounded(graphics,RectF(x,y,88,34),7,slotBorder);
            wchar_t number[8]{}; swprintf_s(number,L"%d",row*6+column+1);
            DrawLabel(graphics,number,x+38,y+10,9,kOrangeSoft,FontStyleBold);
        }
    }
    DrawLabel(graphics, L"Cursor returns to its original position after each sweep.", 150, 506, 10, kMuted);
}

void DrawSocd(Graphics& graphics) {
    DrawLabel(graphics, L"MOVEMENT / INPUT PRIORITY", 128, 94, 10, kOrangeSoft, FontStyleBold);
    DrawLabel(graphics, L"SOCD Cleaner", 128, 116, 34, kText, FontStyleBold);
    DrawLabel(graphics, L"The newest strafe input wins—without movement lock.", 128, 158, 13, kMuted);
    DrawMasterCard(graphics, L"SOCD Cleaner", L"Foreground-only A / D handling", g_socdEnabled);

    LinearGradientBrush panel(RectF(128,276,650,238), Color(255,23,24,30), Color(255,12,13,18), LinearGradientModeVertical);
    FillRounded(graphics, RectF(128,276,650,238), 14, panel);
    Pen border(Color(255,49,38,35),1); StrokeRounded(graphics,RectF(128,276,650,238),14,border);
    DrawLabel(graphics,L"LAST INPUT PRIORITY",150,298,10,kOrangeSoft,FontStyleBold);
    const float keyY=350;
    for(int i=0;i<2;++i){float x=i?574.0f:230.0f;LinearGradientBrush key(RectF(x,keyY,92,92),Color(255,45,46,55),Color(255,20,21,27),LinearGradientModeVertical);FillRounded(graphics,RectF(x,keyY,92,92),13,key);Pen kp(Color(255,87,88,99),1.5f);StrokeRounded(graphics,RectF(x,keyY,92,92),13,kp);DrawLabel(graphics,i?L"D":L"A",x+33,keyY+26,31,kText,FontStyleBold);} 
    DrawLabel(graphics,L"←  LAST  →",380,377,22,kOrangeSoft,FontStyleRegular);
    DrawLabel(graphics,L"Release the newest key to resume the other held direction.",245,468,11,kMuted);
}

void DrawSettings(Graphics& graphics) {
    DrawLabel(graphics,L"SNEWSWARE / SETTINGS",128,94,10,kOrangeSoft,FontStyleBold);
    DrawLabel(graphics,L"Control center",128,116,34,kText,FontStyleBold);
    DrawLabel(graphics,L"Safety, status, and application information.",128,158,13,kMuted);
    const wchar_t* titles[]={L"Emergency stop",L"Focus safety",L"Application"};
    const wchar_t* values[]={L"CTRL + Q",L"Pause outside Roblox",L"Version 0.2 · Snewsware"};
    for(int i=0;i<3;++i){float y=206.0f+i*88.0f;SolidBrush card(kCard);FillRounded(graphics,RectF(128,y,650,70),12,card);Pen bp(Color(255,53,42,39),1);StrokeRounded(graphics,RectF(128,y,650,70),12,bp);DrawLabel(graphics,titles[i],150,y+14,14,kText,FontStyleBold);DrawLabel(graphics,values[i],150,y+39,11,i==0?kOrangeSoft:kMuted);}
}

void RenderInterface(HDC target) {
    RECT client{}; GetClientRect(g_hwnd, &client);
    const int width = client.right, height = client.bottom;
    Bitmap backBuffer(width, height, PixelFormat32bppPARGB);
    Graphics graphics(&backBuffer);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    LinearGradientBrush background(RectF(0,0,static_cast<float>(width),static_cast<float>(height)), Color(255,18,19,24), kCanvas, LinearGradientModeForwardDiagonal);
    graphics.FillRectangle(&background, 0, 0, width, height);
    GraphicsPath glowPath; glowPath.AddEllipse(width-470.0f, 50, 520, 520);
    PathGradientBrush glow(&glowPath); glow.SetCenterColor(Color(72,181,55,30)); Color surround(0,181,55,30); int count=1; glow.SetSurroundColors(&surround,&count); graphics.FillPath(&glow,&glowPath);

    DrawSidebar(graphics, height);
    DrawTopBar(graphics, width);

    if (g_selectedTab == 0) DrawHome(graphics);
    else if (g_selectedTab == 1) DrawChest(graphics);
    else if (g_selectedTab == 2) DrawFisher(graphics);
    else if (g_selectedTab == 3) DrawSocd(graphics);
    else DrawSettings(graphics);

    DrawLabel(graphics,L"CTRL + Q",128,height-34.0f,10,kOrangeSoft,FontStyleBold);
    DrawLabel(graphics,L"Emergency stop",190,height-34.0f,10,kMuted);

    Graphics output(target);
    output.DrawImage(&backBuffer, 0, 0);
}

void SwitchTab(int tab) {
    if (tab == g_targetTab) return;
    g_targetTab = tab;
    g_selectedTab = tab;
    g_contentAlpha = 0.0f;
    g_animating = true;
    SetTimer(g_hwnd, 1, 16, nullptr);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

bool ToggleHit(int x, int y) { return x >= 570 && x <= 650 && y >= 190 && y <= 260; }

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            if (x < 88 && y >= 92 && y < 432) {
                SwitchTab((y - 92) / 68);
                return 0;
            }
            if (ToggleHit(x,y)) {
                if (g_selectedTab == 1) g_chestEnabled = !g_chestEnabled;
                if (g_selectedTab == 2) g_fisherEnabled = !g_fisherEnabled;
                if (g_selectedTab == 3) {
                    g_socdEnabled = !g_socdEnabled;
                    if (!g_socdEnabled) { std::scoped_lock lock(g_keyMutex); ActivateStrafe(0); }
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_TIMER: {
            const float target = 102.0f + g_targetTab * 68.0f;
            g_indicatorY += (target - g_indicatorY) * 0.24f;
            g_contentAlpha = std::min(1.0f, g_contentAlpha + 0.11f);
            if (std::abs(target - g_indicatorY) < 0.4f && g_contentAlpha >= 1.0f) {
                g_indicatorY = target; g_animating = false; KillTimer(window, 1);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
            RenderInterface(dc); EndPaint(window, &paint); return 0;
        }
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput startupInput;
    GdiplusStartup(&g_gdiplusToken, &startupInput, nullptr);
    g_fishermanImage = LoadPngResource(instance, IDR_FISHERMAN);

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    g_hwnd = CreateWindowExW(0, kClassName, L"Snewsware", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1080, 680, nullptr, nullptr, instance, nullptr);
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &darkMode, sizeof(darkMode));
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, instance, 0);
    std::thread(FisherLoop).detach();
    std::thread(ChestLoop).detach();
    std::thread(FocusSafetyLoop).detach();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
    StopAllAutomation();
    g_fishermanImage.reset();
    GdiplusShutdown(g_gdiplusToken);
    return 0;
}
