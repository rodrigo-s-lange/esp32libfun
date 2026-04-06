# setup-clangd.ps1
# Configures clangd for this ESP32 project by writing machine-specific settings
# to VS Code user settings (not workspace settings — those stay clean for git).
#
# What it does:
#   1. Finds the xtensa-esp-elf toolchain and builds a --query-driver glob.
#   2. Finds the Espressif clangd binary (optional but recommended).
#   3. Writes clangd.path and --query-driver into the VS Code user settings.json.
#
# Run once after cloning (or after updating the toolchain):
#   powershell -ExecutionPolicy Bypass -File tools/setup-clangd.ps1

param(
    [string]$IdfToolsPath = $env:IDF_TOOLS_PATH
)

# ---- Locate toolchain -------------------------------------------------------

if (-not $IdfToolsPath) {
    $IdfToolsPath = Join-Path $env:USERPROFILE ".espressif"
}

$ToolsDir   = Join-Path $IdfToolsPath "tools"
$XtensaBase = Join-Path $ToolsDir "xtensa-esp-elf"

if (-not (Test-Path $XtensaBase)) {
    Write-Error "xtensa-esp-elf not found: $XtensaBase"
    Write-Error "Set IDF_TOOLS_PATH or install ESP-IDF first."
    exit 1
}

$VersionDir = Get-ChildItem $XtensaBase -Directory |
              Sort-Object Name -Descending |
              Select-Object -First 1
if (-not $VersionDir) {
    Write-Error "No toolchain version found inside: $XtensaBase"
    exit 1
}

# Forward-slash path for clangd glob.
$BinDir       = ($VersionDir.FullName -replace '\\', '/') + "/xtensa-esp-elf/bin"
$DriverGlob   = "$BinDir/xtensa-esp*-elf-*"

# ---- Locate Espressif clangd (optional) ------------------------------------

$EspClangBase = Join-Path $ToolsDir "esp-clang"
$ClangdExe    = $null

if (Test-Path $EspClangBase) {
    $ClangdExe = Get-ChildItem "$EspClangBase\*\esp-clang\bin\clangd.exe" -Recurse -ErrorAction SilentlyContinue |
                 Sort-Object FullName -Descending |
                 Select-Object -First 1 -ExpandProperty FullName
}

# Also check C:\Espressif (global installer location).
if (-not $ClangdExe) {
    $ClangdExe = Get-Item "C:\Espressif\tools\esp-clang\*\esp-clang\bin\clangd.exe" -ErrorAction SilentlyContinue |
                 Sort-Object FullName -Descending |
                 Select-Object -First 1 -ExpandProperty FullName
}

# ---- Patch VS Code user settings -------------------------------------------

$UserSettingsPath = Join-Path $env:APPDATA "Code\User\settings.json"

$UserSettings = if (Test-Path $UserSettingsPath) {
    Get-Content $UserSettingsPath -Raw | ConvertFrom-Json
} else {
    [PSCustomObject]@{}
}

# Build clangd.arguments: keep existing flags, replace --query-driver.
# Keep any existing flags that are not managed by this script.
$ManagedPrefixes = @("--query-driver", "--compile-commands-dir", "--background-index")
$ExistingArgs = @($UserSettings.'clangd.arguments' | Where-Object {
    $arg = $_
    $arg -and -not ($ManagedPrefixes | Where-Object { $arg.StartsWith($_) })
})
$ExistingArgs += "--background-index"
$ExistingArgs += '--compile-commands-dir=${workspaceFolder}/build'
$ExistingArgs += "--query-driver=$DriverGlob"
$UserSettings | Add-Member -MemberType NoteProperty -Name 'clangd.arguments' -Value $ExistingArgs -Force

if ($ClangdExe) {
    $ClangdExeFwd = $ClangdExe -replace '\\', '/'
    $UserSettings | Add-Member -MemberType NoteProperty -Name 'clangd.path' -Value $ClangdExeFwd -Force
    Write-Host "clangd:   $ClangdExeFwd"
} else {
    Write-Host "clangd:   (not found - using VS Code extension default)"
}

$UserSettings | ConvertTo-Json -Depth 10 |
    Set-Content -Path $UserSettingsPath -Encoding UTF8

Write-Host "driver:   $DriverGlob"
Write-Host "Updated:  $UserSettingsPath"
Write-Host ""
Write-Host "Restart the clangd language server in VS Code to apply:"
Write-Host "  Ctrl+Shift+P -> 'clangd: Restart Language Server'"
