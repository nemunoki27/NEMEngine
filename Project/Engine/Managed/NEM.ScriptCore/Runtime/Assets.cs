namespace NEMEngine;

// AssetRef の runtime resolve。UUID 主体で、native resource / GPU pointer / filesystem path は公開しない。
// 解決は既存 Asset system へ接続する。Missing Asset でも UUID は保持される（AssetRef 側が破壊しない）。
// async load は native 側に基盤が無いため、成功したふりをする API は追加していない。
public static class Assets {

    // asset が現在の AssetDatabase に存在するか。hot path で繰り返す場合は結果を呼び出し側で cache する想定。
    public static bool Exists<TAsset>(AssetRef<TAsset> asset) where TAsset : class, IAssetType {
        return NativeApi.ReadAssetExists(asset.id.value);
    }

    // 表示名（拡張子なしファイル名）。Missing/未設定は null。path 文字列そのものは返さない。
    public static string? GetDisplayName<TAsset>(AssetRef<TAsset> asset) where TAsset : class, IAssetType {
        if (asset.id.value == 0) {
            return null;
        }
        string name = NativeApi.ReadAssetDisplayName(asset.id.value);
        return string.IsNullOrEmpty(name) ? null : name;
    }
}
