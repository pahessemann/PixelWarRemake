param(
    [string]$BuildDir = "build"
)

$ToolBin = Join-Path (Get-Location) ".tools\w64devkit\bin"
if (Test-Path (Join-Path $ToolBin "ctest.exe")) {
    $env:PATH = "$ToolBin;$env:PATH"
    & (Join-Path $ToolBin "ctest.exe") --test-dir $BuildDir --output-on-failure
} else {
    ctest --test-dir $BuildDir --output-on-failure
}
