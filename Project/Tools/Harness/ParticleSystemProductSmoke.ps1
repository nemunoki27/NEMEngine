$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$productRoot = Join-Path $engineRoot 'LocalWork/Tests/PSP/ProductProbe'
$logRoot = Join-Path $engineRoot 'LocalWork/Verification/Refactoring/ParticleSystemRefactor/ProductValidation'
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
$previousLocal = $env:LOCALAPPDATA
$process = $null
try {
    $env:LOCALAPPDATA = Join-Path $logRoot ('Run-' + [Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $env:LOCALAPPDATA
    $engineLog = Join-Path $env:LOCALAPPDATA 'NEMEngine/Games/4e454d41535345547061727470726f64/Saved/Logs/engine.log'
    $shaderRoot = Join-Path $engineRoot 'Project/Engine/Assets/Rendering/Builtin/Particle/GPU'
    $simulate = (Get-Content -Raw (Join-Path $shaderRoot 'particleGPUSimulate.shader.json.meta') | ConvertFrom-Json).guid
    $draw = (Get-Content -Raw (Join-Path $shaderRoot 'particleGPUDraw.shader.json.meta') | ConvertFrom-Json).guid
    $process = Start-Process -FilePath (Join-Path $productRoot 'ProductProbe.exe') -WorkingDirectory $productRoot `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logRoot 'Runtime.log') `
        -RedirectStandardError (Join-Path $logRoot 'Runtime.Error.log')
    # 実際の製品で起動SceneとGPU更新を継続して動かす
    $deadline = [DateTime]::UtcNow.AddMinutes(2)
    do {
        if ($process.WaitForExit(500)) { throw "製品が確認前に終了しました: $($process.ExitCode)" }
        $runtime = if (Test-Path -LiteralPath $engineLog) { Get-Content -LiteralPath $engineLog -Encoding utf8 -Raw } else { '' }
        $started = $runtime -match '起動シーンを決定しました GUID=4e454d41535345547061727473636e01' -and
            $runtime -match "stage=CS shader=$simulate" -and $runtime -match "stage=PS shader=$draw"
    } while (!$started -and [DateTime]::UtcNow -lt $deadline)
    if (!$started) { throw "GPU粒子のシミュレーションと描画の開始を確認できません: $engineLog" }
    if ($process.WaitForExit(5000)) { throw "製品が描画確認中に終了しました: $($process.ExitCode)" }
    if (![NEMParticleProductWindow]::Close($process.Id)) { throw '確認用製品のWindowが見つかりません' }
    if (!$process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "製品の終了に失敗しました: $($process.ExitCode)" }
    $log = Get-Content -LiteralPath $engineLog -Encoding utf8 -Raw
    if ($log -match '\[error\]|\[critical\]|\[Assert|GPU粒子を実行できません|DEVICE_REMOVED') {
        throw '製品の実行ログにエラーがあります'
    }
    Write-Output 'ParticleSystem製品の起動・描画継続・通常終了を確認しました'
} finally {
    if ($process) {
        if (!$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    $env:LOCALAPPDATA = $previousLocal
}
