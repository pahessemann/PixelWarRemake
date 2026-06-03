param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [string]$ConfigFile = ""
)

$ToolBin = Join-Path (Get-Location) ".tools\w64devkit\bin"
if (Test-Path $ToolBin) {
    $env:PATH = "$ToolBin;$env:PATH"
}

$exe = Join-Path $BuildDir "pixelwar_server.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $BuildDir "$Config/pixelwar_server.exe"
}

if ([string]::IsNullOrWhiteSpace($ConfigFile)) {
    if (Test-Path "config/server.json") {
        $ConfigFile = "config/server.json"
    } else {
        $ConfigFile = "config/server.example.json"
    }
}

& $exe $ConfigFile
