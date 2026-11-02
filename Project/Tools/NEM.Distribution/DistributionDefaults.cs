using System.Text.Json;

namespace NEMEngine.Distribution;

public static class DistributionDefaults {
    public static GitHubAppConfiguration GitHub => Read<GitHubAppConfiguration>("GitHubApp");
    public static GitToolsManifest GitTools => Read<GitToolsManifest>("GitTools");

    private static T Read<T>(string name) where T : class {
        using var stream = typeof(DistributionDefaults).Assembly.GetManifestResourceStream("NEMEngine." + name + ".json")!;
        return JsonSerializer.Deserialize<T>(stream, DistributionStorage.JsonOptions) ?? throw new InvalidDataException("配布設定を読み込めません");
    }
}
