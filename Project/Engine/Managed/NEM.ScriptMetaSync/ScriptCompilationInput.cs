using System.Diagnostics;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace NEM.ScriptMetaSync;

// ゲームProjectの条件と参照を使ってmeta解析用の入力を作る
internal static class ScriptCompilationInput {

    internal static CSharpCompilation Read(string project, string configuration) {
        var start = new ProcessStartInfo("dotnet") {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetDirectoryName(Path.GetFullPath(project))!,
        };
        string[] arguments = {
            "msbuild", Path.GetFullPath(project), "-nologo", "-verbosity:quiet",
            "-target:ResolveReferences,GenerateGlobalUsings", "-property:BuildProjectReferences=false",
            "-property:Configuration=" + configuration, "-getItem:Compile,ReferencePath",
            "-getProperty:DefineConstants,LangVersion,AllowUnsafeBlocks,Nullable",
        };
        foreach (string argument in arguments) { start.ArgumentList.Add(argument); }
        using Process process = Process.Start(start) ?? throw new InvalidOperationException("Cannot start MSBuild.");
        Task<string> output = process.StandardOutput.ReadToEndAsync();
        Task<string> errors = process.StandardError.ReadToEndAsync();
        process.WaitForExit();
        string text = output.GetAwaiter().GetResult();
        string diagnostics = errors.GetAwaiter().GetResult();
        if (process.ExitCode != 0) { throw new InvalidOperationException("Cannot resolve script compilation inputs.\n" + text + diagnostics); }
        using JsonDocument document = JsonDocument.Parse(text);
        return Read(document.RootElement);
    }

    // MSBuildから得たCompile集合をそのまま解析する
    internal static CSharpCompilation Read(JsonElement input) {
        JsonElement properties = input.GetProperty("Properties");
        string language = properties.GetProperty("LangVersion").GetString() ?? "default";
        if (!LanguageVersionFacts.TryParse(language, out LanguageVersion version)) {
            throw new InvalidOperationException("Unsupported script language version: " + language);
        }
        string[] symbols = (properties.GetProperty("DefineConstants").GetString() ?? "")
            .Split(new[] { ';', ',' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var parse = new CSharpParseOptions(version, preprocessorSymbols: symbols);
        JsonElement items = input.GetProperty("Items");
        var trees = items.GetProperty("Compile").EnumerateArray()
            .Select(item => item.GetProperty("FullPath").GetString()!).Distinct(StringComparer.OrdinalIgnoreCase)
            .Select(path => CSharpSyntaxTree.ParseText(File.ReadAllText(path), parse, path: path));
        var references = items.GetProperty("ReferencePath").EnumerateArray().Select(item => {
            string path = item.GetProperty("FullPath").GetString()!;
            string aliases = item.TryGetProperty("Aliases", out JsonElement value) ? value.GetString() ?? "" : "";
            MetadataReferenceProperties settings = MetadataReferenceProperties.Assembly;
            if (aliases.Length != 0) { settings = settings.WithAliases(aliases.Split(',')); }
            return MetadataReference.CreateFromFile(path, settings);
        });
        bool unsafeCode = string.Equals(properties.GetProperty("AllowUnsafeBlocks").GetString(), "true", StringComparison.OrdinalIgnoreCase);
        NullableContextOptions nullable = properties.GetProperty("Nullable").GetString() switch {
            "enable" => NullableContextOptions.Enable,
            "warnings" => NullableContextOptions.Warnings,
            "annotations" => NullableContextOptions.Annotations,
            _ => NullableContextOptions.Disable,
        };
        return CSharpCompilation.Create("ScriptMetadataInput", trees, references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: unsafeCode, nullableContextOptions: nullable));
    }
}
