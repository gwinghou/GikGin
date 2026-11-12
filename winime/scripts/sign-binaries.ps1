# 给发布文件添加 Windows Authenticode 数字签名。
#
# 需要先准备代码签名证书：
#   - 正式发布：向 CA 购买 OV/EV Code Signing 证书；
#   - 本机测试：可以创建自签名证书（见 README，其他机器默认不信任）。
#
# 用法（PFX 文件）：
#   sign-binaries.bat -PfxPath C:\certs\codesign.pfx -Password 你的密码
#
# 用法（证书已装在证书存储里，EV 硬件令牌通常用这种方式）：
#   sign-binaries.bat -Thumbprint 0123456789ABCDEF...
#
# 可选：
#   -MachineStore       从“本地计算机”证书存储查找（默认当前用户）
#   -TimestampUrl URL   时间戳服务器，默认 DigiCert
#   -SigntoolPath PATH  手动指定 signtool.exe

[CmdletBinding()]
param(
    [string]$PfxPath,
    [string]$Password,
    [string]$Thumbprint,
    [switch]$MachineStore,
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [string]$SigntoolPath
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$files = @(
    (Join-Path $root 'dist\GikGinIME.dll'),
    (Join-Path $root 'dist\GikGinIMESettings.exe')
)

function Find-SignTool {
    if ($SigntoolPath) {
        if (Test-Path $SigntoolPath) { return (Resolve-Path $SigntoolPath).Path }
        throw "找不到指定的 signtool：$SigntoolPath"
    }

    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kitsRoot) {
        $candidates = Get-ChildItem $kitsRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
            Where-Object { Test-Path $_ }
        if ($candidates.Count -gt 0) { return $candidates[0] }
    }

    throw '找不到 signtool.exe。请安装 Windows 10/11 SDK，或使用 -SigntoolPath 指定路径。'
}

if (-not $PfxPath -and -not $Thumbprint) {
    throw '请指定 -PfxPath 或 -Thumbprint。'
}

$signtool = Find-SignTool
Write-Host "signtool: $signtool"

$signArgs = @('sign', '/fd', 'SHA256', '/td', 'SHA256', '/tr', $TimestampUrl)
if ($PfxPath) {
    $resolvedPfx = (Resolve-Path $PfxPath).Path
    $signArgs += @('/f', $resolvedPfx)
    if ($Password) { $signArgs += @('/p', $Password) }
} else {
    $signArgs += @('/sha1', ($Thumbprint -replace '\s', ''))
    if ($MachineStore) { $signArgs += '/sm' }
}

foreach ($file in $files) {
    if (-not (Test-Path $file)) {
        throw "找不到待签名文件：$file"
    }

    Write-Host ""
    Write-Host "==> 签名 $file" -ForegroundColor Cyan
    & $signtool @signArgs $file
    if ($LASTEXITCODE -ne 0) {
        throw "签名失败：$file（退出码 $LASTEXITCODE）"
    }

    Write-Host "==> 验证 $file" -ForegroundColor Cyan
    & $signtool verify /pa /v $file
    if ($LASTEXITCODE -ne 0) {
        throw "签名验证失败：$file（退出码 $LASTEXITCODE）"
    }
}

Write-Host ""
Write-Host "全部文件签名并验证成功。" -ForegroundColor Green
Write-Host "注意：签名后不要再修改 DLL/EXE，否则签名会失效。"
