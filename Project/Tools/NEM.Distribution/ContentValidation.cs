namespace NEMEngine.Distribution;

// 配布一覧と作成前の内容を同じ条件で確認する
public static class ContentValidation {
    public static void Validate(PackageInfo package) {
        ArgumentNullException.ThrowIfNull(package);
        DistributionStorage.RequireID(package.ID);
        if (package.Bytes <= 0 || package.UnpackedBytes <= 0 || package.Sha256.Length != 64 ||
            !package.Sha256.All(Uri.IsHexDigit)) throw new InvalidDataException("配布物の検証情報が不正です");
        if (!Uri.TryCreate(package.Url, UriKind.Absolute, out Uri? uri) || uri.Scheme != "https")
            throw new InvalidDataException("配布URLはHTTPSで指定してください");
    }

    public static void Validate(SampleRelease sample) {
        DistributionStorage.RequireID(sample.ID);
        DistributionStorage.RequireVersion(sample.Version);
        DistributionStorage.RequireVersion(sample.EngineVersion);
        if (string.IsNullOrWhiteSpace(sample.Name)) throw new InvalidDataException("サンプル名がありません");
        Validate(sample.Package);
    }

    public static void Validate(AssetRelease asset) {
        DistributionStorage.RequireID(asset.ID);
        DistributionStorage.RequireVersion(asset.Version);
        if (asset.RecommendedEngineVersion.Length != 0) DistributionStorage.RequireVersion(asset.RecommendedEngineVersion);
        if (string.IsNullOrWhiteSpace(asset.Name)) throw new InvalidDataException("アセット名がありません");
        if (asset.FileName.Length == 0 || Path.GetFileName(asset.FileName) != asset.FileName ||
            !asset.FileName.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) ||
            asset.FileName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
            throw new InvalidDataException("アセットのZIPファイル名が不正です");
        DistributionStorage.Child(Path.GetTempPath(), asset.FileName);
        Validate(asset.Package);
    }

    public static void Validate(ReleaseCatalog catalog) {
        var samples = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var sample in catalog.Samples) {
            Validate(sample);
            if (!samples.Add(sample.ID + "/" + sample.Version + "/" + sample.EngineVersion))
                throw new InvalidDataException("配布一覧に同じサンプルの版が重複しています");
        }
        var assets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var asset in catalog.Assets) {
            Validate(asset);
            if (!assets.Add(asset.ID + "/" + asset.Version))
                throw new InvalidDataException("配布一覧に同じアセットの版が重複しています");
        }
    }
}
