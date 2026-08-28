# Install libchttpx from GitHub Releases (Windows).
#
# Latest:
#   iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
#
# Specific version:
#   iwr .../install.ps1 -UseBasicParsing | iex --% --version=1.5.5
#   powershell -ExecutionPolicy Bypass -File install.ps1 --version=1.5.5
#   powershell -ExecutionPolicy Bypass -File install.ps1 -v=1.5.5
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Version 1.5.5
#   $env:LIBCHTTPX_VERSION='1.5.5'; iwr .../install.ps1 -UseBasicParsing | iex

param(
    [Alias('v')]
    [string]$Version
)

function Show-InstallUsage {
    Write-Host @"
Usage: install.ps1 [options]

Options:
  -Version, -v VER       Release tag or version (e.g. 1.5.5 or v1.5.5)
  --version=VER, -v=VER Same as above
  --version VER         Same as above
  -h, --help            Show this help

Environment:
  LIBCHTTPX_VERSION     Version when piping to iex (e.g. 1.5.5)

Examples:
  iwr .../install.ps1 -UseBasicParsing | iex
  powershell -File install.ps1 --version=1.5.5
  `$env:LIBCHTTPX_VERSION='1.5.5'; iwr .../install.ps1 -UseBasicParsing | iex
"@
}

function Normalize-VersionTag([string]$Ver) {
    if ($Ver -match '^v') { return $Ver }
    return "v$Ver"
}

$i = 0
while ($i -lt $args.Count) {
    $arg = $args[$i]
    switch -Regex ($arg) {
        '^(-h|--help)$' {
            Show-InstallUsage
            exit 0
        }
        '^(--version|-v)=(.+)$' {
            $Version = $Matches[2]
        }
        '^(--version|-v)$' {
            $i++
            if ($i -ge $args.Count) {
                Write-Error "Error: $arg requires a value"
                exit 1
            }
            $Version = $args[$i]
        }
        default {
            Write-Error "Error: unknown option: $arg"
            Show-InstallUsage
            exit 1
        }
    }
    $i++
}

if (-not $Version -and $env:LIBCHTTPX_VERSION) {
    $Version = $env:LIBCHTTPX_VERSION
}

$tmp = "$env:TEMP\libchttpx.zip"
$installDir = "$env:ProgramFiles\libchttpx"

if ($Version) {
    $tag = Normalize-VersionTag $Version
    $url = "https://github.com/netcorelink/libchttpx/releases/download/$tag/libchttpx-win64.zip"
    Write-Host "Installing libchttpx $tag to $installDir"
}
else {
    $url = "https://github.com/netcorelink/libchttpx/releases/latest/download/libchttpx-win64.zip"
    Write-Host "Installing libchttpx (latest release) to $installDir"
}

Invoke-WebRequest $url -OutFile $tmp
Expand-Archive $tmp $installDir -Force
Remove-Item $tmp -Force -ErrorAction SilentlyContinue

[Environment]::SetEnvironmentVariable(
    "CPATH",
    "$installDir\include;$installDir\lib\cjson",
    "Machine"
)

[Environment]::SetEnvironmentVariable(
    "LIBRARY_PATH",
    "$installDir",
    "Machine"
)

Write-Host ""
Write-Host "libchttpx installed successfully!"
Write-Host ""
Write-Host "Usage:"
Write-Host "  gcc server.c -lchttpx -lws2_32"
Write-Host ""
Write-Host "Includes:"
Write-Host "  #include <libchttpx.h>"
Write-Host "  #include <cJSON.h>"
Write-Host ""
Write-Host "Restart terminal to apply environment variables."
