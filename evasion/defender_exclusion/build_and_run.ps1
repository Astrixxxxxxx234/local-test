# Build and run exclusion.cpp
# Run this from an elevated PowerShell (Admin)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = "$scriptDir\exclusion.cpp"
$out = "$scriptDir\exclusion.exe"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -property installationPath
    $clPath = Get-ChildItem "$vsPath\VC\Tools\MSVC" -Recurse -Filter "cl.exe" |
              Where-Object { $_.FullName -match "x64\\cl.exe" } |
              Select-Object -First 1 -ExpandProperty FullName

    if ($clPath) {
        Write-Host "[*] Found MSVC: $clPath"
        & $clPath $src advapi32.lib /Fe:$out /nologo
    } else {
        Write-Host "[!] cl.exe not found, trying MinGW..."
        & g++ -o $out $src -ladvapi32
    }
} else {
    Write-Host "[*] No MSVC found, trying MinGW (g++)..."
    & g++ -o $out $src -ladvapi32
}

if (Test-Path $out) {
    Write-Host "[+] Compiled OK: $out"
    Write-Host "[*] Running..."
    & $out
} else {
    Write-Host "[!] Compile failed - no output file found"
    Write-Host "[!] Make sure Visual Studio or MinGW is installed"
}
