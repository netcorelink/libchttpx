$ErrorActionPreference = 'Stop'

$installDir = "$env:ProgramFiles\libchttpx"

Write-Host "Installing libchttpx to $installDir"

if (!(Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir | Out-Null
}

Copy-Item "$PSScriptRoot\libchttpx.dll" $installDir
Copy-Item "$PSScriptRoot\libchttpx.a" $installDir
Copy-Item "$PSScriptRoot\include" $installDir -Recurse

$env:Path += ";$installDir"

Write-Host "libchttpx installed successfully!"
Write-Host "Use it via:"
Write-Host "  gcc main.c -I$installDir\include -L$installDir -lchttpx -lws2_32"
