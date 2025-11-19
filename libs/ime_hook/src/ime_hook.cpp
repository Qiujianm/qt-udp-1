#include "ime_hook/ime_hook.hpp"
#include <windows.h>
#include <imm.h>
#include <cstring>
#include <cstdio>

// Global callback
static IMECompositionCallback g_callback = nullptr;
static HHOOK g_callWndProcHook = nullptr;
static HHOOK g_getMessageHook = nullptr;
static HMODULE g_hModule = nullptr;

// Forward declaration
LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK GetMessageProc(int nCode, WPARAM wParam, LPARAM lParam);

// Debug logging function
static void DebugLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Output to debugger
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            DebugLog("[IME_HOOK] DLL loaded");
            break;
        case DLL_PROCESS_DETACH:
            UninstallHooks();
            DebugLog("[IME_HOOK] DLL unloaded");
            break;
    }
    return TRUE;
}

LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_callback) {
        CWPSTRUCT* cwp = reinterpret_cast<CWPSTRUCT*>(lParam);
        
        // Listen for WM_IME_COMPOSITION messages
        if (cwp->message == WM_IME_COMPOSITION) {
            DebugLog("[IME_HOOK] WM_IME_COMPOSITION received, hwnd=0x%p, lParam=0x%08X", cwp->hwnd, cwp->lParam);
            
            HIMC hIMC = ImmGetContext(cwp->hwnd);
            if (hIMC) {
                // Check if result string is available (user confirmed the input)
                if (cwp->lParam & GCS_RESULTSTR) {
                    DebugLog("[IME_HOOK] GCS_RESULTSTR flag set");
                    
                    // Get the size of the result string
                    DWORD dwSize = ImmGetCompositionString(hIMC, GCS_RESULTSTR, nullptr, 0);
                    DebugLog("[IME_HOOK] Result string size: %lu bytes", dwSize);
                    
                    if (dwSize > 0) {
                        // Allocate buffer for the result string
                        const int charCount = dwSize / sizeof(wchar_t);
                        wchar_t* buffer = new wchar_t[charCount + 1];
                        
                        // Get the result string
                        DWORD actualSize = ImmGetCompositionString(hIMC, GCS_RESULTSTR, buffer, dwSize);
                        if (actualSize > 0) {
                            buffer[charCount] = L'\0';
                            
                            DebugLog("[IME_HOOK] Result string: %d characters, first char: 0x%04X", charCount, buffer[0]);
                            
                            // Call the callback
                            if (g_callback) {
                                DebugLog("[IME_HOOK] Calling callback with %d characters", charCount);
                                g_callback(buffer, charCount, cwp->hwnd);
                                DebugLog("[IME_HOOK] Callback returned");
                            }
                        } else {
                            DebugLog("[IME_HOOK] Failed to get result string, error: %lu", GetLastError());
                        }
                        
                        delete[] buffer;
                    }
                } else {
                    DebugLog("[IME_HOOK] GCS_RESULTSTR flag not set, lParam=0x%08X", cwp->lParam);
                }
                
                ImmReleaseContext(cwp->hwnd, hIMC);
            } else {
                DebugLog("[IME_HOOK] Failed to get IME context, error: %lu", GetLastError());
            }
        }
    }
    return CallNextHookEx(g_callWndProcHook, nCode, wParam, lParam);
}

LRESULT CALLBACK GetMessageProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_callback && wParam == PM_REMOVE) {
        MSG* msg = reinterpret_cast<MSG*>(lParam);
        
        // Listen for WM_CHAR and WM_IME_CHAR messages
        if (msg->message == WM_CHAR || msg->message == WM_IME_CHAR) {
            wchar_t ch = static_cast<wchar_t>(msg->wParam);
            if (ch != 0 && ch != 0xFFFF) {
                DebugLog("[IME_HOOK] WM_CHAR/WM_IME_CHAR: 0x%04X (hwnd=0x%p)", ch, msg->hwnd);
                
                // Call the callback with single character
                if (g_callback) {
                    DebugLog("[IME_HOOK] Calling callback with WM_CHAR/WM_IME_CHAR");
                    g_callback(&ch, 1, msg->hwnd);
                }
            }
        }
    }
    return CallNextHookEx(g_getMessageHook, nCode, wParam, lParam);
}

extern "C" IME_HOOK_API bool InitializeIMEHook(IMECompositionCallback callback) {
    g_callback = callback;
    DebugLog("[IME_HOOK] InitializeIMEHook called");
    return true;
}

extern "C" IME_HOOK_API void UninitializeIMEHook() {
    UninstallHooks();
    g_callback = nullptr;
    DebugLog("[IME_HOOK] UninitializeIMEHook called");
}

extern "C" IME_HOOK_API bool InstallHooks() {
    if (!g_hModule) {
        DebugLog("[IME_HOOK] InstallHooks failed: g_hModule is null");
        return false;
    }
    
    DebugLog("[IME_HOOK] Installing hooks...");
    
    // Install WH_CALLWNDPROC hook for WM_IME_COMPOSITION messages
    g_callWndProcHook = SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, g_hModule, 0);
    if (!g_callWndProcHook) {
        DWORD error = GetLastError();
        DebugLog("[IME_HOOK] Failed to install WH_CALLWNDPROC hook, error: %lu", error);
        return false;
    }
    DebugLog("[IME_HOOK] WH_CALLWNDPROC hook installed");
    
    // Install WH_GETMESSAGE hook for WM_CHAR and WM_IME_CHAR messages
    g_getMessageHook = SetWindowsHookEx(WH_GETMESSAGE, GetMessageProc, g_hModule, 0);
    if (!g_getMessageHook) {
        DWORD error = GetLastError();
        DebugLog("[IME_HOOK] Failed to install WH_GETMESSAGE hook, error: %lu", error);
        UnhookWindowsHookEx(g_callWndProcHook);
        g_callWndProcHook = nullptr;
        return false;
    }
    DebugLog("[IME_HOOK] WH_GETMESSAGE hook installed");
    
    DebugLog("[IME_HOOK] All hooks installed successfully");
    return true;
}

extern "C" IME_HOOK_API void UninstallHooks() {
    if (g_callWndProcHook) {
        UnhookWindowsHookEx(g_callWndProcHook);
        g_callWndProcHook = nullptr;
        DebugLog("[IME_HOOK] WH_CALLWNDPROC hook uninstalled");
    }
    
    if (g_getMessageHook) {
        UnhookWindowsHookEx(g_getMessageHook);
        g_getMessageHook = nullptr;
        DebugLog("[IME_HOOK] WH_GETMESSAGE hook uninstalled");
    }
}

