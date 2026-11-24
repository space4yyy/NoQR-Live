# Requires 64-bit Windows, CMake 3.28+, and Ninja.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$versions = Get-Content "$root/config/versions.json" -Raw | ConvertFrom-Json
$deps = "$root/.deps"
New-Item -ItemType Directory -Path $deps -Force | Out-Null

function Get-VerifiedZip($Url, $Archive, $Sha256, $Destination, $ExpectedFile) {
    if (Test-Path "$Destination/$ExpectedFile" -PathType Leaf) { return }
    if (!(Test-Path $Archive) -or (Get-FileHash $Archive -Algorithm SHA256).Hash -ne $Sha256) {
        Invoke-WebRequest -Uri $Url -OutFile "$Archive.partial"
        if ((Get-FileHash "$Archive.partial" -Algorithm SHA256).Hash -ne $Sha256) {
            throw "SHA-256 mismatch: $Url"
        }
        Move-Item -LiteralPath "$Archive.partial" -Destination $Archive -Force
    }
    Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
    if (!(Test-Path "$Destination/$ExpectedFile" -PathType Leaf)) { throw "Missing $ExpectedFile in $Archive" }
}

$obs = "OBS-Studio-$($versions.obs)-Windows-x64"
$obsDirectory = "$deps/$obs"
$obsDll = "$obsDirectory/bin/64bit/obs.dll"
Get-VerifiedZip "https://github.com/obsproject/obs-studio/releases/download/$($versions.obs)/$obs.zip" `
    "$deps/$obs.zip" $versions.obsWindowsX64Sha256 $obsDirectory 'bin/64bit/obs.dll'

$toolchain = "llvm-mingw-$($versions.llvmMingw)-ucrt-x86_64"
$toolchainDirectory = "$deps/llvm-mingw"
$compilerPath = "$toolchain/bin/x86_64-w64-mingw32-clang++.exe"
$clang = "$toolchainDirectory/$compilerPath"
Get-VerifiedZip "https://github.com/mstorsjo/llvm-mingw/releases/download/$($versions.llvmMingw)/$toolchain.zip" `
    "$deps/$toolchain.zip" $versions.llvmMingwWindowsX64Sha256 $toolchainDirectory $compilerPath

$build = "$root/build/windows-x64"
$localSource = @()
if (Test-Path "$root/third_party/zxing-cpp/CMakeLists.txt") {
    $localSource = @("-DFETCHCONTENT_SOURCE_DIR_ZXING=$root/third_party/zxing-cpp")
}
cmake -S $root -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/windows-x64.cmake" `
    "-DCMAKE_CXX_COMPILER=$clang" "-DOBS_DLL=$obsDll" -DCMAKE_BUILD_TYPE=Release @localSource
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed' }
ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Windows smoke test failed' }
cmake --build $build --target package
if ($LASTEXITCODE -ne 0) { throw 'Windows packaging failed' }

$zip = "OBS-QR-Shield-$($versions.plugin)-Windows-x64.zip"
$hash = (Get-FileHash "$build/$zip" -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $zip" | Set-Content "$build/$zip.sha256" -Encoding ascii
Write-Output "Created: $build/$zip"
