$tmp = "$env:TEMP\libchttpx.zip"
$installDir = "$env:ProgramFiles\libchttpx"
$url = "https://github.com/netcorelink/libchttpx/releases/latest/download/libchttpx-win64.zip"

Write-Host "Installing libchttpx to $installDir"

Invoke-WebRequest $url -OutFile $tmp
Expand-Archive $tmp $installDir -Force

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
