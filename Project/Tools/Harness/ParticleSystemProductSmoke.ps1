$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$productRoot = Join-Path $engineRoot 'Generated/PSP/ProductProbe'
$logRoot = Join-Path $engineRoot 'Generated/Refactoring/ParticleSystemRefactor/ProductValidation'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NEMParticleProductWindow {
    private delegate bool Callback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processID);
    [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    public static bool Close(uint processID) {
        bool found = false;
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processID) found |= PostMessageW(window, 0x10, IntPtr.Zero, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$previousPortable = $env:NEMENGINE_PORTABLE
$process = $null
try {
    $env:NEMENGINE_PORTABLE = '1'
    $process = Start-Process -FilePath (Join-Path $productRoot 'ProductProbe.exe') -WorkingDirectory $productRoot `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logRoot 'Runtime.log') `
        -RedirectStandardError (Join-Path $logRoot 'Runtime.Error.log')
    # 実際の製品で起動SceneとGPU更新を継続して動かす
    if ($process.WaitForExit(15000)) { throw "製品が確認前に終了しました: $($process.ExitCode)" }
    if (![NEMParticleProductWindow]::Close($process.Id)) { throw '確認用製品のWindowが見つかりません' }
    if (!$process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "製品の終了に失敗しました: $($process.ExitCode)" }
    $log = Get-Content -LiteralPath (Join-Path $logRoot 'Runtime.log') -Raw
    if ($log -match '\[error\]|\[critical\]|\[Assert|GPU粒子を実行できません|DEVICE_REMOVED') {
        throw '製品の実行ログにエラーがあります'
    }
    Write-Output 'ParticleSystem製品の起動・描画継続・通常終了を確認しました'
} finally {
    if ($process) {
        if (!$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    $env:NEMENGINE_PORTABLE = $previousPortable
}
