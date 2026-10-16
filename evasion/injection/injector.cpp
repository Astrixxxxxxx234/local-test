#include <windows.h>
#include <tlhelp32.h>
#include <iostream>

// DLL Injector - injects a DLL into a target process by name
// Requires: Admin privileges
// Usage: injector.exe <process_name> <dll_path>
// Example: injector.exe notepad.exe C:\projects\chimera-local-test\exclusion.dll

DWORD GetProcessId(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}

bool InjectDLL(DWORD pid, const wchar_t* dllPath) {
    size_t pathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    // Open target process
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
    if (!hProcess) {
        std::wcerr << L"[!] OpenProcess failed. Error: " << GetLastError() << std::endl;
        return false;
    }
    std::wcout << L"[+] Opened process PID: " << pid << std::endl;

    // Allocate memory in target process for DLL path
    LPVOID remoteBuffer = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuffer) {
        std::wcerr << L"[!] VirtualAllocEx failed. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::wcout << L"[+] Allocated memory in target process" << std::endl;

    // Write DLL path into target process memory
    if (!WriteProcessMemory(hProcess, remoteBuffer, dllPath, pathSize, NULL)) {
        std::wcerr << L"[!] WriteProcessMemory failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::wcout << L"[+] Wrote DLL path to target process memory" << std::endl;

    // Get address of LoadLibraryW in kernel32.dll (same across processes)
    LPVOID loadLibAddr = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLibAddr) {
        std::wcerr << L"[!] GetProcAddress failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::wcout << L"[+] Found LoadLibraryW at: " << loadLibAddr << std::endl;

    // Create remote thread in target process to call LoadLibraryW with our DLL path
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

    // Cleanup
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        std::wcerr << L"[!] DLL load failed in target process (LoadLibrary returned NULL)" << std::endl;
        return false;
    }

    std::wcout << L"[+] DLL injected successfully!" << std::endl;
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3) {
        std::wcout << L"Usage: injector.exe <process_name> <dll_path>" << std::endl;
        std::wcout << L"Example: injector.exe notepad.exe C:\\projects\\exclusion.dll" << std::endl;
        return 1;
    }

    const wchar_t* targetProcess = argv[1];
    const wchar_t* dllPath = argv[2];

    std::wcout << L"[*] Target process: " << targetProcess << std::endl;
    std::wcout << L"[*] DLL path: " << dllPath << std::endl;

    DWORD pid = GetProcessId(targetProcess);
    if (pid == 0) {
        std::wcerr << L"[!] Process not found: " << targetProcess << std::endl;
        return 1;
    }
    std::wcout << L"[+] Found process PID: " << pid << std::endl;

    if (!InjectDLL(pid, dllPath)) {
        std::wcerr << L"[!] Injection failed" << std::endl;
        return 1;
    }

    return 0;
}

// Compile:
//   MSVC:  cl injector.cpp /Fe:injector.exe /nologo
//   MinGW: g++ -o injector.exe injector.cpp
