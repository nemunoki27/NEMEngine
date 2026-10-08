using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
namespace NEMEngine;

// 隔離Assemblyから構築用成果物を保存する
internal static unsafe class ScriptManifestWriter {

    // 実行中のAssemblyに触れず構築用成果物を保存する
    internal static ManagedStatus GenerateManifestIsolated(string dllPath, string outPath) {

        string assemblyName = Path.GetFileNameWithoutExtension(dllPath);

        // 隔離したAssemblyからManifestとschemaを取得する
        (ManifestRoot root, bool valid, string schemaJSON) = LoadAndCollectManifest(dllPath, assemblyName);

        // 成果物の配置前に一時Assemblyの参照を回収する
        GC.Collect();
        GC.WaitForPendingFinalizers();

        if (!valid) {
            // 型IDの検証失敗を呼出元へ返す
            return ManagedStatus.SerializationError;
        }

        try {
            string json = JsonSerializer.Serialize(root, manifestJSONOptions);
            File.WriteAllText(outPath, json);
            NativeApplicationAPI.WriteLog(0, $"Generated script manifest: {outPath} (assembly={assemblyName} scripts={root.scripts.Count})");

            // 保存schemaをManifestと同じ場所へ配置する
            string schemaPath = Path.Combine(Path.GetDirectoryName(outPath) ?? string.Empty, "GameScripts.scriptschema.json");
            File.WriteAllText(schemaPath, string.IsNullOrEmpty(schemaJSON) ? "{\"schemaVersion\":2,\"scripts\":[]}" : schemaJSON);
            NativeApplicationAPI.WriteLog(0, $"Generated script schema: {schemaPath}");
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"Failed to write script manifest/schema: {outPath}\n{ex}");
            return ManagedStatus.SerializationError;
        }
    }

    // Manifestの保存形式
    private sealed class ManifestRoot {
        public int schemaVersion { get; set; }
        public string assemblyName { get; set; } = string.Empty;
        public List<ManifestScript> scripts { get; set; } = new();
    }
    private sealed class ManifestScript {
        public string scriptTypeID { get; set; } = string.Empty;
        public string fullTypeName { get; set; } = string.Empty;
        public string displayName { get; set; } = string.Empty;
        public string sourcePath { get; set; } = string.Empty;
        public string sourceAssetID { get; set; } = string.Empty;
    }

    private const int ManifestSchemaVersion = 1;

    private static readonly JsonSerializerOptions manifestJSONOptions = new() { WriteIndented = true };

    // ALCの強参照を残さず型一覧とschemaを取得する
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (ManifestRoot root, bool valid, string schemaJSON) LoadAndCollectManifest(string dllPath, string assemblyName) {

        var loadContext = new GameScriptLoadContext(dllPath);
        string schemaJSON = string.Empty;
        try {
            Assembly assembly = loadContext.LoadFromAssemblyPath(dllPath);
            var root = new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName };
            var seenGUIDs = new HashSet<string>(StringComparer.Ordinal);
            bool valid = true;

            // 生成器の保存schemaをそのまま取得する
            try {
                Type? schemaType = assembly.GetType("NEMEngine.GeneratedScriptSchema", throwOnError: false);
                MethodInfo? schemaMethod = schemaType?.GetMethod("GetSchemaJson", BindingFlags.Public | BindingFlags.Static);
                schemaJSON = schemaMethod?.Invoke(null, null) as string ?? string.Empty;
            }
            catch {
                schemaJSON = string.Empty;
            }

            // 正規化した型IDの重複を拒否する
            void AddScript(string rawGUID, string fullName, string displayName, string sourcePath) {

                string? normalized = ScriptGeneratedMetadata.NormalizeGUID(rawGUID);
                if (normalized == null) {
                    NativeApplicationAPI.WriteLog(2, $"manifest: invalid Script Type GUID for '{fullName}'.");
                    valid = false;
                    return;
                }
                if (!seenGUIDs.Add(normalized)) {
                    NativeApplicationAPI.WriteLog(2, $"manifest: duplicate Script Type GUID '{normalized}' ('{fullName}').");
                    valid = false;
                    return;
                }
                root.scripts.Add(new ManifestScript {
                    scriptTypeID = normalized,
                    fullTypeName = fullName,
                    displayName = string.IsNullOrEmpty(displayName) ? fullName : displayName,
                    sourcePath = sourcePath ?? string.Empty,
                    sourceAssetID = string.Empty,
                });
            }

            ScriptTypeDescriptor[]? generated = ScriptGeneratedMetadata.TryReadGeneratedManifest(assembly);
            if (generated != null) {
                foreach (ScriptTypeDescriptor descriptor in generated) {
                    AddScript(descriptor.ScriptTypeID, descriptor.FullTypeName, descriptor.DisplayName, descriptor.SourcePath);
                }
            } else {
                NativeApplicationAPI.WriteLog(2,
                    $"manifest: GeneratedScriptManifest was not found in '{dllPath}'");
                valid = false;
            }

            root.scripts.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));
            return (root, valid, schemaJSON);
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"manifest: failed to inspect assembly '{dllPath}'\n{ex}");
            return (new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName }, false, schemaJSON);
        }
        finally {
            loadContext.Unload();
        }
    }
}
