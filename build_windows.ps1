param(
    [ValidateSet("dev", "debug", "release", "package")]
    [string]$Mode = "dev",

    [ValidateSet("msys2", "msvc")]
    [string]$Toolchain = "msys2",

    [string]$Msys2Ucrt64 = $env:MSYS2_UCRT64,

    [switch]$Test,
    [switch]$Clean,
    [switch]$NoZip,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Usage {
    Write-Host @"
Usage:
  .\build_windows.ps1 [-Mode dev|debug|release|package] [options]

Commands:
  dev, debug  Build Debug under build\ (default; never writes dist\).
  release     Build Release under build\ (never writes dist\).
  package     Build Release, deploy DLL/QML dependencies, and write dist\.

Examples:
  .\build_windows.ps1 -Mode dev -Test
  .\build_windows.ps1 -Mode release -Jobs 8
  .\build_windows.ps1 -Mode package -Toolchain msys2

Options:
  -Toolchain msys2|msvc  Use MSYS2/UCRT64 Ninja (default) or Visual Studio.
  -Msys2Ucrt64 PATH      MSYS2 UCRT64 prefix.
  -Test                  Run CTest for a Debug build.
  -Clean                 Remove the selected build directory first.
  -NoZip                 Do not create a ZIP in package mode.
  -Jobs N                Parallel build jobs. Default: CPU core count.

Outputs:
  dev/release  build\windows-* only
  package      dist\MVPImageViewer-windows-x64 and a versioned ZIP
"@
}

function Find-ApplicationExecutable {
    param([Parameter(Mandatory = $true)][string]$Root)

    $candidate = Get-ChildItem -LiteralPath $Root -Filter "MVPImageViewer.exe" -File -Recurse |
        Where-Object { $_.FullName -notmatch "CMakeFiles|_autogen|tests" } |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "MVPImageViewer.exe was not produced under $Root"
    }
    return $candidate.FullName
}

function Find-WinDeployQt {
    param([string]$QtBin)

    foreach ($name in @("windeployqt6.exe", "windeployqt.exe")) {
        if (-not [string]::IsNullOrWhiteSpace($QtBin)) {
            $candidate = Join-Path $QtBin $name
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($null -ne $command) { return $command.Source }
    }
    throw "windeployqt was not found. Add the selected Qt bin directory to PATH."
}

function Copy-Msys2DependencyClosure {
    param(
        [Parameter(Mandatory = $true)][string]$TargetDir,
        [Parameter(Mandatory = $true)][string]$Prefix
    )

    $prefixBin = Join-Path $Prefix "bin"
    $objdump = Join-Path $prefixBin "objdump.exe"
    if (-not (Test-Path -LiteralPath $objdump)) {
        $command = Get-Command objdump.exe -ErrorAction SilentlyContinue
        if ($null -eq $command) {
            throw "objdump.exe is required to deploy the MSYS2 DLL dependency closure."
        }
        $objdump = $command.Source
    }

    $queue = [System.Collections.Generic.Queue[string]]::new()
    $visited = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem -LiteralPath $TargetDir -File -Recurse |
        Where-Object { $_.Extension -in @(".exe", ".dll") } |
        ForEach-Object { $queue.Enqueue($_.FullName) }

    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        if (-not $visited.Add($binary)) { continue }

        foreach ($line in (& $objdump -p $binary 2>$null)) {
            if ($line -notmatch "DLL Name:\s*(.+\.dll)\s*$") { continue }
            $dllName = $Matches[1].Trim()
            $target = Join-Path $TargetDir $dllName
            if (Test-Path -LiteralPath $target) { continue }

            $source = Join-Path $prefixBin $dllName
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination $target
                $queue.Enqueue($target)
            }
        }
    }
}

function Publish-WindowsPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$QtBin,
        [string]$MsysPrefix
    )

    $stageDir = Join-Path $ScriptDir "build\package-windows-staging"
    $distDir = Join-Path $ScriptDir "dist\MVPImageViewer-windows-x64"
    if (Test-Path -LiteralPath $stageDir) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageDir | Out-Null

    $targetExe = Join-Path $stageDir "MVPImageViewer.exe"
    Copy-Item -LiteralPath $Executable -Destination $targetExe
    $deployQt = Find-WinDeployQt -QtBin $QtBin
    Write-Host "Deploying Qt and QML dependencies with $deployQt"
    & $deployQt --release --compiler-runtime --qmldir (Join-Path $ScriptDir "src\qml") $targetExe
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

    # MSYS2's development layout stores QML modules under share\qt6, while
    # windeployqt copies them to qml\ beside the executable. Pin relocatable
    # runtime paths so a copied package never uses the build-time Qt prefix.
    Set-Content -LiteralPath (Join-Path $stageDir "qt.conf") -Encoding Ascii -Value @"
[Paths]
Prefix=.
Plugins=.
QmlImports=qml
Translations=translations
"@

    Write-Host "Pruning optional Qt styles, QML modules, and plugins"
    cmake "-DPACKAGE_ROOT=$stageDir" "-DPACKAGE_PLATFORM=windows" `
        -P (Join-Path $ScriptDir "scripts\prune_qt_runtime.cmake")
    if ($LASTEXITCODE -ne 0) { throw "Qt runtime pruning failed with exit code $LASTEXITCODE" }

    if (-not [string]::IsNullOrWhiteSpace($MsysPrefix)) {
        Copy-Msys2DependencyClosure -TargetDir $stageDir -Prefix $MsysPrefix
    }

    if (Test-Path -LiteralPath $distDir) {
        Remove-Item -LiteralPath $distDir -Recurse -Force
    }
    Copy-Item -LiteralPath $stageDir -Destination $distDir -Recurse

    $versionMatch = Select-String -LiteralPath (Join-Path $ScriptDir "CMakeLists.txt") `
        -Pattern "project\(ISPImageViewer VERSION ([0-9.]+)" | Select-Object -First 1
    $version = if ($null -ne $versionMatch) { $versionMatch.Matches[0].Groups[1].Value } else { "unknown" }
    $zipPath = Join-Path $ScriptDir "dist\MVPImageViewer-$version-windows-x64.zip"
    if (-not $NoZip) {
        if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
        Compress-Archive -LiteralPath $distDir -DestinationPath $zipPath -CompressionLevel Optimal
        $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Host "SHA-256: $hash"
        Write-Host "Distributable ZIP: $zipPath"
    }
    Write-Host "Distributable directory: $distDir"
}

if ($Help) {
    Show-Usage
    exit 0
}

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq "Core") {
    throw "build_windows.ps1 must be run on Windows."
}

# GNU windres uses ComSpec to launch its C preprocessor. Some automation
# environments omit it even on Windows, which otherwise surfaces as a
# misleading "can't popen ... Bad file descriptor" error.
if ([string]::IsNullOrWhiteSpace($env:ComSpec)) {
    $systemRoot = if ([string]::IsNullOrWhiteSpace($env:SystemRoot)) {
        $env:windir
    } else {
        $env:SystemRoot
    }
    $commandProcessor = Join-Path $systemRoot "System32\cmd.exe"
    if (Test-Path -LiteralPath $commandProcessor) {
        $env:ComSpec = $commandProcessor
        $env:Path = "$(Split-Path -Parent $commandProcessor);$env:Path"
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$Package = $Mode -eq "package"
$BuildMode = if ($Mode -in @("dev", "debug")) { "debug" } else { "release" }

if ($Toolchain -eq "msys2") {
    if ([string]::IsNullOrWhiteSpace($Msys2Ucrt64)) {
        $qmake = Get-Command qmake -ErrorAction SilentlyContinue
        if ($null -ne $qmake) {
            $Msys2Ucrt64 = (& $qmake.Source -query QT_INSTALL_PREFIX).Trim()
        }
    }
    if ([string]::IsNullOrWhiteSpace($Msys2Ucrt64) -or
        -not (Test-Path -LiteralPath (Join-Path $Msys2Ucrt64 "bin\qmake.exe"))) {
        throw "MSYS2 UCRT64 Qt was not found. Set MSYS2_UCRT64 or put qmake on PATH."
    }

    $Msys2Ucrt64 = (Resolve-Path -LiteralPath $Msys2Ucrt64).Path
    $qtBin = Join-Path $Msys2Ucrt64 "bin"
    $qtLibExecs = Join-Path $Msys2Ucrt64 "share\qt6\bin"
    $env:Path = "$qtLibExecs;$qtBin;$env:Path"
    $BuildDir = Join-Path $ScriptDir "build\windows-msys2-$BuildMode"
    if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
        Write-Host "Removing build directory: $BuildDir"
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }

    $configureArgs = @(
        "-S", $ScriptDir, "-B", $BuildDir, "-G", "Ninja",
        "-DCMAKE_PREFIX_PATH=$Msys2Ucrt64", "-DCMAKE_BUILD_TYPE=$BuildMode"
    )
    if ($BuildMode -eq "debug") {
        $configureArgs += "-DBUILD_TESTING=ON"
    } else {
        $configureArgs += @("-DBUILD_TESTING=OFF", "-DISPVIEW_BUILD_BENCHMARKS=ON")
    }

    Write-Host "Configuring MSYS2/UCRT64 build: $BuildDir"
    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }
    cmake --build $BuildDir --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

    if ($Test) {
        if ($BuildMode -eq "debug") {
            ctest --test-dir $BuildDir --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE" }
        } else {
            Write-Host "Release builds do not enable CTest; skipping -Test."
        }
    }

    $executable = Find-ApplicationExecutable -Root $BuildDir
    if ($Package) {
        Publish-WindowsPackage -Executable $executable -QtBin $qtBin -MsysPrefix $Msys2Ucrt64
    } else {
        Write-Host "Development executable: $executable"
        Write-Host "dist\ was not modified."
    }
    exit 0
}

$Preset = "windows-$BuildMode"
$BuildDir = Join-Path $ScriptDir "build\windows-$BuildMode"
if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host "Removing build directory: $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }
cmake --build --preset $Preset --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
if ($Test) {
    if ($BuildMode -eq "debug") {
        ctest --preset windows-debug --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE" }
    } else {
        Write-Host "Release builds do not enable CTest; skipping -Test."
    }
}

$executable = Find-ApplicationExecutable -Root $BuildDir
if ($Package) {
    $deployCommand = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    $qtBin = if ($null -ne $deployCommand) { Split-Path -Parent $deployCommand.Source } else { "" }
    Publish-WindowsPackage -Executable $executable -QtBin $qtBin
} else {
    Write-Host "Development executable: $executable"
    Write-Host "dist\ was not modified."
}
