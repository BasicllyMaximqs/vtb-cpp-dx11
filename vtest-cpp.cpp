#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <thread>
#include <vector>
#include <iostream>
#include <cmath>
#include <string>
#include <chrono>
#include <random>
#include <ctime>
#include <algorithm> // Include for std::max
#include "WinUser.h"
#include "./Interception/library/interception.h"
#include <dwmapi.h>
#include <gdiplus.h>
#define NOMINMAX
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "interception.lib")
#pragma comment(lib, "gdiplus.lib")

// Configuration
struct Config {
    int requiredThreshold = 3;
    int targetR = 200;
    int targetG = 200;
    int targetB = 50;
    int colorTolerance = 20;
    int scanStep = 1;
    int fovRadius = 12;
    bool useAA = false;
    bool disableAfterOne = false;
} config;

// Global Variables (Minimize where possible)
HWND hOverlayWnd = nullptr;
bool running = true;
bool toggle = false;
InterceptionContext context = nullptr; // Initialize to nullptr
InterceptionDevice keyboard = 0;
InterceptionDevice mouse = 0;
POINT monitor1Center = { 0, 0 };
RECT monitor1Rect = { 0, 0, 0, 0 };

// ========== Color Detection ==========

bool isColorClose(BYTE r, BYTE g, BYTE b, const Config& cfg) {
    return std::abs(r - cfg.targetR) < cfg.colorTolerance &&
        std::abs(g - cfg.targetG) < cfg.colorTolerance &&
        std::abs(b - cfg.targetB) < cfg.colorTolerance;
}

bool isInFOV(int x, int y, int centerX, int centerY, int radius) {
    int dx = x - centerX;
    int dy = y - centerY;
    return (dx * dx + dy * dy) <= (radius * radius);
}

// ========== Interception Send ==========

void sendJ() {
    if (context && keyboard) {
        InterceptionKeyStroke jDown = { 0x24, INTERCEPTION_KEY_DOWN, 0 }; // 'J' key down
        InterceptionKeyStroke jUp = { 0x24, INTERCEPTION_KEY_UP, 0 };     // 'J' key up
        interception_send(context, keyboard, (InterceptionStroke*)&jDown, 1);
        interception_send(context, keyboard, (InterceptionStroke*)&jUp, 1);
    }
    else {
        std::cerr << "[Interception] Keyboard device or context not initialized for sendJ." << std::endl;
    }
}

void getMousePos(int& x, int& y) {
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    x = cursorPos.x;
    y = cursorPos.y;
}

void moveMouseRelative(int dx, int dy) {
    if (context && mouse) {
        InterceptionMouseStroke moveStroke = {};
        moveStroke.x = dx;
        moveStroke.y = dy;
        moveStroke.flags = INTERCEPTION_MOUSE_MOVE_RELATIVE;
        interception_send(context, mouse, (InterceptionStroke*)&moveStroke, 1);
        // std::cout << "[Mouse] Moved by (" << dx << ", " << dy << ")" << std::endl; // Consider conditional logging
    }
    else {
        std::cerr << "[Interception] Mouse device or context not initialized for relative move." << std::endl;
    }
}

void UpdatePrimaryMonitorCenter() {
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
            auto* list = reinterpret_cast<std::vector<HMONITOR>*>(lParam);
            list->push_back(hMon);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    for (HMONITOR mon : monitors) {
        MONITORINFOEX mi = { sizeof(mi) };
        if (GetMonitorInfo(mon, &mi)) {
            if (mi.dwFlags & MONITORINFOF_PRIMARY) {
                monitor1Rect = mi.rcMonitor;
                monitor1Center.x = (mi.rcMonitor.left + mi.rcMonitor.right) / 2;
                monitor1Center.y = (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
                std::cout << "[Monitor] Primary center: " << monitor1Center.x << ", " << monitor1Center.y << std::endl;
                break;
            }
        }
    }
}

// ========== Detection Loop ==========

void detectionLoop() {
    while (true) {
        //sleep a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        ID3D11Device* d3dDevice = nullptr;
        ID3D11DeviceContext* d3dContext = nullptr;
        IDXGIOutputDuplication* duplication = nullptr;

        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext))) {
            std::cerr << "[D3D11] Failed to create device." << std::endl;
            return;
        }
        std::cout << "[D3D11] Device created." << std::endl;

        IDXGIDevice* dxgiDevice = nullptr;
        if (FAILED(d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
            if (d3dContext) d3dContext->Release();
            if (d3dDevice) d3dDevice->Release();
            return;
        }

        IDXGIAdapter* adapter = nullptr;
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
            dxgiDevice->Release();
            if (d3dContext) d3dContext->Release();
            if (d3dDevice) d3dDevice->Release();
            return;
        }

        IDXGIOutput* output = nullptr;
        if (FAILED(adapter->EnumOutputs(0, &output))) {
            adapter->Release();
            dxgiDevice->Release();
            if (d3dContext) d3dContext->Release();
            if (d3dDevice) d3dDevice->Release();
            return;
        }

        IDXGIOutput1* output1 = nullptr;
        if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1))) {
            output->Release();
            adapter->Release();
            dxgiDevice->Release();
            if (d3dContext) d3dContext->Release();
            if (d3dDevice) d3dDevice->Release();
            return;
        }

        if (FAILED(output1->DuplicateOutput(d3dDevice, &duplication))) {
            std::cerr << "[DXGI] Failed to duplicate output." << std::endl;
            output1->Release();
            output->Release();
            adapter->Release();
            dxgiDevice->Release();
            if (d3dContext) d3dContext->Release();
            if (d3dDevice) d3dDevice->Release();
            return;
        }
        std::cout << "[DXGI] Output duplicated." << std::endl;

        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;

        while (running) {
            if (!toggle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small sleep to avoid busy-waiting
                continue;
            }

            DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
            IDXGIResource* resource = nullptr;

            if (FAILED(duplication->AcquireNextFrame(100, &frameInfo, &resource))) continue;

            ID3D11Texture2D* tex = nullptr;
            if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) {
                resource->Release();
                continue;
            }

            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);

            ID3D11Texture2D* cpuTex = nullptr;
            D3D11_TEXTURE2D_DESC cpuDesc = desc;
            cpuDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            cpuDesc.Usage = D3D11_USAGE_STAGING;
            cpuDesc.BindFlags = 0;
            cpuDesc.MiscFlags = 0;

            if (FAILED(d3dDevice->CreateTexture2D(&cpuDesc, nullptr, &cpuTex))) {
                tex->Release();
                resource->Release();
                continue;
            }

            d3dContext->CopyResource(cpuTex, tex);

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(d3dContext->Map(cpuTex, 0, D3D11_MAP_READ, 0, &mapped))) {
                BYTE* data = reinterpret_cast<BYTE*>(mapped.pData);
                int pitch = mapped.RowPitch;
                int screenWidth = desc.Width;
                int screenHeight = desc.Height;
                int gameWidth = screenHeight * 16 / 9;
                int marginX = (screenWidth - gameWidth) / 2;
                int yellowCount = 0;
                int sum_x = 0, sum_y = 0;

                for (int y = 0; y < screenHeight; y += config.scanStep) {
                    for (int x = marginX; x < screenWidth - marginX; x += config.scanStep) {
                        if (yellowCount >= config.requiredThreshold) break;

                        BYTE* px = data + y * pitch + x * 4;
                        BYTE b = px[0], g = px[1], r = px[2];

                        if (r > 200 && g > 200 && b < 40 && isInFOV(x, y, monitor1Center.x, monitor1Center.y, config.fovRadius)) {
                            yellowCount++;
                            if (config.useAA) {
                                sum_x += x;
                                sum_y += y;
                            }
                        }
                    }
                }

                if (yellowCount >= config.requiredThreshold) {
                    std::cout << "[Detection] Threshold exceeded" << std::endl;
                    sendJ();
                    //if (config.useAA && yellowCount > 0) {
                   //     moveMouseRelative((sum_x / yellowCount) - monitor1Center.x, (sum_y / yellowCount) - monitor1Center.y);
                   //}
                    if (config.disableAfterOne) {
                        toggle = false;
                    }
                }
                d3dContext->Unmap(cpuTex, 0);
            }

            if (cpuTex) cpuTex->Release();
            if (tex) tex->Release();
            if (resource) resource->Release();
            duplication->ReleaseFrame();

            frameCount++;
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
            if (elapsed.count() >= 1) {
                std::cout << "[FPS] " << frameCount << std::endl;
                frameCount = 0;
                lastTime = currentTime;
            }
        }

        std::cout << "[DXGI] Lost duplication." << std::endl;

        if (duplication) duplication->Release();
        if (output1) output1->Release();
        if (output) output->Release();
        if (adapter) adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        if (d3dContext) d3dContext->Release();
        if (d3dDevice) d3dDevice->Release();
    }
}

// ========== Window Creation and Overlay ==========

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

std::wstring generateRandomWString(int length) {
    static std::mt19937 generator(static_cast<unsigned int>(std::time(nullptr)));
    static std::uniform_int_distribution<int> distribution(L'0', L'z'); // use int instead of wchar_t

    std::wstring str(length, L' ');
    for (auto& ch : str) {
        ch = static_cast<wchar_t>(distribution(generator)); // cast back to wchar_t
        while (!iswalnum(ch)) { // Ensure alphanumeric for class/window names
            ch = static_cast<wchar_t>(distribution(generator));
        }
    }
    return str;
}

void createOverlayWindow() {
    std::wstring classNameTemp = generateRandomWString(16);
    std::wstring windowNameTemp = generateRandomWString(18);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = classNameTemp.c_str();
    if (!RegisterClassEx(&wc)) {
        std::cerr << "[Overlay] Failed to register class." << std::endl;
        return;
    }

    hOverlayWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        windowNameTemp.c_str(),
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, wc.hInstance, NULL
    );

    if (!hOverlayWnd) {
        std::cerr << "[Overlay] Failed to create window." << std::endl;
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return;
    }

    ShowWindow(hOverlayWnd, SW_SHOW);
}

std::string GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "";
    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));
    return std::string(title);
}

bool isValorantActive() {
    return GetActiveWindowTitle().find("VALORANT") != std::string::npos;
}

void checkActiveWindowThread() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        bool isActive = isValorantActive();
        if (hOverlayWnd) {
            if (isActive && !IsWindowVisible(hOverlayWnd)) {
                ShowWindow(hOverlayWnd, SW_SHOW);
                std::cout << "[Overlay] Revealing." << std::endl;
            }
            else if (!isActive && IsWindowVisible(hOverlayWnd)) {
                ShowWindow(hOverlayWnd, SW_HIDE);
                std::cout << "[Overlay] Hiding." << std::endl;
            }
        }
        if (!isActive) {
            toggle = false;
        }
    }
}

void drawOverlay() {
    if (!hOverlayWnd) return;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(NULL, hdcScreen);
        std::cerr << "[Overlay] CreateCompatibleDC failed: " << GetLastError() << std::endl;
        return;
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = screenWidth;
    bmi.bmiHeader.biHeight = -screenHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!hBitmap) {
        std::cerr << "[Overlay] CreateDIBSection failed: " << GetLastError() << std::endl;
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    int centerX = monitor1Center.x;
    int centerY = monitor1Center.y;
    int radius = config.fovRadius;

    int left = centerX - radius;
    int top = centerY - radius;
    int right = centerX + radius;
    int bottom = centerY + radius;

    HPEN hPen = CreatePen(PS_SOLID, 2, toggle ? RGB(0, 200, 200) : RGB(128, 128, 128));
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));

    SelectObject(hdcMem, hPen);
    SelectObject(hdcMem, hBrush);

    Ellipse(hdcMem, left, top, right, bottom);

    DeleteObject(hPen);
    DeleteObject(hBrush);

    SIZE sizeWnd = { screenWidth, screenHeight };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    BOOL res = UpdateLayeredWindow(hOverlayWnd, hdcScreen, NULL, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    if (!res) {
        std::cerr << "[Overlay] UpdateLayeredWindow failed: " << GetLastError() << std::endl;
    }

    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

// ========== Main Loop ==========

void drawLoop() {
    while (running) {
        drawOverlay();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
template <typename T>
bool IsInBounds(const T& value, const T& low, const T& high) {
    return !(value < low) && (value < high);
}
int main() {
    std::cout << "VTB C++/DirectX 11 Edition" << std::endl;
    std::cout << "(why the fuck did i make this..)" << std::endl << std::endl;
    SetProcessDPIAware();
    std::cout << "DPI awareness set" << std::endl;

    UpdatePrimaryMonitorCenter();
    std::cout << "Primary monitor center: " << monitor1Center.x << ", " << monitor1Center.y << std::endl;

    std::cout << "Initializing overlay" << std::endl;
    createOverlayWindow();
    if (!hOverlayWnd) {
        std::cerr << "Failed to create overlay window. Exiting." << std::endl;
        return 1;
    }
    std::cout << "Overlay initialization done, rendering first frame for UpdateLayeredWindow() check" << std::endl;
    drawOverlay();

    std::cout << "Initializing Interception Driver" << std::endl;
    context = interception_create_context();
    if (!context) {
        std::cerr << "Interception context creation failed. Exiting." << std::endl;
        return 1;
    }
    std::cout << "Interception context created." << std::endl;

    std::cout << "Setting Interception Filters" << std::endl;
    interception_set_filter(context, interception_is_keyboard, INTERCEPTION_FILTER_KEY_ALL);
    //interception_set_filter(context, interception_is_mouse, INTERCEPTION_FILTER_MOUSE_ALL);
    std::cout << "Interception filters set." << std::endl;

    std::cout << "Starting routines" << std::endl;
    std::thread detectThread(detectionLoop);
    detectThread.detach();

    std::thread drawLoopInactive(drawLoop);
    drawLoopInactive.detach();

    std::thread ForegroundCheckThread(checkActiveWindowThread);
    ForegroundCheckThread.detach();

    std::cout << "Starting message loop" << std::endl << std::endl;
    std::cout << "F = Main Toggle" << std::endl;
    std::cout << "F2/F3 = Decrease/Increase FOV" << std::endl;
    std::cout << "F7 = Toggle AAssist" << std::endl;
    std::cout << "F8 = Toggle Disable after first detection" << std::endl;
    std::cout << "Arrow Up/Down = Increase/Decrease Yellow Pixel Threshold" << std::endl << std::endl;

    InterceptionStroke stroke;
    MSG msg = { 0 };

    while (running) {
        BOOL gotMessage = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
        if (gotMessage) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            // Process Interception events when no Windows messages are waiting
            InterceptionDevice device = interception_wait(context); // Wait with a small timeout
            if (device != 0) {
                if (interception_receive(context, device, &stroke, 1) > 0) {
                    InterceptionKeyStroke& key = *(InterceptionKeyStroke*)&stroke;
                    //check if keycode is in range
                    if (IsInBounds((int)key.code, 32, 67)) {
                        //std::cout << "Key: " << key.code << ", " << key.state << std::endl;
                        if (key.state == 0 || key.state == 3) { // Key down or repeat
                            switch (key.code) {
                            case 33: // F1
                                if (hOverlayWnd && IsWindowVisible(hOverlayWnd)) {
                                    toggle = !toggle;
                                    std::cout << "[Main Toggle] " << (toggle ? "ON" : "OFF") << std::endl;
                                }
                                break;
                            case 60: // F2
                                config.fovRadius = max(1, config.fovRadius - 1);
                                std::cout << "[FOV] " << config.fovRadius << std::endl;
                                break;
                            case 61: // F3
                                config.fovRadius++;
                                std::cout << "[FOV] " << config.fovRadius << std::endl;
                                break;
                            case 65: // F7
                                config.useAA = !config.useAA;
                                std::cout << "[AAssist] " << (config.useAA ? "ON" : "OFF") << std::endl;
                                break;
                            case 66: // F8
                                config.disableAfterOne = !config.disableAfterOne;
                                std::cout << "[Disable after one Shot] " << (config.disableAfterOne ? "ON" : "OFF") << std::endl;
                                break;
                            case 72: // Arrow Up
                                config.requiredThreshold++;
                                std::cout << "[Threshold] " << config.requiredThreshold << std::endl;
                                break;
                            case 80: // Arrow Down
                                config.requiredThreshold = max(0, config.requiredThreshold - 1);
                                std::cout << "[Threshold] " << config.requiredThreshold << std::endl;
                                break;
                            }
                        }
                    }

                    if (keyboard == 0 && interception_is_keyboard(device))
                        keyboard = device;

                    //if (mouse == 0 && interception_is_mouse(device))
                    //    mouse = device;

                    interception_send(context, device, &stroke, 1);
                }
            }
        }
    }

    // Clean up
    if (context) {
        interception_destroy_context(context);
    }
    if (hOverlayWnd) {
        DestroyWindow(hOverlayWnd);
        WNDCLASSEX wc;
        if (GetClassInfoEx(GetModuleHandle(NULL), generateRandomWString(16).c_str(), &wc)) {
            UnregisterClass(wc.lpszClassName, wc.hInstance);
        }
    }

    return 0;
}
