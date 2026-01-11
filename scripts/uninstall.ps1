$installDir = "$env:ProgramFiles\libchttpx"

Remove-Item $installDir -Recurse -Force

[Environment]::SetEnvironmentVariable("CPATH", $null, "Machine")
[Environment]::SetEnvironmentVariable("LIBRARY_PATH", $null, "Machine")

Write-Host "libchttpx removed"
