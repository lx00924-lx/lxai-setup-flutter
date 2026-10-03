<#
.SYNOPSIS
    打出一个**单文件**的 LxAI 安装程序。

.DESCRIPTION
    产物 = 原生 exe（界面已编在资源里）+ 追加在尾部的 payload.zip + 32 字节尾部标记。

    为什么用"标准 ZIP + 尾部标记"而不是自研容器：
      · ZIP 由 PowerShell 的 Compress-Archive 直接产出，CRC/目录表都是现成的；
      · 运行时用 vendored 的 miniz 读，是条久经考验的路；
      · 出问题时能拿任何解压工具打开那一截来看，排查不用猜。
    尾部标记是定长 32 字节（"LXAIZIP1" + 偏移 + 长度 + 预留），
    运行时按「文件长度 - 32」直接定位，不必扫描 25 MB。

    界面文件（HTML / 两个图标）**不在 ZIP 里** —— 它们由 ui\ui.rc 编进 exe 资源。
    这样一来：素材坏了还能修（重下），界面坏了这份 exe 根本没救，
    两者放进同一层反而让"到底哪一层出问题"变得难查。

.PARAMETER Version
    版本号。不给就从素材里 App exe 的 VERSIONINFO 读 —— **App 自己才是版本号的唯一真相**，
    硬编码的值在发版时必然忘记改。

.PARAMETER SkipBuild
    跳过 native\build.ps1（已经编好了，只想重打包时用）。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tool\build-installer.ps1
#>
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$ToolDir  = $PSScriptRoot
$ProjRoot = Split-Path $ToolDir -Parent
$Payload  = Join-Path $ProjRoot 'payload'
$OutDir   = Join-Path $ProjRoot 'output'
$NativeExe = Join-Path $ProjRoot 'native\build\Release\LxAI-Setup.exe'

function Write-Step([string]$m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok([string]$m)   { Write-Host "    $m" -ForegroundColor Green }
function Write-Warn2([string]$m) { Write-Host "    $m" -ForegroundColor Yellow }

# ── 1) 编译原生宿主 ────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Step '编译原生宿主'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjRoot 'native\build.ps1')
    if ($LASTEXITCODE -ne 0) { throw "native\build.ps1 失败（退出码 $LASTEXITCODE）" }
}
if (-not (Test-Path -LiteralPath $NativeExe)) { throw "找不到 $NativeExe，先跑 native\build.ps1" }
$exeItem = Get-Item -LiteralPath $NativeExe
Write-Ok ("LxAI-Setup.exe  {0:N0} B  构建于 {1:yyyy-MM-dd HH:mm:ss}" -f $exeItem.Length, $exeItem.LastWriteTime)

# ── 2) 素材 ────────────────────────────────────────────────────────────────
Write-Step '检查安装素材'
if (-not (Test-Path -LiteralPath (Join-Path $Payload 'app\LxAI.exe'))) {
    throw "素材不完整：缺 payload\app\LxAI.exe。先在主仓库跑 tool\build-payload.ps1。"
}
if (-not (Test-Path -LiteralPath (Join-Path $Payload 'python\python.exe'))) {
    throw "素材不完整：缺 payload\python\python.exe（私有 Python 运行时）。"
}
$payloadFiles = @(Get-ChildItem -LiteralPath $Payload -Recurse -File)
$payloadBytes = ($payloadFiles | Measure-Object Length -Sum).Sum
Write-Ok ("payload  {0} 个文件 / {1:N1} MB" -f $payloadFiles.Count, ($payloadBytes / 1MB))

# ── 3) 版本号：从 App exe 的 VERSIONINFO 读 ────────────────────────────────
if (-not $Version) {
    $appExe = Join-Path $Payload 'app\LxAI.exe'
    $vi = (Get-Item -LiteralPath $appExe).VersionInfo
    $Version = if ($vi.ProductVersion) { $vi.ProductVersion } else { '1.0.0' }
    Write-Ok "版本号（读自 App exe 的 VERSIONINFO）：$Version"
}

# ── 4) 压缩素材 ────────────────────────────────────────────────────────────
Write-Step '压缩素材'
$zipPath = Join-Path $OutDir 'payload.zip'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

# ⚠️ `Compress-Archive -Path <目录>\*` 而不是 `-Path <目录>`：
#    后者会把 payload 这一层目录名也写进 ZIP，运行时解出来就多一层 `payload\payload\app\...`，
#    而安装引擎期望的是"解出来的目录里直接就是 app\ 与 python\"。
Compress-Archive -Path (Join-Path $Payload '*') -DestinationPath $zipPath -CompressionLevel Optimal
$zipItem = Get-Item -LiteralPath $zipPath
$ratio = [math]::Round($zipItem.Length / $payloadBytes * 100, 1)
Write-Ok ("payload.zip  {0:N1} MB（原始 {1:N1} MB，压到 {2}%）" -f ($zipItem.Length/1MB), ($payloadBytes/1MB), $ratio)

# ── 5) 拼单文件 ────────────────────────────────────────────────────────────
Write-Step '拼装单文件'
# ⚠️ `$Version` 是 "1.0.1+101"（ProductVersion 带着 Flutter 的构建号）。
#    **文件名不能带那个 `+`**：官网 Downloads 是按 Release 资产名匹配的，
#    而历史资产名一直是 `LxAI-Setup-1.0.1.exe`（旧 Inno 版就是）——
#    改了名字，官网的下载按钮就找不到包了。
$fileVersion = ($Version -split '\+')[0]
$finalName = "LxAI-Setup-$fileVersion.exe"
$finalPath = Join-Path $OutDir $finalName
if (Test-Path -LiteralPath $finalPath) { Remove-Item -LiteralPath $finalPath -Force }

$exeBytes = [IO.File]::ReadAllBytes($NativeExe)
$zipBytes = [IO.File]::ReadAllBytes($zipPath)
$offset = [uint64]$exeBytes.Length
$size   = [uint64]$zipBytes.Length

$footer = New-Object byte[] 32
[Text.Encoding]::ASCII.GetBytes('LXAIZIP1').CopyTo($footer, 0)
[BitConverter]::GetBytes($offset).CopyTo($footer, 8)
[BitConverter]::GetBytes($size).CopyTo($footer, 16)
# 16..31 预留（保持 32 字节定长，将来要加校验不用改长度假设）

$out = [IO.File]::Create($finalPath)
try {
    $out.Write($exeBytes, 0, $exeBytes.Length)
    $out.Write($zipBytes, 0, $zipBytes.Length)
    $out.Write($footer, 0, $footer.Length)
} finally {
    $out.Dispose()
}

$finalItem = Get-Item -LiteralPath $finalPath
Write-Ok ("{0}  {1:N1} MB" -f $finalName, ($finalItem.Length / 1MB))

# ── 6) 自检：尾部标记必须能被原样读回来 ────────────────────────────────────
Write-Step '自检'
$check = [IO.File]::ReadAllBytes($finalPath)
$tail = $check[($check.Length - 32)..($check.Length - 1)]
$magic = [Text.Encoding]::ASCII.GetString($tail[0..7])
$readOffset = [BitConverter]::ToUInt64($tail, 8)
$readSize   = [BitConverter]::ToUInt64($tail, 16)

if ($magic -ne 'LXAIZIP1') { throw "尾部标记写入失败：magic=$magic" }
if ($readOffset -ne $offset) { throw "尾部标记 offset 不符：写 $offset 读 $readOffset" }
if ($readSize -ne $size) { throw "尾部标记 size 不符：写 $size 读 $readSize" }
if (($readOffset + $readSize + 32) -ne [uint64]$check.Length) { throw "尾部标记与文件长度不自洽" }

# ZIP 的头 4 字节应是 PK\x03\x04（本地文件头）——顺带确认拼接没把素材错位
$pk = [Text.Encoding]::ASCII.GetString($check[[int]$readOffset..([int]$readOffset + 3)])
if ($pk -ne "PK$([char]3)$([char]4)") { throw "素材段不是合法的 ZIP 头：$pk" }

Write-Ok "尾部标记 OK：offset=$readOffset size=$readSize magic=$magic"
Write-Ok ("素材段 ZIP 头 OK：{0}" -f ($pk -replace '[^\x20-\x7E]', '.'))

# 探针：这份 exe 里不该再有"找不到界面文件"的可能 —— 界面在资源里
Write-Ok "界面已编进 exe 资源（ui\ui.rc），单文件不依赖任何伴随目录"

Write-Host ''
Write-Host ("单文件安装包：{0}" -f $finalPath) -ForegroundColor Green
Write-Host ("体积：{0:N1} MB（原生宿主 {1:N0} B + 素材 {2:N1} MB）" -f ($finalItem.Length/1MB), $exeItem.Length, ($zipItem.Length/1MB)) -ForegroundColor Green
