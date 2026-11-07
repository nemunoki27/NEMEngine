using System.Xml.Linq;
using NEMEngine.Distribution;

// 共通雛形だけで中間出力の切替後も生成コードを除外できるか確認する
internal static class TemplateScriptBuildChecks {
    public static async Task RunAsync(string repository, string root, Action<bool, string> check) {
        string game = Path.Combine(root, "TemplateScriptBuild");
        DistributionStorage.CopyTree(Path.Combine(repository, "Templates/ManagedProject/Common"), game);
        string project = Path.Combine(game, "Scripts/GameScripts.csproj");
        var document = XDocument.Load(project);
        document.Descendants("Import").Where(item => ((string?)item.Attribute("Project")) is "../Library/EngineBinding.props" or
            "$(NEMEngineRoot)/Managed/Build/NEM.GameScripts.targets").Remove();
        document.Descendants("Reference").Where(item => (string?)item.Attribute("Include") == "NEM.ScriptCore").Remove();
        document.Descendants("Analyzer").Remove();
        document.Save(project);
        DistributionStorage.WriteText(Path.Combine(game, "GameAssets/TemplateBuildCheck.cs"), "public sealed class TemplateBuildCheck { public float Value { get; set; } }\n");
        string reports = Path.Combine(repository, "Generated/ContentDistributionValidation/ScriptBuild");
        Directory.CreateDirectory(reports);
        string dotnet = Environment.GetEnvironmentVariable("DOTNET_HOST_PATH") ?? "dotnet";
        var environment = new Dictionary<string, string> { ["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1", ["DOTNET_NOLOGO"] = "1" };
        foreach (string configuration in new[] { "Debug", "Develop" }) {
            await DistributionIntegration.RunAsync(dotnet, ["build", project, "-c", configuration, "-v:minimal", "-m:1", "-nodeReuse:false"],
                game, environment, Path.Combine(reports, configuration + ".log"));
            check(File.Exists(Path.Combine(game, "Managed", configuration, "GameScripts.dll")), "通常ビルドのDLLがありません");
        }
        for (int cycle = 1; cycle <= 2; ++cycle) {
            string staging = Path.Combine(game, "Managed/Staging/Develop", cycle.ToString());
            await DistributionIntegration.RunAsync(dotnet, ["build", project, "-c", "Develop", "--no-dependencies", "--no-incremental",
                "-p:NEMScriptIntermediateOutput=" + Path.Combine(staging, "obj").Replace('\\', '/') + "/",
                "-p:NEMScriptStagingOutput=" + staging, "-p:NEMSkipMetadataSync=true", "-v:minimal", "-m:1", "-nodeReuse:false"],
                game, environment, Path.Combine(reports, "Staging" + cycle + ".log"));
            check(File.Exists(Path.Combine(staging, "GameScripts.dll")) && File.Exists(Path.Combine(staging, "GameScripts.pdb")),
                "通常ビルド後のStagingビルドでDLLとPDBを生成できません");
        }
    }
}
