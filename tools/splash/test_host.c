/* Run only in an isolated fixture folder, never as the real game. */
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "hold")) { Sleep(15000); return 0; }
    int expected = argc > 1 && !strcmp(argv[1], "visible");
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    void *owned = VirtualAlloc(base + 0xf00000, 0x10000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (owned != base + 0xf00000) return 1;
    unsigned char *target = base + 0xf0cef0;
    memset(target, 0x90, 32); target[16] = 0xc3;
    HMODULE hook = LoadLibraryW(L".\\trhook.dll");
    if (!hook) { printf("load_error=%lu\n", GetLastError()); return 2; }
    int dependencies = GetModuleHandleW(L"rtlcompanion.dll") != NULL &&
                       GetModuleHandleW(L"mw4splash.dll") != NULL;
    int installed = target[0] == 0xff && target[1] == 0x25;
    ULONGLONG start = GetTickCount64(), shown = 0, closed = 0;
    int stole_focus = 0;
    while (GetTickCount64() - start < 6000) {
        HWND window = FindWindowW(L"MW4FarsiSplash14", NULL);
        DWORD pid = 0; if (window) GetWindowThreadProcessId(window, &pid);
        int visible = window && pid == GetCurrentProcessId() && IsWindowVisible(window);
        if (visible) {
            if (!shown) shown = GetTickCount64();
            if (GetForegroundWindow() == window) stole_focus = 1;
        } else if (shown) { closed = GetTickCount64(); break; }
        if (!expected && GetTickCount64() - start > 1500) break;
        Sleep(10);
    }
    ULONGLONG duration = shown && closed ? closed - shown : 0;
    int ok = dependencies && installed && !stole_focus &&
             (expected ? duration >= 4800 && duration <= 5500 : !shown);
    char result[256];
    int length = snprintf(result, sizeof(result), "dependencies=%d hook_installed=%d shown=%d duration_ms=%llu stole_focus=%d passed=%d\n",
           dependencies, installed, shown != 0, duration, stole_focus, ok);
    /* The existing translation hook can close the console; use a test-only file too. */
    HANDLE file = CreateFileW(L"host-result.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 4;
    DWORD written = 0; WriteFile(file, result, (DWORD)length, &written, NULL); CloseHandle(file);
    if (written != (DWORD)length) return 4;
    printf("%s", result);
    return ok ? 0 : 3;
}
