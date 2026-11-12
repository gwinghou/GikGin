# 替换 dist\GikGinIME.dll 的一键脚本。
#
# 背景：TSF 输入法 DLL 被加载后，Windows 不允许直接覆盖同名文件。
# 本脚本优先结束/重启 Explorer，然后尝试覆盖；如果仍被其他进程占用，
# 就把旧 DLL 重命名到 build\locked\，再复制新 DLL。
#
# 用法：
#   replace-ime-dll.bat
#   replace-ime-dll.bat -KillAll
#   replace-ime-dll.bat -NoExplorerRestart
#
# 参数：
#   -KillAll             结束所有占用该 DLL 的普通程序（默认不动浏览器/IDE/
#                        当前 Agent，避免丢失未保存内容）
#   -NoExplorerRestart   不自动重启资源管理器（调试脚本时用）

[CmdletBinding()]
param(
    [switch]$KillAll,
    [switch]$NoExplorerRestart
)

$ErrorActionPreference = 'Stop'

# winime 目录：scripts 的上一级
$root = Split-Path -Parent $PSScriptRoot
$sourceDll = Join-Path $root 'build\x64\Release\GikGinIME.dll'
$destDll = Join-Path $root 'dist\GikGinIME.dll'
$sourceSettings = Join-Path $root 'build\x64\Release\GikGinIMESettings.exe'
$destSettings = Join-Path $root 'dist\GikGinIMESettings.exe'
$lockedDir = Join-Path $root 'build\locked'

function Write-Step([string]$text) {
    Write-Host ""
    Write-Host "==> $text" -ForegroundColor Cyan
}

# 无论成功还是失败，都要把 Explorer 拉起来，避免脚本出错后任务栏消失。
function Restart-ExplorerIfNeeded {
    if ($script:explorerWasRunning -and -not $NoExplorerRestart) {
        Write-Step "重新启动资源管理器"
        Start-Process explorer.exe
        $script:explorerWasRunning = $false
    }
}

# 找出所有加载了指定文件的进程。
# 某些受保护/位数不同的进程可能读不到 Modules，忽略即可；
# 真正的占用会通过“复制失败 -> 重命名”兜底。
function Get-ProcessesUsingFile([string]$path) {
    $result = @()
    $full = [System.IO.Path]::GetFullPath($path)
    foreach ($process in Get-Process) {
        try {
            foreach ($module in $process.Modules) {
                if ([string]::Equals($module.FileName, $full,
                                      [System.StringComparison]::OrdinalIgnoreCase)) {
                    $result += [PSCustomObject]@{
                        Id = $process.Id
                        Name = $process.ProcessName
                    }
                    break
                }
            }
        } catch {
            # 无权访问该进程的模块列表，跳过
        }
    }
    return $result
}

if (-not (Test-Path $sourceDll)) {
    Write-Host "找不到新 DLL：$sourceDll" -ForegroundColor Red
    Write-Host "请先用 VS2019 生成 Release | x64，或修改脚本顶部的路径。"
    exit 1
}

Write-Step "目标：$destDll"
Write-Host "新 DLL：$sourceDll"

$holders = @(Get-ProcessesUsingFile $destDll)
if ($holders.Count -eq 0) {
    Write-Host "当前没有进程占用目标 DLL。"
} else {
    Write-Host "当前占用进程："
    $holders | ForEach-Object { Write-Host ("  {0} (PID {1})" -f $_.Name, $_.Id) }
}

# Explorer 是最常见的占用者；结束并重启它通常就能释放 DLL。
$explorerWasRunning = $false
if (-not $NoExplorerRestart) {
    $explorer = Get-Process explorer -ErrorAction SilentlyContinue
    if ($explorer) {
        $explorerWasRunning = $true
        Write-Step "重启资源管理器"
        try {
            $explorer | Stop-Process -Force -ErrorAction Stop
            Start-Sleep -Milliseconds 800
        } catch {
            Write-Host "结束 Explorer 失败（可能权限不足），继续尝试其他方式。"
        }
    }
}

# 可选：结束其他占用程序。默认只结束 Explorer，避免误关浏览器/编辑器。
if ($KillAll) {
    Write-Step "结束其他占用进程"
    $holders = @(Get-ProcessesUsingFile $destDll)
    foreach ($holder in $holders) {
        if ($holder.Id -eq $PID) { continue }
        if ($holder.Name -ieq 'explorer') { continue }
        if ($holder.Name -ieq 'DeepSeek Harness') { continue }
        if ($holder.Name -ieq 'powershell' -or $holder.Name -ieq 'pwsh' -or
            $holder.Name -ieq 'cmd') { continue }
        try {
            Write-Host ("  结束 {0} (PID {1})" -f $holder.Name, $holder.Id)
            Stop-Process -Id $holder.Id -Force -ErrorAction Stop
        } catch {
            Write-Host ("  无法结束 {0} (PID {1})" -f $holder.Name, $holder.Id)
        }
    }
    Start-Sleep -Milliseconds 500
}

# 先尝试直接覆盖。失败则重命名旧文件：
# Windows 允许对已加载的 DLL 改名，改名后原路径就可以写入新文件。
$copied = $false
try {
    Copy-Item $sourceDll $destDll -Force -ErrorAction Stop
    $copied = $true
    Write-Step "已直接覆盖 DLL"
} catch {
    Write-Host ""
    Write-Host "直接覆盖失败，改用重命名方式..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path $lockedDir | Out-Null

    if (Test-Path $destDll) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $oldDll = Join-Path $lockedDir ("GikGinIME.old-{0}.dll" -f $stamp)
        try {
            Move-Item $destDll $oldDll -Force -ErrorAction Stop
            Write-Host "旧 DLL 已移到：$oldDll"
        } catch {
            Write-Host "重命名旧 DLL 失败：$($_.Exception.Message)" -ForegroundColor Red
            Write-Host "请关闭占用程序后重试，或以管理员身份运行。"
            Restart-ExplorerIfNeeded
            exit 1
        }
    }

    try {
        Copy-Item $sourceDll $destDll -Force -ErrorAction Stop
        $copied = $true
        Write-Step "已通过重命名方式替换 DLL"
    } catch {
        Write-Host "复制新 DLL 失败：$($_.Exception.Message)" -ForegroundColor Red
        Restart-ExplorerIfNeeded
        exit 1
    }
}

# 顺手更新设置程序；它不是常驻 DLL，一般不会被占用。
if (Test-Path $sourceSettings) {
    try {
        Copy-Item $sourceSettings $destSettings -Force -ErrorAction Stop
        Write-Host "已更新 GikGinIMESettings.exe"
    } catch {
        Write-Host "设置程序复制失败（可能正在运行）：$($_.Exception.Message)" -ForegroundColor Yellow
    }
}

# 恢复 Explorer。
Restart-ExplorerIfNeeded

# 路径没变，一般不需要重新注册。这里只做提示。
Write-Step "完成"
Write-Host "新 DLL 已就位：$destDll"
Write-Host ""
Write-Host "注意：已经打开的程序仍然加载着旧 DLL，需要重启该程序"
Write-Host "（或重启系统）才会用上新版本。"
Write-Host ""
Write-Host "如果仍有程序占用，可以运行："
Write-Host "  replace-ime-dll.bat -KillAll"
Write-Host "（会结束占用该 DLL 的普通程序，请先保存工作。）"

exit 0
