using System.Net.Http.Json;

namespace NEMEngine.Distribution;

// 配信一覧を検証してから利用する
public static class CatalogService {
    public static async Task<ReleaseCatalog> FetchAsync(HttpClient client, string url, CancellationToken token) {
        if (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme != "https")
            throw new InvalidDataException("配布一覧のHTTPS URLを設定してください");
        using var request = new HttpRequestMessage(HttpMethod.Get, uri);
        request.Headers.CacheControl = new() { NoCache = true };
        using var response = await client.SendAsync(request, token);
        response.EnsureSuccessStatusCode();
        var catalog = await response.Content.ReadFromJsonAsync<ReleaseCatalog>(DistributionStorage.JsonOptions, token)
            ?? throw new InvalidDataException("配布一覧を読み込めません");
        if (catalog.SchemaVersion != 1) throw new InvalidDataException("対応する配布一覧ではありません");
        var versions = new HashSet<string>();
        foreach (var release in catalog.Engines) {
            DistributionStorage.RequireVersion(release.Version);
            if (!versions.Add(release.Version)) throw new InvalidDataException("配布一覧に同じ版が重複しています");
        }
        ContentValidation.Validate(catalog);
        return catalog;
    }
}
