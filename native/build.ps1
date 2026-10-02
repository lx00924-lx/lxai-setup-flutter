<#
.SYNOPSIS
    构建原生安装器（C++ + WebView2）。

.DESCRIPTION
    步骤：拉 WebView2 SDK（nuget，已缓存则跳过）→ CMake 配置 → MSBuild 编译 → 报告产物。

.PARAMETER Clean
    先删掉 build 目录再来一遍。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File native\build.ps1
#>
[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$NativeDir = $PSScriptRoot
$BuildDir  = Join-Path $NativeDir 'build'
$ThirdParty = Join-Path $NativeDir 'third_party'
$Wv2Version = '1.0.4258.31'
$Wv2Dir = Join-Path $ThirdParty "Microsoft.Web.WebView2.$Wv2Version"

# VS 自带这两个工具，但都不在 PATH 上（干净环境里 PATH 里连 node 都没有），所以用绝对路径
$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$CMake  = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

function Write-Step([string]$m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok([string]$m)   { Write-Host "    $m" -ForegroundColor Green }

if (-not (Test-Path -LiteralPath $CMake)) {
    $alt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Directory -EA SilentlyContinue |
           ForEach-Object { Join-Path $_.FullName 'Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' } |
           Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($alt) { $CMake = $alt } else { throw "找不到 cmake.exe（VS 的 CMake 组件没装？）" }
}

# ── 1) WebView2 SDK ───────────────────────────────────────────────────────
Write-Step 'WebView2 SDK'
if (Test-Path -LiteralPath (Join-Path $Wv2Dir 'build\native\include\WebView2.h')) {
    Write-Ok "已有 $Wv2Version（third_party 下）"
} else {
    New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null
    & nuget install Microsoft.Web.WebView2 -Version $Wv2Version -OutputDirectory $ThirdParty -NonInteractive |
        Select-Object -Last 2
    if (-not (Test-Path -LiteralPath (Join-Path $Wv2Dir 'build\native\include\WebView2.h'))) {
        throw 'WebView2 SDK 拉取失败'
    }
    Write-Ok "已拉取 $Wv2Version"
}

# ── 2) 配置 ───────────────────────────────────────────────────────────────
if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Step '清理 build 目录'
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

Write-Step 'CMake 配置'
& $CMake -S $NativeDir -B $BuildDir -G 'Visual Studio 18 2026' -A x64 2>&1 |
    Select-Object -Last 4
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（退出码 $LASTEXITCODE）" }

# ── 3) 编译 ───────────────────────────────────────────────────────────────
Write-Step '编译（Release x64）'
& $CMake --build $BuildDir --config Release --parallel 2>&1 | Select-Object -Last 8
if ($LASTEXITCODE -ne 0) { throw "编译失败（退出码 $LASTEXITCODE）" }

# ── 4) 报告 ───────────────────────────────────────────────────────────────
$exe = Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter 'LxAI-Setup.exe' -EA SilentlyContinue |
       Select-Object -First 1
if (-not $exe) { throw '编译过了但找不到产物 exe' }

Write-Host ''
Write-Ok ("产物：{0}" -f $exe.FullName)
Write-Ok ("大小：{0:N0} B（{1:N1} KB）" -f $exe.Length, ($exe.Length / 1KB))
Write-Ok '运行它即可看到界面（ui\index.html 在旁边，开发期直接从磁盘加载）'
