namespace NEMEngine.Distribution;

// 別プロセスの認証更新や同じプロジェクトの操作を直列化する
internal static class OperationFileLock {
    public static async Task<FileStream> AcquireAsync(string root, string key, CancellationToken token) {
        string hash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(key)));
        string directory = Path.Combine(root, "Locks");
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, hash + ".lock");
        var started = System.Diagnostics.Stopwatch.StartNew();
        while (true) {
            token.ThrowIfCancellationRequested();
            try { return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose); }
            catch (IOException) when (started.Elapsed < TimeSpan.FromMinutes(2)) { await Task.Delay(100, token); }
        }
    }
}
