#pragma once

#include <windows.h>

#ifdef IME_HOOK_EXPORTS
#define IME_HOOK_API __declspec(dllexport)
#else
#define IME_HOOK_API __declspec(dllimport)
#endif

// Callback function type for IME composition result
typedef void (*IMECompositionCallback)(const wchar_t* text, int length, HWND hwnd);

// Initialize the hook DLL
extern "C" IME_HOOK_API bool InitializeIMEHook(IMECompositionCallback callback);

// Uninitialize the hook DLL
extern "C" IME_HOOK_API void UninitializeIMEHook();

// Install global hooks
extern "C" IME_HOOK_API bool InstallHooks();

// Uninstall global hooks
extern "C" IME_HOOK_API void UninstallHooks();

