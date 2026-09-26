using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// 入力の検証と診断を保持する
internal sealed class BindingInputReader {

    private const int SupportedSchemaVersion = 2;
    internal int errorCount;
    internal List<AbiLayoutModel> layouts = new();
    private static readonly string[] AllowedExposure = {
        "GeneratedBinding", "HandwrittenFacade", "RuntimeCommand", "RuntimeEvent", "InternalOnly",
    };
    internal (List<EnumModel>, List<ComponentModel>) LoadAndValidate(string path) {

        var enums = new List<EnumModel>();
        var components = new List<ComponentModel>();
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
        JsonElement root = doc.RootElement;

        int schema = root.TryGetProperty("schemaVersion", out JsonElement sv) ? sv.GetInt32() : 0;
        if (schema != SupportedSchemaVersion) {
            Error($"unsupported schemaVersion {schema} (expected {SupportedSchemaVersion}).");
        }

        var enumNames = new HashSet<string>(StringComparer.Ordinal);
        if (root.TryGetProperty("enums", out JsonElement enumsEl) && enumsEl.ValueKind == JsonValueKind.Array) {
            foreach (JsonElement e in enumsEl.EnumerateArray()) {
                var model = new EnumModel {
                    ManagedType = Str(e, "managedType"),
                    NativeType = Str(e, "nativeType"),
                };
                if (e.TryGetProperty("members", out JsonElement mem) && mem.ValueKind == JsonValueKind.Array) {
                    foreach (JsonElement m in mem.EnumerateArray()) {
                        model.Members.Add(new EnumMember { Name = Str(m, "name"), Value = m.GetProperty("value").GetInt64() });
                    }
                }
                if (!enumNames.Add(model.ManagedType)) Error($"duplicate enum managedType '{model.ManagedType}'.");
                enums.Add(model);
            }
        }

        var componentKeys = new HashSet<string>(StringComparer.Ordinal);
        var managedTypes = new HashSet<string>(StringComparer.Ordinal);
        var componentIDs = new HashSet<int>();
        if (root.TryGetProperty("components", out JsonElement compsEl) && compsEl.ValueKind == JsonValueKind.Array) {
            foreach (JsonElement c in compsEl.EnumerateArray()) {
                var model = new ComponentModel {
                    ID = c.TryGetProperty("id", out JsonElement id) ? id.GetInt32() : -1,
                    RegistryName = Str(c, "registryName"),
                    NativeType = Str(c, "nativeType"),
                    NativeHeader = Str(c, "nativeHeader"),
                    Exposure = Str(c, "exposure"),
                    ManagedType = Str(c, "managedType"),
                    ManagedFactory = c.TryGetProperty("managedFactory", out JsonElement mf) && mf.GetBoolean(),
                    AllowAdd = !c.TryGetProperty("allowAdd", out JsonElement aa) || aa.GetBoolean(),
                    AllowRemove = !c.TryGetProperty("allowRemove", out JsonElement ar) || ar.GetBoolean(),
                };
                if (model.ID < 0) Error($"{model.RegistryName}: component id must be non-negative.");
                if (!componentIDs.Add(model.ID)) Error($"duplicate component id '{model.ID}'.");
                if (string.IsNullOrEmpty(model.RegistryName)) Error("component missing registryName.");
                if (string.IsNullOrEmpty(model.NativeType)) Error($"{model.RegistryName}: nativeType is required.");
                if (string.IsNullOrEmpty(model.NativeHeader)) Error($"{model.RegistryName}: nativeHeader is required.");
                if (Array.IndexOf(AllowedExposure, model.Exposure) < 0)
                    Error($"{model.RegistryName}: invalid exposure '{model.Exposure}'.");
                if (!componentKeys.Add(model.RegistryName)) Error($"duplicate component registryName '{model.RegistryName}'.");
                if (!string.IsNullOrEmpty(model.ManagedType) && !managedTypes.Add(model.ManagedType))
                    Error($"duplicate managed wrapper type '{model.ManagedType}'.");
                if (model.Exposure == "GeneratedBinding" && string.IsNullOrEmpty(model.ManagedType))
                    Error($"{model.RegistryName}: GeneratedBinding requires managedType.");

                var propNames = new HashSet<string>(StringComparer.Ordinal);
                if (c.TryGetProperty("properties", out JsonElement props) && props.ValueKind == JsonValueKind.Array) {
                    foreach (JsonElement p in props.EnumerateArray()) {
                        var pm = new PropertyModel {
                            ManagedName = Str(p, "managedName"),
                            NativeMember = Str(p, "nativeMember"),
                            Kind = Str(p, "kind"),
                            AssetType = p.TryGetProperty("assetType", out JsonElement at) ? at.GetString() : null,
                            EnumType = p.TryGetProperty("enumType", out JsonElement et) ? et.GetString() : null,
                            Access = p.TryGetProperty("access", out JsonElement ac) ? (ac.GetString() ?? "ReadWrite") : "ReadWrite",
                            Visibility = p.TryGetProperty("visibility", out JsonElement vis) ? (vis.GetString() ?? "Public") : "Public",
                        };
                        if (string.IsNullOrWhiteSpace(pm.ManagedName) || string.IsNullOrWhiteSpace(pm.NativeMember))
                            Error($"{model.ManagedType}: property requires managedName and nativeMember.");
                        if (pm.Access != "ReadOnly" && pm.Access != "ReadWrite")
                            Error($"{model.ManagedType}.{pm.ManagedName}: invalid access '{pm.Access}'.");
                        if (!propNames.Add(pm.ManagedName)) Error($"{model.ManagedType}: duplicate property '{pm.ManagedName}'.");
                        if (!IsKnownKind(pm.Kind)) Error($"{model.ManagedType}.{pm.ManagedName}: unknown kind '{pm.Kind}'.");
                        if (pm.Visibility != "Public" && pm.Visibility != "Internal")
                            Error($"{model.ManagedType}.{pm.ManagedName}: invalid visibility '{pm.Visibility}'.");
                        if (pm.Kind == "Enum" && (pm.EnumType == null || !enumNames.Contains(pm.EnumType)))
                            Error($"{model.ManagedType}.{pm.ManagedName}: Enum requires a known enumType.");
                        if (pm.Kind == "AssetRef" && string.IsNullOrEmpty(pm.AssetType))
                            Error($"{model.ManagedType}.{pm.ManagedName}: AssetRef requires assetType.");
                        model.Properties.Add(pm);
                    }
                }
                components.Add(model);
            }
        }

        if (components.Count == 0) { Error("Component manifest has no components."); }
        components.Sort((a, b) => a.ID.CompareTo(b.ID));
        for (int i = 0; i < components.Count; ++i) {
            if (components[i].ID != i) {
                Error($"component ids must be contiguous from 0. expected={i} actual={components[i].ID}.");
            }
        }
        return (enums, components);
    }

    internal List<AbiFieldModel> LoadAndValidateAbi(string path) {

        var fields = new List<AbiFieldModel>();
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
        JsonElement root = doc.RootElement;
        int schema = root.TryGetProperty("schemaVersion", out JsonElement sv) ? sv.GetInt32() : 0;
        if (schema != 1) {
            Error($"unsupported ABI schemaVersion {schema} (expected 1).");
        }

        var names = new HashSet<string>(StringComparer.Ordinal);
        if (root.TryGetProperty("functions", out JsonElement entries) && entries.ValueKind == JsonValueKind.Array) {
            foreach (JsonElement entry in entries.EnumerateArray()) {
                var field = new AbiFieldModel {
                    Name = Str(entry, "name"),
                    NativeType = Str(entry, "nativeType"),
                    ManagedType = Str(entry, "managedType"),
                };
                if (string.IsNullOrEmpty(field.Name) ||
                    string.IsNullOrEmpty(field.NativeType) ||
                    string.IsNullOrEmpty(field.ManagedType)) {
                    Error("ABI function requires name, nativeType and managedType.");
                }
                if (!names.Add(field.Name)) {
                    Error($"duplicate ABI field '{field.Name}'.");
                }
                fields.Add(field);
            }
        }
        if (fields.Count == 0) {
            Error("ABI schema has no functions.");
        }
        layouts.Clear();
        if (root.TryGetProperty("layouts", out JsonElement layoutEntries)) {
            var layoutNames = new HashSet<string>(StringComparer.Ordinal);
            foreach (JsonElement entry in layoutEntries.EnumerateArray()) {
                var layout = new AbiLayoutModel {
                    NativeType = Str(entry, "nativeType"), ManagedType = Str(entry, "managedType"),
                    Size = entry.GetProperty("size").GetInt32(),
                };
                if (layout.NativeType.Length == 0 || layout.ManagedType.Length == 0 || layout.Size <= 0 ||
                    !layoutNames.Add(layout.ManagedType)) { Error("Invalid or duplicate ABI layout."); }
                foreach (JsonProperty member in entry.GetProperty("members").EnumerateObject()) {
                    int offset = member.Value.GetInt32();
                    if (offset < 0 || offset >= layout.Size || !layout.Members.TryAdd(member.Name, offset)) {
                        Error("Invalid or duplicate ABI member offset.");
                    }
                }
                layouts.Add(layout);
            }
        }
        return fields;
    }

    internal string Str(JsonElement e, string key) {
        return e.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? (v.GetString() ?? "") : "";
    }

    internal void Error(string message) {
        ++errorCount;
        Console.Error.WriteLine($"[ComponentBindingGen] error: {message}");
    }
}
