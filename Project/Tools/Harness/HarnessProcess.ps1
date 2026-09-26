# 環境変数と終了コードを揃えて検証プロセスを起動する
function Invoke-HarnessProcess {
    param([string]$FilePath, [string[]]$Arguments)

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
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
        $process.WaitForExit()
        return [pscustomobject]@{
            exitCode = $process.ExitCode
            output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}
