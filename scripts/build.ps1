param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug"
)

$ToolBin = Join-Path (Get-Location) ".tools\w64devkit\bin"
if (Test-Path (Join-Path $ToolBin "cmake.exe")) {
    $env:PATH = "$ToolBin;$env:PATH"
    & (Join-Path $ToolBin "cmake.exe") -S . -B $BuildDir -G "MinGW Makefiles" `
        -DCMAKE_CXX_COMPILER="$ToolBin\g++.exe" `
        -DCMAKE_MAKE_PROGRAM="$ToolBin\mingw32-make.exe" `
        -DCMAKE_BUILD_TYPE=$Config `
        -DCMAKE_CXX_SCAN_FOR_MODULES=OFF `
        -DPIXELWAR_BUILD_TESTS=ON
    & (Join-Path $ToolBin "cmake.exe") --build $BuildDir --parallel
} else {
    cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=$Config -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DPIXELWAR_BUILD_TESTS=ON
    cmake --build $BuildDir --config $Config --parallel
}
