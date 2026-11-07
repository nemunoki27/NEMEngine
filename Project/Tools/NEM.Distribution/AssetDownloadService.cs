namespace NEMEngine.Distribution;

// 検証済みのZIPだけを選択した保存先へ公開する
public sealed class AssetDownloadService(HttpClient client) {
    public async Task SaveAsync(AssetRelease asset, string cacheRoot, string destination, bool overwrite,
        IProgress<TransferProgress>? progress, CancellationToken token) {
        ContentValidation.Validate(asset);
        destination = Path.GetFullPath(destination);
        string parent = Path.GetDirectoryName(destination)!;
        if (!Directory.Exists(parent)) throw new DirectoryNotFoundException("保存先フォルダーが見つかりません");
        if (!overwrite && File.Exists(destination)) throw new IOException("同じ名前のファイルが既に存在します");
        string archive = await new PackageTransfer(client).DownloadAsync(asset.Package, cacheRoot, progress, token);
        if (destination.Equals(Path.GetFullPath(archive), StringComparison.OrdinalIgnoreCase)) return;
        string stage = DistributionStorage.Child(parent, ".asset-" + Guid.NewGuid().ToString("N") + ".partial");
        try {
            progress?.Report(new("ZIPを保存中"));
            await using (var input = new FileStream(archive, FileMode.Open, FileAccess.Read, FileShare.Read))
            await using (var output = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                await input.CopyToAsync(output, token);
            token.ThrowIfCancellationRequested();
            File.Move(stage, destination, overwrite);
        } finally { if (File.Exists(stage)) File.Delete(stage); }
    }
}
