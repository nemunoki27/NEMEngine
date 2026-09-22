using System.Reflection;
using System.Runtime.CompilerServices;



using System.Text.Json;




namespace NEMEngine;

// 隔離Assemblyから構築用成果物を保存する
internal static unsafe class ScriptManifestWriter {

    private const int ManifestSchemaVersion = 1;

    private static readonly JsonSerializerOptions manifestJsonOptions = new() { WriteIndented = true };

    // manifest JSON schema（GameScripts.scriptmanifest.json）
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

    internal static ManagedStatus GenerateManifestIsolated(string dllPath, string outPath) {

        string assemblyName = Path.GetFileNameWithoutExtension(dllPath);

        // 一時 collectible ALC で対象 DLL を反射して manifest + schema を組む（現行 gameLoadContext には触れない）
        (ManifestRoot root, bool valid, string schemaJson) = LoadAndCollectManifest(dllPath, assemblyName);

        // 一時 ALC の DLL ロックを早期に解放する（直後の shadow copy のため）
        GC.Collect();
        GC.WaitForPendingFinalizers();

        if (!valid) {
            // GUID 不正 / 重複 → 検証失敗。呼び出し側は現行 DLL を維持する
            return ManagedStatus.SerializationError;
        }

        try {
            string json = JsonSerializer.Serialize(root, manifestJsonOptions);
            File.WriteAllText(outPath, json);
            NativeApplicationAPI.WriteLog(0, $"Generated script manifest: {outPath} (assembly={assemblyName} scripts={root.scripts.Count})");

            // companion: serialized field schema を manifest と同じディレクトリへ出力する（staging artifact）
            string schemaPath = Path.Combine(Path.GetDirectoryName(outPath) ?? string.Empty, "GameScripts.scriptschema.json");
            File.WriteAllText(schemaPath, string.IsNullOrEmpty(schemaJson) ? "{\"schemaVersion\":2,\"scripts\":[]}" : schemaJson);
            NativeApplicationAPI.WriteLog(0, $"Generated script schema: {schemaPath}");
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"Failed to write script manifest/schema: {outPath}\n{ex}");
            return ManagedStatus.SerializationError;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (ManifestRoot root, bool valid, string schemaJson) LoadAndCollectManifest(string dllPath, string assemblyName) {

        var loadContext = new GameScriptLoadContext(dllPath);
        string schemaJson = string.Empty;
        try {
            Assembly assembly = loadContext.LoadFromAssemblyPath(dllPath);
            var root = new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName };
            var seenGuids = new HashSet<string>(StringComparer.Ordinal);
            bool valid = true;

            // generator が埋め込んだ serialized field schema をそのまま artifact として取り出す
            try {
                Type? schemaType = assembly.GetType("NEMEngine.GeneratedScriptSchema", throwOnError: false);
                MethodInfo? schemaMethod = schemaType?.GetMethod("GetSchemaJson", BindingFlags.Public | BindingFlags.Static);
                schemaJson = schemaMethod?.Invoke(null, null) as string ?? string.Empty;
            }
            catch {
                schemaJson = string.Empty;
            }

            void AddScript(string rawGuid, string fullName, string displayName, string sourcePath) {

                string? normalized = ScriptGeneratedMetadata.NormalizeGuid(rawGuid);
                if (normalized == null) {
                    NativeApplicationAPI.WriteLog(2, $"manifest: invalid Script Type GUID for '{fullName}'.");
                    valid = false;
                    return;
                }
                if (!seenGuids.Add(normalized)) {
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
            return (root, valid, schemaJson);
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"manifest: failed to inspect assembly '{dllPath}'\n{ex}");
            return (new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName }, false, schemaJson);
        }
        finally {
            loadContext.Unload();
        }
    }
}
