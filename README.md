# local-test

Local testing repo for red team lab techniques — standalone components tested here before integration into the Mythic agent.

## Structure

```
evasion/
  defender_exclusion/     # Windows Defender exclusion via registry
    exclusion.cpp         # Standalone .exe version
    exclusion_dll.cpp     # DLL version for injection
    exclusion.py          # Python version (PS or winreg)
```

## Notes

- All techniques require Admin or SYSTEM unless noted
- Test in isolated VM only
- Verify in regedit: HKLM\SOFTWARE\Microsoft\Windows Defender\Exclusions\Paths
