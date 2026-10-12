# ===========================================================================
#  run_tests.ps1 —— 运行全部测试用例，并把结果保存到 docs\output\
#
#  用法：
#     powershell -ExecutionPolicy Bypass -File run_tests.ps1
#     powershell -ExecutionPolicy Bypass -File run_tests.ps1 -NoBuild
# ===========================================================================
param([switch]$NoBuild)

$ErrorActionPreference = 'Stop'

$root   = $PSScriptRoot
$binDir = Join-Path $root 'bin'
$outDir = Join-Path $root 'docs\output'

# 让子进程的 UTF-8 输出能被 PowerShell 正确解读，并原样写入文件
$prevOut = [Console]::OutputEncoding
try { [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false) } catch { }

if (-not $NoBuild) { & (Join-Path $root 'build.ps1') }

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$cases = @(
    @{ name = 'fcfs_1';   exe = 'fcfs';   args = @('-f', 'test\fcfs_1.txt') },
    @{ name = 'fcfs_2';   exe = 'fcfs';   args = @('-f', 'test\fcfs_2.txt') },
    @{ name = 'rr_1';     exe = 'rr';     args = @('-f', 'test\rr_1.txt', '-t', '1') },
    @{ name = 'rr_2';     exe = 'rr';     args = @('-f', 'test\rr_2.txt', '-t', '2') },
    @{ name = 'mlfq_1';   exe = 'mlfq';   args = @('-f', 'test\mlfq_1.txt') },
    @{ name = 'mlfq_2';   exe = 'mlfq';   args = @('-f', 'test\mlfq_2.txt') },
    @{ name = 'mlfq_3';   exe = 'mlfq';   args = @('-f', 'test\mlfq_3.txt', '-a', '5') },
    @{ name = 'banker_1'; exe = 'banker'; args = @('-f', 'test\banker_1.txt', '-b') },
    @{ name = 'banker_2'; exe = 'banker'; args = @('-f', 'test\banker_2.txt', '-b') },
    @{ name = 'banker_3'; exe = 'banker'; args = @('-f', 'test\banker_3.txt', '-b') },
    @{ name = 'banker_4'; exe = 'banker'; args = @('-f', 'test\banker_4.txt', '-b') },
    @{ name = 'cmp_fcfs'; exe = 'fcfs';   args = @('-f', 'test\compare.txt') },
    @{ name = 'cmp_rr';   exe = 'rr';     args = @('-f', 'test\compare.txt', '-t', '1') },
    @{ name = 'cmp_mlfq'; exe = 'mlfq';   args = @('-f', 'test\compare.txt') }
)

foreach ($c in $cases) {
    $exe = Join-Path $binDir ($c.exe + '.exe')
    if (-not (Test-Path -LiteralPath $exe)) { throw "找不到可执行文件 $exe，请先编译。" }

    $outFile = Join-Path $outDir ($c.name + '.txt')
    $cmdLine = ($c.args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    Write-Host ("运行 {0}.exe {1} -> docs\output\{2}.txt" -f $c.exe, $cmdLine, $c.name)

    $text = & $exe @($c.args) 2>&1 | Out-String
    $header = "【由 run_tests.ps1 自动生成】命令行：bin\$($c.exe).exe $cmdLine`r`n"
    Set-Content -LiteralPath $outFile -Value ($header + $text) -Encoding utf8
}

[Console]::OutputEncoding = $prevOut
Write-Host ''
Write-Host "全部测试运行完毕，结果已保存到 $outDir"
