using System.Text;
using System.Text.Json;

namespace NEM.ComponentBindingGen;

// ManagedComponentBindings.json から C++ dispatch と C# wrapper を決定的に生成する。
// 同じ入力からは byte 単位で安定した出力（sorted・LF・UTF-8 no BOM）。
internal static class Program {

    private const string GeneratorVersion = "1";
    private const int SupportedSchemaVersion = 1;

    private sealed class EnumMember { public string Name = ""; public long Value; }
    private sealed class EnumModel {
        public string ManagedType = "";
        public string NativeType = "";
        public List<EnumMember> Members = new();
    }
    private sealed class PropertyModel {
        public string ManagedName = "";
        public string NativeMember = "";
        public string Kind = "";
        public string? AssetType;
        public string? EnumType;
        public string Access = "ReadWrite";
        public bool ReadOnly => string.Equals(Access, "ReadOnly", StringComparison.Ordinal);
    }
    private sealed class ComponentModel {
        public string RegistryName = "";
        public string NativeType = "";
        public string NativeHeader = "";
        public string ManagedType = "";
        public bool AllowAdd = true;
        public bool AllowRemove = true;
        public List<PropertyModel> Properties = new();
    }

    private static int errorCount;

    private static int Main(string[] args) {

        string? metadataPath = null;
        string? outNativeDir = null;
        string? outCsDir = null;
        for (int i = 0; i < args.Length; ++i) {
            switch (args[i]) {
                case "--metadata": if (i + 1 < args.Length) metadataPath = args[++i]; break;
                case "--out-native-dir": if (i + 1 < args.Length) outNativeDir = args[++i]; break;
                case "--out-cs-dir": if (i + 1 < args.Length) outCsDir = args[++i]; break;
            }
        }
        if (metadataPath == null || outNativeDir == null || outCsDir == null) {
            Console.Error.WriteLine("[ComponentBindingGen] usage: --metadata <json> --out-native-dir <dir> --out-cs-dir <dir>");
            return 1;
        }
        if (!File.Exists(metadataPath)) {
            Console.Error.WriteLine($"[ComponentBindingGen] metadata not found: {metadataPath}");
            return 1;
        }

        (List<EnumModel> enums, List<ComponentModel> components) = LoadAndValidate(metadataPath);
        if (errorCount > 0) {
            Console.Error.WriteLine($"[ComponentBindingGen] failed with {errorCount} validation error(s).");
            return 1;
        }

        // 決定的にするため安定ソート
        enums.Sort((a, b) => string.CompareOrdinal(a.ManagedType, b.ManagedType));
        components.Sort((a, b) => string.CompareOrdinal(a.RegistryName, b.RegistryName));
        foreach (ComponentModel c in components) {
            c.Properties.Sort((a, b) => string.CompareOrdinal(a.ManagedName, b.ManagedName));
        }
        var enumByName = new Dictionary<string, EnumModel>(StringComparer.Ordinal);
        foreach (EnumModel e in enums) enumByName[e.ManagedType] = e;

        Directory.CreateDirectory(outNativeDir);
        Directory.CreateDirectory(outCsDir);

        WriteIfChanged(Path.Combine(outNativeDir, "ManagedComponentBindings.generated.h"), EmitNativeHeader());
        WriteIfChanged(Path.Combine(outNativeDir, "ManagedComponentBindings.generated.cpp"), EmitNativeCpp(components, enumByName));
        WriteIfChanged(Path.Combine(outCsDir, "ComponentBindings.generated.cs"), EmitCSharp(components, enums));

        Console.WriteLine($"[ComponentBindingGen] done. enums={enums.Count} components={components.Count}");
        return 0;
    }

    //========================================================================
    //	load + validate
    //========================================================================
    private static (List<EnumModel>, List<ComponentModel>) LoadAndValidate(string path) {

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
        if (root.TryGetProperty("components", out JsonElement compsEl) && compsEl.ValueKind == JsonValueKind.Array) {
            foreach (JsonElement c in compsEl.EnumerateArray()) {
                var model = new ComponentModel {
                    RegistryName = Str(c, "registryName"),
                    NativeType = Str(c, "nativeType"),
                    NativeHeader = Str(c, "nativeHeader"),
                    ManagedType = Str(c, "managedType"),
                    AllowAdd = !c.TryGetProperty("allowAdd", out JsonElement aa) || aa.GetBoolean(),
                    AllowRemove = !c.TryGetProperty("allowRemove", out JsonElement ar) || ar.GetBoolean(),
                };
                if (string.IsNullOrEmpty(model.RegistryName)) Error("component missing registryName.");
                if (!componentKeys.Add(model.RegistryName)) Error($"duplicate component registryName '{model.RegistryName}'.");
                if (!managedTypes.Add(model.ManagedType)) Error($"duplicate managed wrapper type '{model.ManagedType}'.");

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
                        };
                        if (!propNames.Add(pm.ManagedName)) Error($"{model.ManagedType}: duplicate property '{pm.ManagedName}'.");
                        if (!IsKnownKind(pm.Kind)) Error($"{model.ManagedType}.{pm.ManagedName}: unknown kind '{pm.Kind}'.");
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
        return (enums, components);
    }

    private static bool IsKnownKind(string kind) {
        switch (kind) {
            case "Bool": case "Byte": case "SByte": case "Short": case "UShort":
            case "Int": case "UInt": case "Long": case "ULong": case "Float": case "Double":
            case "String": case "Enum":
            case "Vector2": case "Vector3": case "Vector4": case "Quaternion":
            case "Color3": case "Color4": case "AssetRef": case "EntityRef":
                return true;
            default: return false;
        }
    }

    // POD kind の C# 型・marshal バッファ byte 数（String/AssetRef/Enum/Bool は個別扱い）
    private static (string csType, int size) PodInfo(string kind) {
        switch (kind) {
            case "Byte": return ("byte", 1);
            case "SByte": return ("sbyte", 1);
            case "Short": return ("short", 2);
            case "UShort": return ("ushort", 2);
            case "Int": return ("int", 4);
            case "UInt": return ("uint", 4);
            case "Long": return ("long", 8);
            case "ULong": return ("ulong", 8);
            case "Float": return ("float", 4);
            case "Double": return ("double", 8);
            case "Vector2": return ("Vector2", 8);
            case "Vector3": return ("Vector3", 12);
            case "Vector4": return ("Vector4", 16);
            case "Quaternion": return ("Quaternion", 16);
            case "Color3": return ("Color3", 12);
            case "Color4": return ("Color4", 16);
            default: return ("", 0);
        }
    }

    //========================================================================
    //	emit C++ header
    //========================================================================
    private static string EmitNativeHeader() {
        var sb = new StringBuilder();
        sb.Append(NativeBanner());
        sb.Append("#pragma once\n\n");
        sb.Append("#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>\n\n");
        sb.Append("namespace Engine::GeneratedComponentBindings {\n\n");
        sb.Append("\t// 自動生成 component wrapper の typed property dispatch。ManagedScriptRuntime が table へ結線する。\n");
        sb.Append("\tManagedStatus GetComponentProperty(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, void* outValue, int32_t valueSize);\n");
        sb.Append("\tManagedStatus SetComponentProperty(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, const void* value, int32_t valueSize);\n");
        sb.Append("\tManagedStatus GetComponentStringProperty(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, char* buffer, int32_t capacity, int32_t* written);\n");
        sb.Append("\tManagedStatus SetComponentStringProperty(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, const char* utf8, int32_t length);\n\n");
        sb.Append("} // namespace Engine::GeneratedComponentBindings\n");
        return sb.ToString();
    }

    //========================================================================
    //	emit C++ cpp
    //========================================================================
    private static string EmitNativeCpp(List<ComponentModel> components, Dictionary<string, EnumModel> enumByName) {
        var sb = new StringBuilder();
        sb.Append(NativeBanner());
        sb.Append("#include \"ManagedComponentBindings.generated.h\"\n\n");
        sb.Append("#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>\n");
        sb.Append("#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>\n");
        sb.Append("#include <Engine/Core/Foundation/Identity/UUID.h>\n");
        foreach (string header in components.Select(c => c.NativeHeader).Where(h => !string.IsNullOrEmpty(h)).Distinct().OrderBy(h => h, StringComparer.Ordinal)) {
            sb.Append($"#include <{header}>\n");
        }
        sb.Append("\n#include <cstring>\n#include <vector>\n\n");
        sb.Append("namespace Engine::GeneratedComponentBindings {\n\n");

        // typeId -> binding index の遅延マップ（ECS の compact id は engine セッション固定）
        sb.Append("\tnamespace {\n\n");
        sb.Append("\t\tstd::vector<int32_t> g_bindingByTypeId;\n");
        sb.Append("\t\tbool g_idMapBuilt = false;\n\n");
        sb.Append("\t\tvoid EnsureIdMap() {\n");
        sb.Append("\t\t\tif (g_idMapBuilt) { return; }\n");
        sb.Append("\t\t\tauto& registry = ComponentTypeRegistry::GetInstance();\n");
        sb.Append("\t\t\tg_bindingByTypeId.assign(registry.GetComponentTypeCount(), -1);\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\t\tif (const ComponentTypeInfo* info = registry.FindByName(\"{components[i].RegistryName}\")) {{ if (info->id < g_bindingByTypeId.size()) g_bindingByTypeId[info->id] = {i}; }}\n");
        }
        sb.Append("\t\t\tg_idMapBuilt = true;\n");
        sb.Append("\t\t}\n\n");
        sb.Append("\t\tint32_t BindingIndexOf(int32_t typeId) {\n");
        sb.Append("\t\t\tEnsureIdMap();\n");
        sb.Append("\t\t\treturn (typeId >= 0 && static_cast<size_t>(typeId) < g_bindingByTypeId.size()) ? g_bindingByTypeId[typeId] : -1;\n");
        sb.Append("\t\t}\n\n");

        // 各 component の get/set/str ハンドラ
        for (int i = 0; i < components.Count; ++i) {
            EmitCppComponentHandlers(sb, components[i], enumByName);
        }
        sb.Append("\t} // anonymous namespace\n\n");

        // dispatch entry points
        EmitCppDispatch(sb, components, "GetComponentProperty", "GetProp", "void* outValue, int32_t valueSize", "outValue, valueSize", checkOut: true);
        EmitCppDispatch(sb, components, "SetComponentProperty", "SetProp", "const void* value, int32_t valueSize", "value, valueSize", checkOut: false, valueArg: "value");
        EmitCppStringDispatch(sb, components);

        sb.Append("} // namespace Engine::GeneratedComponentBindings\n");
        return sb.ToString();
    }

    private static void EmitCppComponentHandlers(StringBuilder sb, ComponentModel comp, Dictionary<string, EnumModel> enumByName) {

        string nt = comp.NativeType;

        // 適用対象 property を先に振り分ける。対象ゼロの handler は
        // 空 switch(C4065) や未使用パラメータ(C4100) を出さず、void cast で閉じる。
        var podGet = new List<int>();
        var podSet = new List<int>();
        var strGet = new List<int>();
        var strSet = new List<int>();
        for (int p = 0; p < comp.Properties.Count; ++p) {
            PropertyModel prop = comp.Properties[p];
            bool isString = prop.Kind == "String";
            if (isString) {
                strGet.Add(p);
                if (!prop.ReadOnly) strSet.Add(p);
            } else {
                podGet.Add(p);
                if (!prop.ReadOnly) podSet.Add(p);
            }
        }

        // --- POD get ---
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_GetProp(ECSWorld& world, const Entity& entity, int32_t propertyId, void* out, int32_t size) {{\n");
        if (podGet.Count == 0) {
            sb.Append("\t\t\t(void)world; (void)entity; (void)propertyId; (void)out; (void)size;\n");
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponent<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyId) {\n");
            foreach (int p in podGet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                EmitCppGet(sb, comp.Properties[p], enumByName);
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }

        // --- POD set ---
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_SetProp(ECSWorld& world, const Entity& entity, int32_t propertyId, const void* value, int32_t size) {{\n");
        if (podSet.Count == 0) {
            sb.Append("\t\t\t(void)world; (void)entity; (void)propertyId; (void)value; (void)size;\n");
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponent<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyId) {\n");
            foreach (int p in podSet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                EmitCppSet(sb, comp.Properties[p], enumByName);
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }

        // --- string get ---
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_GetStr(ECSWorld& world, const Entity& entity, int32_t propertyId, char* buffer, int32_t capacity, int32_t* written) {{\n");
        if (strGet.Count == 0) {
            sb.Append("\t\t\t(void)world; (void)entity; (void)propertyId; (void)buffer; (void)capacity; (void)written;\n");
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponent<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyId) {\n");
            foreach (int p in strGet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                sb.Append($"\t\t\t\tconst std::string& s = c->{comp.Properties[p].NativeMember};\n");
                sb.Append("\t\t\t\tconst int32_t needed = static_cast<int32_t>(s.size());\n");
                sb.Append("\t\t\t\tif (written) { *written = needed; }\n");
                sb.Append("\t\t\t\tif (!buffer || capacity < needed) { return ManagedStatus::BufferTooSmall; }\n");
                sb.Append("\t\t\t\tif (needed > 0) { std::memcpy(buffer, s.data(), static_cast<size_t>(needed)); }\n");
                sb.Append("\t\t\t\treturn ManagedStatus::Ok;\n");
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }

        // --- string set ---
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_SetStr(ECSWorld& world, const Entity& entity, int32_t propertyId, const char* utf8, int32_t length) {{\n");
        if (strSet.Count == 0) {
            sb.Append("\t\t\t(void)world; (void)entity; (void)propertyId; (void)utf8; (void)length;\n");
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponent<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyId) {\n");
            foreach (int p in strSet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                sb.Append($"\t\t\t\tc->{comp.Properties[p].NativeMember} = (utf8 && length > 0) ? std::string(utf8, static_cast<size_t>(length)) : std::string();\n");
                sb.Append("\t\t\t\treturn ManagedStatus::Ok;\n");
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }
    }

    private static void EmitCppGet(StringBuilder sb, PropertyModel prop, Dictionary<string, EnumModel> enumByName) {
        string m = prop.NativeMember;
        switch (prop.Kind) {
            case "Bool":
                sb.Append("\t\t\t\tif (size < 4) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\t*reinterpret_cast<int32_t*>(out) = c->{m} ? 1 : 0;\n");
                break;
            case "Enum": {
                EnumModel e = enumByName[prop.EnumType!];
                sb.Append("\t\t\t\tif (size < 4) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\t*reinterpret_cast<int32_t*>(out) = static_cast<int32_t>(c->{m});\n");
                _ = e;
                break;
            }
            case "AssetRef":
                sb.Append("\t\t\t\tif (size < 8) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\t*reinterpret_cast<uint64_t*>(out) = c->{m}.value;\n");
                break;
            default: {
                (_, int podSize) = PodInfo(prop.Kind);
                sb.Append($"\t\t\t\tif (size < {podSize}) {{ return ManagedStatus::InvalidArgument; }}\n");
                sb.Append($"\t\t\t\tstd::memcpy(out, &(c->{m}), {podSize});\n");
                break;
            }
        }
        sb.Append("\t\t\t\treturn ManagedStatus::Ok;\n");
    }

    private static void EmitCppSet(StringBuilder sb, PropertyModel prop, Dictionary<string, EnumModel> enumByName) {
        string m = prop.NativeMember;
        switch (prop.Kind) {
            case "Bool":
                sb.Append("\t\t\t\tif (size < 4) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\tc->{m} = (*reinterpret_cast<const int32_t*>(value)) != 0;\n");
                break;
            case "Enum": {
                EnumModel e = enumByName[prop.EnumType!];
                sb.Append("\t\t\t\tif (size < 4) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\tc->{m} = static_cast<{e.NativeType}>(*reinterpret_cast<const int32_t*>(value));\n");
                break;
            }
            case "AssetRef":
                sb.Append("\t\t\t\tif (size < 8) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\tc->{m}.value = *reinterpret_cast<const uint64_t*>(value);\n");
                break;
            default: {
                (_, int podSize) = PodInfo(prop.Kind);
                sb.Append($"\t\t\t\tif (size < {podSize}) {{ return ManagedStatus::InvalidArgument; }}\n");
                sb.Append($"\t\t\t\tstd::memcpy(&(c->{m}), value, {podSize});\n");
                break;
            }
        }
        sb.Append("\t\t\t\treturn ManagedStatus::Ok;\n");
    }

    private static void EmitCppDispatch(StringBuilder sb, List<ComponentModel> components, string fnName, string handler,
        string extraParams, string extraArgs, bool checkOut, string? valueArg = null) {

        sb.Append($"\tManagedStatus {fnName}(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, {extraParams}) {{\n");
        sb.Append("\t\tECSWorld* world = ResolveWorld(entity);\n");
        sb.Append("\t\tconst Entity resolved = ResolveEntity(entity);\n");
        sb.Append("\t\tif (!world || !world->IsAlive(resolved)) { return ManagedStatus::InvalidEntityHandle; }\n");
        if (checkOut) {
            sb.Append("\t\tif (!outValue) { return ManagedStatus::InvalidArgument; }\n");
        } else if (valueArg != null) {
            sb.Append($"\t\tif (!{valueArg}) {{ return ManagedStatus::InvalidArgument; }}\n");
        }
        sb.Append("\t\tswitch (BindingIndexOf(typeId)) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\tcase {i}: return {components[i].ManagedType}_{handler}(*world, resolved, propertyId, {extraArgs});\n");
        }
        sb.Append("\t\tdefault: return ManagedStatus::InvalidArgument;\n");
        sb.Append("\t\t}\n\t}\n\n");
    }

    private static void EmitCppStringDispatch(StringBuilder sb, List<ComponentModel> components) {
        sb.Append("\tManagedStatus GetComponentStringProperty(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, char* buffer, int32_t capacity, int32_t* written) {\n");
        sb.Append("\t\tECSWorld* world = ResolveWorld(entity);\n");
        sb.Append("\t\tconst Entity resolved = ResolveEntity(entity);\n");
        sb.Append("\t\tif (!world || !world->IsAlive(resolved)) { return ManagedStatus::InvalidEntityHandle; }\n");
        sb.Append("\t\tswitch (BindingIndexOf(typeId)) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\tcase {i}: return {components[i].ManagedType}_GetStr(*world, resolved, propertyId, buffer, capacity, written);\n");
        }
        sb.Append("\t\tdefault: return ManagedStatus::InvalidArgument;\n");
        sb.Append("\t\t}\n\t}\n\n");

        sb.Append("\tManagedStatus SetComponentStringProperty(ManagedNativeEntity entity, int32_t typeId, int32_t propertyId, const char* utf8, int32_t length) {\n");
        sb.Append("\t\tECSWorld* world = ResolveWorld(entity);\n");
        sb.Append("\t\tconst Entity resolved = ResolveEntity(entity);\n");
        sb.Append("\t\tif (!world || !world->IsAlive(resolved)) { return ManagedStatus::InvalidEntityHandle; }\n");
        sb.Append("\t\tswitch (BindingIndexOf(typeId)) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\tcase {i}: return {components[i].ManagedType}_SetStr(*world, resolved, propertyId, utf8, length);\n");
        }
        sb.Append("\t\tdefault: return ManagedStatus::InvalidArgument;\n");
        sb.Append("\t\t}\n\t}\n\n");
    }

    //========================================================================
    //	emit C#
    //========================================================================
    private static string EmitCSharp(List<ComponentModel> components, List<EnumModel> enums) {
        var sb = new StringBuilder();
        sb.Append(CsBanner());
        sb.Append("namespace NEMEngine;\n\n");

        // enums
        foreach (EnumModel e in enums) {
            sb.Append($"public enum {e.ManagedType} {{\n");
            foreach (EnumMember m in e.Members) {
                sb.Append($"    {m.Name} = {m.Value},\n");
            }
            sb.Append("}\n\n");
        }

        // wrappers
        foreach (ComponentModel comp in components) {
            sb.Append($"// {comp.NativeType} の調整可能 property を公開する wrapper（owner Entity の opaque handle のみ保持）\n");
            sb.Append($"public readonly unsafe partial struct {comp.ManagedType} : IComponentRef<{comp.ManagedType}> {{\n\n");
            sb.Append("    public Entity entity { get; }\n\n");
            sb.Append($"    internal {comp.ManagedType}(Entity entity) {{ this.entity = entity; }}\n\n");
            sb.Append($"    public static string componentTypeName => \"{comp.RegistryName}\";\n");
            sb.Append($"    public static {comp.ManagedType} FromEntity(Entity entity) => new(entity);\n\n");
            sb.Append("    private int TypeId => ComponentType<" + comp.ManagedType + ">.Id;\n\n");
            for (int p = 0; p < comp.Properties.Count; ++p) {
                EmitCsProperty(sb, comp.Properties[p], p);
            }
            sb.Append("}\n\n");
        }
        return sb.ToString();
    }

    private static void EmitCsProperty(StringBuilder sb, PropertyModel prop, int propId) {
        switch (prop.Kind) {
            case "Bool": {
                sb.Append($"    public bool {prop.ManagedName} {{\n");
                sb.Append($"        get {{ int v = 0; NativeApi.ComponentGet(entity.native, TypeId, {propId}, &v, 4); return v != 0; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ int v = value ? 1 : 0; NativeApi.ComponentSet(entity.native, TypeId, {propId}, &v, 4); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "Enum": {
                string t = prop.EnumType!;
                sb.Append($"    public {t} {prop.ManagedName} {{\n");
                sb.Append($"        get {{ int v = 0; NativeApi.ComponentGet(entity.native, TypeId, {propId}, &v, 4); return ({t})v; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ int v = (int)value; NativeApi.ComponentSet(entity.native, TypeId, {propId}, &v, 4); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "AssetRef": {
                string t = prop.AssetType!;
                sb.Append($"    public AssetRef<{t}> {prop.ManagedName} {{\n");
                sb.Append($"        get {{ ulong v = 0; NativeApi.ComponentGet(entity.native, TypeId, {propId}, &v, 8); return new AssetRef<{t}>(new Uuid(v)); }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ ulong v = value.id.value; NativeApi.ComponentSet(entity.native, TypeId, {propId}, &v, 8); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "String": {
                sb.Append($"    public string {prop.ManagedName} {{\n");
                sb.Append($"        get => NativeApi.ComponentGetString(entity.native, TypeId, {propId});\n");
                if (!prop.ReadOnly) sb.Append($"        set => NativeApi.ComponentSetString(entity.native, TypeId, {propId}, value);\n");
                sb.Append("    }\n\n");
                break;
            }
            default: {
                (string csType, int size) = PodInfo(prop.Kind);
                sb.Append($"    public {csType} {prop.ManagedName} {{\n");
                sb.Append($"        get {{ {csType} v = default; NativeApi.ComponentGet(entity.native, TypeId, {propId}, &v, {size}); return v; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ NativeApi.ComponentSet(entity.native, TypeId, {propId}, &value, {size}); }}\n");
                sb.Append("    }\n\n");
                break;
            }
        }
    }

    //========================================================================
    //	helpers
    //========================================================================
    private static string NativeBanner() {
        return "//============================================================================\n" +
               "//\tAUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n" +
               $"//\tgenerator: NEM.ComponentBindingGen v{GeneratorVersion}\n" +
               $"//\tmetadata schemaVersion: {SupportedSchemaVersion}\n" +
               "//\t編集する場合は ManagedComponentBindings.json を更新して generate_vs2026.bat を再実行する\n" +
               "//============================================================================\n";
    }
    private static string CsBanner() {
        return "//============================================================================\n" +
               "//\tAUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n" +
               $"//\tgenerator: NEM.ComponentBindingGen v{GeneratorVersion}\n" +
               $"//\tmetadata schemaVersion: {SupportedSchemaVersion}\n" +
               "//============================================================================\n";
    }

    private static void WriteIfChanged(string path, string content) {
        string? current = File.Exists(path) ? File.ReadAllText(path) : null;
        if (current != null && current.Replace("\r\n", "\n") == content) {
            Console.WriteLine($"[ComponentBindingGen] up-to-date {Path.GetFileName(path)}");
            return;
        }
        File.WriteAllText(path, content, new UTF8Encoding(false));
        Console.WriteLine($"[ComponentBindingGen] wrote {Path.GetFileName(path)}");
    }

    private static string Str(JsonElement e, string key) {
        return e.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? (v.GetString() ?? "") : "";
    }

    private static void Error(string message) {
        ++errorCount;
        Console.Error.WriteLine($"[ComponentBindingGen] error: {message}");
    }
}
