# ===========================================================================
#  build.ps1 —— 编译本实验的四个 C 程序（Windows / PowerShell）
#
#  用法：
#     powershell -ExecutionPolicy Bypass -File build.ps1
#     powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
#
#  生成的可执行文件位于 bin 目录：fcfs.exe、rr.exe、mlfq.exe、banker.exe
# ===========================================================================
param([switch]$Clean)

$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$srcDir  = Join-Path $root 'src'
$incDir  = Join-Path $root 'include'
$binDir  = Join-Path $root 'bin'
$common  = Join-Path $srcDir 'common.c'
$targets = @('fcfs', 'rr', 'mlfq', 'banker')

if ($Clean -and (Test-Path -LiteralPath $binDir)) {
    Get-ChildItem -LiteralPath $binDir -Filter '*.exe' | Remove-Item -Force
    Write-Host '已清理 bin 目录中的可执行文件。'
}
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    throw '找不到 gcc，请先安装 MinGW-w64 / MSYS2 并把它加入 PATH。'
}

$failed = 0
foreach ($t in $targets) {
    $out = Join-Path $binDir "$t.exe"
    Write-Host "编译 $t.c ..." -NoNewline
    & gcc -std=c11 -Wall -Wextra -O2 -I $incDir -o $out $common (Join-Path $srcDir "$t.c")
    if ($LASTEXITCODE -ne 0) {
        Write-Host ' 失败'
        $failed++
    } else {
        Write-Host ' 成功'
    }
}

if ($failed -gt 0) { throw "有 $failed 个程序编译失败。" }
Write-Host ''
Write-Host '编译完成，可执行文件在 bin 目录：'
Get-ChildItem -LiteralPath $binDir -Filter '*.exe' | ForEach-Object { Write-Host ('  ' + $_.FullName) }
