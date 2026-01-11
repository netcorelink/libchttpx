$ErrorActionPreference = 'Stop'

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$installDir = "$env:ProgramFiles\libchttpx"
$cjsonDir = "$toolsDir\lib\cjson"

Write-Host "Installing libchttpx to $installDir"

if (!(Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}

# Create lib dir
New-Item -ItemType Directory -Path "$installDir\lib" -Force | Out-Null

if (!(Test-Path $cjsonDir)) {
    New-Item -ItemType Directory -Path $cjsonDir -Force | Out-Null
}

# cjson
$cjson_h = "$cjsonDir\cJSON.h"
$cjson_c = "$cjsonDir\cJSON.c"

if (!(Test-Path $cjson_h)) {
    Write-Host "Downloading cjson..."
    Invoke-WebRequest "https://raw.githubusercontent.com/DaveGamble/cJSON/refs/heads/master/cJSON.h" -OutFile $cjson_h
    Invoke-WebRequest "https://raw.githubusercontent.com/DaveGamble/cJSON/refs/heads/master/cJSON.c" -OutFile $cjson_c
}

Copy-Item "$PSScriptRoot\libchttpx.dll" $installDir
Copy-Item "$PSScriptRoot\libchttpx.a" $installDir
Copy-Item "$PSScriptRoot\include" $installDir -Recurse
Copy-Item "$cjsonDir" "$installDir\lib\cjson" -Recurse -Force

# Set environment variables
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