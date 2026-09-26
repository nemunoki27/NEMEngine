using System.Reflection;





using System.Text.Json.Nodes;



namespace NEMEngine;

// 生成済みの型情報を読み取る
internal static unsafe class ScriptGeneratedMetadata {

    // C#表記の入れ子型名も実行型へ解決する
    internal static Type? ResolveType(Assembly assembly, string name) {
        Type? type = assembly.GetType(name, throwOnError: false);
        for (int separator = name.LastIndexOf('.'); type == null && separator >= 0; separator = name.LastIndexOf('.')) {
            name = name[..separator] + "+" + name[(separator + 1)..];
            type = assembly.GetType(name, throwOnError: false);
        }
        return type;
    }

    internal static JsonObject? TryReadGeneratedSchema(Assembly assembly) {

        Type? schemaType = assembly.GetType("NEMEngine.GeneratedScriptSchema", throwOnError: false);
        MethodInfo? method = schemaType?.GetMethod("GetSchemaJson", BindingFlags.Public | BindingFlags.Static);
        if (method == null) {
            return null;
        }
        try {
            string? json = method.Invoke(null, null) as string;
            if (string.IsNullOrEmpty(json)) {
                return null;
            }
            JsonNode? root = JsonNode.Parse(json!);
            var byType = new JsonObject();
            if (root?["scripts"] is not JsonArray scripts) {
                throw new InvalidOperationException("Generated schema has no scripts array.");
            }
            foreach (JsonNode? scriptNode in scripts) {
                if (scriptNode is not JsonObject scriptObj ||
                    scriptObj["scriptTypeId"]?.GetValue<string>() is not string rawID ||
                    NormalizeGuid(rawID) is not string id || byType.ContainsKey(id)) {
                    throw new InvalidOperationException("Generated schema contains an invalid or duplicate script ID.");
                }
                byType.Add(id, scriptObj.DeepClone());
            }
            return byType;
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"Failed to read GeneratedScriptSchema\n{ex}");
            return null;
        }
    }

    internal static ScriptTypeDescriptor[]? TryReadGeneratedManifest(Assembly assembly) {

        Type? generatedType = assembly.GetType("NEMEngine.GeneratedScriptManifest", throwOnError: false);
        MethodInfo? method = generatedType?.GetMethod("GetDescriptors", BindingFlags.Public | BindingFlags.Static);
        if (method == null) {
            return null;
        }
        return method.Invoke(null, null) as ScriptTypeDescriptor[];
    }

    internal static string? NormalizeGuid(string? raw) {

        if (string.IsNullOrWhiteSpace(raw)) {
            return null;
        }
        return Guid.TryParse(raw, out Guid guid) ? guid.ToString("D") : null;
    }

    internal static FieldInfo? ResolveFieldInfo(Type rootType, string declaringTypeName, string fieldName) {

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? t = rootType; t != null && t != typeof(object); t = t.BaseType) {
            FieldInfo? f = t.GetField(fieldName, flags);
            if (f != null) {
                string owner = f.DeclaringType?.FullName ?? string.Empty;
                string definition = t.IsGenericType ? t.GetGenericTypeDefinition().FullName ?? string.Empty : owner;
                if (string.IsNullOrEmpty(declaringTypeName) || owner == declaringTypeName || owner.Replace('+', '.') == declaringTypeName ||
                    definition == declaringTypeName) {
                    return f;
                }
            }
        }
        return null;
    }
}
