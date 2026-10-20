/* Independent startup window. No game data, callbacks or executable bytes touched. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <string.h>
#include <bcrypt.h>
#include "splash_digest.h"
static HINSTANCE module;

/* Bind the embedded artwork to this build; external images are never opened. */
static int artwork_matches(void) {
    static const unsigned char expected[32] = MW4_SPLASH_SHA256_INITIALIZER;
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_BITMAP);
    if (!resource) return 0;
    DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    unsigned char *bytes = loaded ? LockResource(loaded) : NULL;
    if (!bytes || !size) return 0;
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[32]; int valid = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) return 0;
    if (BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) >= 0 &&
        BCryptHashData(hash, bytes, size, 0) >= 0 &&
        BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0)
        valid = memcmp(digest, expected, sizeof(digest)) == 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return valid;
}

static HBITMAP bitmap;
static BITMAP dimensions;
static const wchar_t class_name[] = L"MW4FarsiSplash14";

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        HDC source = CreateCompatibleDC(dc);
        if (source) {
            HGDIOBJ previous = SelectObject(source, bitmap);
            RECT client; GetClientRect(window, &client);
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, 0, 0, client.right, client.bottom, source, 0, 0,
                       dimensions.bmWidth, dimensions.bmHeight, SRCCOPY);
            SelectObject(source, previous); DeleteDC(source);
        }
        EndPaint(window, &ps); return 0;
    }
    case WM_TIMER: DestroyWindow(window); return 0;
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static DWORD WINAPI splash_worker(void *unused) {
    (void)unused;
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(NULL, path, 32768);
    if (!length || length >= 32768) return 1;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || _wcsicmp(slash + 1, L"sp26-cod.exe")) return 0;
    length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return 1;
    slash = wcsrchr(path, L'\\');
    if (!slash || (size_t)(slash - path) + 13 >= 32768) return 1;
    wcscpy(slash + 1, L"trdata2.bin");
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    if (!artwork_matches()) return 2;
    bitmap = LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bitmap) return 2; /* Artwork errors must not prevent the game from starting. */
    if (!GetObjectW(bitmap, sizeof(dimensions), &dimensions) ||
        dimensions.bmWidth <= 0 || dimensions.bmHeight <= 0) {
        DeleteObject(bitmap); return 2;
    }
    WNDCLASSW wc = {0}; wc.hInstance = module; wc.lpfnWndProc = window_proc;
    wc.lpszClassName = class_name;
    if (!RegisterClassW(&wc)) { DeleteObject(bitmap); return 3; }
    POINT cursor; GetCursorPos(&cursor);
    MONITORINFO monitor = { .cbSize = sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        UnregisterClassW(class_name, module); DeleteObject(bitmap); return 3;
    }
    RECT work = monitor.rcWork;
    int width = (work.right - work.left) * 4 / 5;
    if (width > 1280) width = 1280;
    int height = MulDiv(width, dimensions.bmHeight, dimensions.bmWidth);
    int max_height = (work.bottom - work.top) * 4 / 5;
    if (height > max_height) { height = max_height; width = MulDiv(height, dimensions.bmWidth, dimensions.bmHeight); }
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        class_name, L"MW4 Farsi v1.4", WS_POPUP,
        work.left + (work.right-work.left-width)/2, work.top + (work.bottom-work.top-height)/2,
        width, height, NULL, NULL, module, NULL);
    if (window) {
        ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window);
        if (SetTimer(window, 1, 5000, NULL)) {
            MSG message;
            while (GetMessageW(&message, NULL, 0, 0) > 0) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        } else DestroyWindow(window);
    }
    UnregisterClassW(class_name, module); DeleteObject(bitmap); bitmap = NULL;
    return 0;
}

__declspec(dllexport) void Mw4SplashInit(void) {}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance;
        DisableThreadLibraryCalls(instance);
        /* No UI and no waiting under the loader lock. Failure is nonfatal. */
        HANDLE thread = CreateThread(NULL, 0, splash_worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
