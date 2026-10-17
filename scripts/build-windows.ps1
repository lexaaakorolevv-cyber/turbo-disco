param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Generator = 'Visual Studio 17 2022',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Run this script on Windows x64.' }

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Get-Zip {
    param([string]$Url, [string]$Archive, [string]$Destination)
    Invoke-WebRequest -Uri $Url -OutFile $Archive
    Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
}

$BotRoot = Split-Path -Parent $PSScriptRoot
$BotDeps = Join-Path $BotRoot 'deps'
$BotBuild = Join-Path $BotRoot 'build/windows-x64'
New-Item -ItemType Directory -Force -Path $BotDeps | Out-Null
$BotSdk = if ($env:GEODE_SDK) { $env:GEODE_SDK } else { Join-Path $BotDeps 'geode' }
$BotBindings = if ($env:GEODE_BINDINGS_REPO_PATH) { $env:GEODE_BINDINGS_REPO_PATH } else { Join-Path $BotDeps 'bindings' }
$BindingsCommit = '7407717c307d1cd94461786210b330ab868f4260'
if (-not (Test-Path (Join-Path $BotSdk 'CMakeLists.txt'))) {
    Invoke-Checked 'git' @('clone', '--depth', '1', '--branch', 'v5.11.2', 'https://github.com/geode-sdk/geode.git', $BotSdk)
}
if ((Get-Content (Join-Path $BotSdk 'VERSION') -Raw).Trim() -ne '5.11.2') {
    throw 'Use the Geode v5.11.2 SDK.'
}
if (-not (Test-Path (Join-Path $BotBindings '.git'))) {
    Invoke-Checked 'git' @('clone', 'https://github.com/geode-sdk/bindings.git', $BotBindings)
    Invoke-Checked 'git' @('-C', $BotBindings, 'checkout', $BindingsCommit)
}
$ActualBindingsCommit = (& git -C $BotBindings rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ActualBindingsCommit -ne $BindingsCommit) {
    throw "Bindings must be pinned to $BindingsCommit. Existing checkout was not modified."
}

$BotCliDirectory = Join-Path $BotDeps 'cli-3.9.0'
$BotCli = Join-Path $BotCliDirectory 'geode.exe'
if (-not (Test-Path $BotCli)) {
    Get-Zip 'https://github.com/geode-sdk/cli/releases/download/v3.9.0/geode-cli-v3.9.0-win.zip' (Join-Path $BotDeps 'cli.zip') $BotCliDirectory
}
$BotSdkBin = Join-Path $BotSdk 'bin/5.11.2'
if (-not (Test-Path (Join-Path $BotSdkBin 'Geode.lib'))) {
    $BotLoaderDirectory = Join-Path $BotDeps 'loader-5.11.2'
    Get-Zip 'https://github.com/geode-sdk/geode/releases/download/v5.11.2/geode-v5.11.2-win.zip' (Join-Path $BotDeps 'loader.zip') $BotLoaderDirectory
    New-Item -ItemType Directory -Force -Path $BotSdkBin | Out-Null
    Copy-Item (Join-Path $BotLoaderDirectory 'Geode.lib') $BotSdkBin
}
Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'audit-bindings.py'), (Join-Path $BotBindings 'bindings/2.2081/GeometryDash.bro'))

if (-not $SkipTests) {
    $BotTests = Join-Path $BotRoot 'build/tests-windows'
    Invoke-Checked 'cmake' @('-S', $BotRoot, '-B', $BotTests, '-G', $Generator, '-A', 'x64', '-DBOT_CORE_TESTS_ONLY=ON')
    Invoke-Checked 'cmake' @('--build', $BotTests, '--config', 'Debug', '--parallel', '4')
    Invoke-Checked 'ctest' @('--test-dir', $BotTests, '-C', 'Debug', '--output-on-failure')
}
Invoke-Checked 'cmake' @('-S', $BotRoot, '-B', $BotBuild, '-G', $Generator, '-A', 'x64',
    "-DGEODE_SDK=$BotSdk", "-DGEODE_BINDINGS_REPO_PATH=$BotBindings", "-DGEODE_CLI=$BotCli",
    '-DGEODE_TARGET_PLATFORM=Win64', '-DGEODE_DISABLE_PRECOMPILED_HEADERS=ON')
Invoke-Checked 'cmake' @('--build', $BotBuild, '--config', $Configuration, '--parallel', '4')
$BotPackage = Join-Path $BotBuild 'local.cube_learner.geode'
Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'verify-package.py'), $BotPackage)
$BotChecksum = (Get-FileHash -Algorithm SHA256 $BotPackage).Hash.ToLowerInvariant()
"$BotChecksum  local.cube_learner.geode" | Set-Content -Encoding ascii (Join-Path $BotBuild 'SHA256SUMS.txt')
Write-Host "Package: $BotPackage"
