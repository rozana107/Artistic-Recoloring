param(
    [string]$OpenCVDir = "",
    [string]$ImageAbstractionDir = "",
    [string]$ToolsRoot = 'D:\WorkStation\tools',
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $portableCompiler = Join-Path $ToolsRoot 'w64devkit\bin'
    $portableCMake = Join-Path $ToolsRoot 'cmake-3.31.8-windows-x86_64\bin\cmake.exe'
    if ((Test-Path -LiteralPath $portableCompiler) -and (Test-Path -LiteralPath $portableCMake)) {
        $env:PATH = $portableCompiler + ';' + $env:PATH
        $cmake = $portableCMake
        $generator = @('-G', 'MinGW Makefiles')
        if ($OpenCVDir -eq '') { $OpenCVDir = Join-Path $ToolsRoot 'opencv-install\x64\mingw\staticlib' }
    } else {
        $cmake = 'cmake'
        $generator = @()
    }
    $configure = @('-S', '.', '-B', 'build', '-D', 'CMAKE_BUILD_TYPE=Release') + $generator
    if ($Fresh) { $configure += '--fresh' }
    if ($OpenCVDir -ne '') { $configure += @('-D', "OpenCV_DIR=$OpenCVDir") }
    if ($ImageAbstractionDir -ne '') { $configure += @('-D', "IMAGE_ABSTRACTION_DIR=$ImageAbstractionDir") }
    & $cmake @configure
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build build --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    $ctest = Join-Path (Split-Path (Get-Command $cmake).Source) 'ctest.exe'
    & $ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Correctness checks failed.' }
} finally {
    Pop-Location
}
