# 環境変数と終了コードを揃えて検証プロセスを起動する
function Invoke-HarnessProcess {
    param([string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory = '',
        [ValidateRange(0, 86400)][int]$TimeoutSeconds = 0)

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    if ($WorkingDirectory) { $info.WorkingDirectory = $WorkingDirectory }
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # Windowsでは環境変数名の大小文字を同一視する
    $info.Environment.Clear()
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $info.Environment[$entry.Key] = $entry.Value
    }
    # 診断言語を揃え、検証後のビルドプロセスを残さない
    $info.Environment['DOTNET_CLI_UI_LANGUAGE'] = 'en'
    $info.Environment['VSLANG'] = '1033'
    $info.Environment['MSBUILDDISABLENODEREUSE'] = '1'
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    try {
        # 両方のパイプを同時に読み、出力詰まりを防ぐ
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = $false
        if ($TimeoutSeconds -gt 0) {
            $timedOut = !$process.WaitForExit($TimeoutSeconds * 1000)
            if ($timedOut) {
                # この検証から起動したプロセスだけを終了する
                try { $process.Kill($true) }
                catch [InvalidOperationException] { if (!$process.HasExited) { throw } }
                $process.WaitForExit()
            }
        } else {
            $process.WaitForExit()
        }
        return [pscustomobject]@{
            exitCode = $(if ($timedOut) { -1 } else { $process.ExitCode })
            output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
            timedOut = $timedOut
        }
    } finally {
        $process.Dispose()
    }
}
