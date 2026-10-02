<#
.SYNOPSIS
    组装安装器用的 payload（安装素材）。

.DESCRIPTION
    为什么要有这个脚本：payload 之前是**手工 robocopy 拼**出来的，于是两次踩坑 ——
      1. 取错了目录：用了源码仓 `123\flutter_app\build\...` 里 09-29 的旧构建产物，
         导致"新装的应用顶着旧图标、托盘 bug 也没修好"（那次的锅全在这里）；
      2. 漏了图标文件：`app_icon.ico` 只存在于 `windows\runner\resources\`，
         不在 Flutter assets 里，手工拷很容易漏，而它正是快捷方式图标的来源。
    把组装过程脚本化，这两类错误就不会再犯。

.PARAMETER AppSource
    指定 App 产物的 Release 目录；不给就自动找（优先打包目录，其次源码仓）。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tool\build-payload.ps1
#>
[CmdletBinding()]
param(
    [string]$AppSource
)

$ErrorActionPreference = 'Stop'

$ToolDir   = $PSScriptRoot
$ProjRoot  = Split-Path $ToolDir -Parent
$Payload   = Join-Path $ProjRoot 'payload'
$RepoRoot  = 'F:\ai\flutter\123'

function Write-Step([string]$m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok([string]$m)   { Write-Host "    $m" -ForegroundColor Green }
function Write-Warn2([string]$m) { Write-Host "    $m" -ForegroundColor Yellow }

# ── 1) 找 App 产物：优先打包目录（那里才是发布用的构建）────────────────────
if (-not $AppSource) {
    $candidates = @(
        'F:\ai\flutter\flutter-app\flutter_app\build\windows\x64\runner\Release',
        (Join-Path $RepoRoot 'flutter_app\build\windows\x64\runner\Release')
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $c 'LxAI.exe')) { $AppSource = $c; break }
    }
}
if (-not $AppSource -or -not (Test-Path -LiteralPath (Join-Path $AppSource 'LxAI.exe'))) {
    throw "找不到 App 产物（LxAI.exe）。请先 flutter build windows --release，或用 -AppSource 指定目录。"
}
Write-Step "App 产物：$AppSource"
$appExe = Get-Item -LiteralPath (Join-Path $AppSource 'LxAI.exe')
Write-Ok ("LxAI.exe  {0} B  构建于 {1:yyyy-MM-dd HH:mm:ss}" -f $appExe.Length, $appExe.LastWriteTime)
if ($AppSource -like "$RepoRoot*") {
    Write-Warn2 "⚠ 用的是源码仓里的产物 —— 它可能比打包目录旧（这正是上次图标没换的原因）"
    Write-Warn2 "  发布前请确认：cd F:\ai\flutter\flutter-app && flutter build windows --release"
}

# ── 2) 铺 payload ─────────────────────────────────────────────────────────
Write-Step '组装 payload'
New-Item -ItemType Directory -Force -Path $Payload | Out-Null
foreach ($d in 'app', 'python') {
    $t = Join-Path $Payload $d
    if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $t | Out-Null
}

Write-Host '    → App 产物'
robocopy $AppSource (Join-Path $Payload 'app') /E /XJ /R:1 /W:1 /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy(app) 失败，退出码 $LASTEXITCODE" }

Write-Host '    → 私有 Python 运行时'
$pySrc = Join-Path $RepoRoot 'installer\runtime\python'
if (Test-Path -LiteralPath (Join-Path $pySrc 'python.exe')) {
    robocopy $pySrc (Join-Path $Payload 'python') /E /XJ /R:1 /W:1 /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy(python) 失败，退出码 $LASTEXITCODE" }
} else {
    Write-Warn2 "私有 Python 运行时不存在（$pySrc）—— 先跑 installer\prepare-runtime.ps1"
}

Write-Host '    → 许可文本'
foreach ($f in 'NOTICE', 'TERMS.md', 'PRIVACY.md') {
    $p = Join-Path $RepoRoot $f
    if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $Payload -Force }
}

Write-Host '    → 应用图标（快捷方式图标的来源）'
$icoSrc = Join-Path $RepoRoot 'flutter_app\windows\runner\resources\app_icon.ico'
if (Test-Path -LiteralPath $icoSrc) {
    Copy-Item -LiteralPath $icoSrc -Destination (Join-Path $Payload 'app_icon.ico') -Force
    Write-Ok "app_icon.ico  $((Get-Item -LiteralPath $icoSrc).Length) B  SHA256 $((Get-FileHash -LiteralPath $icoSrc -Algorithm SHA256).Hash.Substring(0,16))"
} else {
    Write-Warn2 "找不到 app_icon.ico（$icoSrc）—— 快捷方式会回退成 exe 内嵌图标"
}

# ── 3) 自检 ───────────────────────────────────────────────────────────────
Write-Step '自检'
$checks = @{
    'app\LxAI.exe'          = '主程序'
    'python\python.exe'     = '私有运行时'
    'app_icon.ico'          = '快捷方式图标'
    'NOTICE'                = '许可声明'
}
$bad = 0
foreach ($k in $checks.Keys) {
    $p = Join-Path $Payload $k
    if (Test-Path -LiteralPath $p) { Write-Ok ("✔ {0,-22} {1}" -f $k, $checks[$k]) }
    else { Write-Warn2 ("✘ {0,-22} {1}" -f $k, $checks[$k]); $bad++ }
}

$all = @(Get-ChildItem -LiteralPath $Payload -Recurse -File)
Write-Host ''
Write-Host ("payload：{0} 个文件 / {1:N1} MB  →  {2}" -f $all.Count, (($all | Measure-Object Length -Sum).Sum / 1MB), $Payload) -ForegroundColor Green

# 图标内容哈希（安装器会用同样的规则给快捷方式生成 app_icon_<hash>.ico）
if (Test-Path -LiteralPath (Join-Path $Payload 'app_icon.ico')) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $Payload 'app_icon.ico'))
    # ⚠️ 必须用 [int64] 运算再取模 2^32：
    #    PowerShell 里 0xFFFFFFFF 会被当成 Int32 的 -1，拿它做 -band 等于**不截断**，
    #    下一步转 uint32 就会抛"值太大/太小"。这个坑在脚本层踩一次就够了。
    [int64]$h = 2166136261
    foreach ($b in $bytes) {
        $h = $h -bxor $b
        $h = ($h * 16777619) % 4294967296
    }
    Write-Host ("图标内容哈希（FNV-1a）：{0:x8}  →  安装后快捷方式将指向 app_icon_{0:x8}.ico" -f [uint32]$h) -ForegroundColor Green
}

if ($bad -gt 0) { throw "payload 自检未通过：有 $bad 项缺失" }
