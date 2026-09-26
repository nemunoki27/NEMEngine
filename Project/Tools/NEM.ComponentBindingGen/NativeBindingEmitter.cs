using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class NativeBindingEmitter {

    internal static string EmitNativeHeader() {
        var sb = new StringBuilder();
        sb.Append(NativeBanner());
        sb.Append("#pragma once\n\n");
        sb.Append("#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>\n\n");
        sb.Append("namespace Engine::GeneratedComponentBindings {\n\n");
        sb.Append("\t// 自動生成 component wrapper の typed property dispatch。ManagedScriptRuntime が table へ結線する。\n");
        sb.Append("\tManagedStatus GetComponentProperty(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, void* outValue, int32_t valueSize);\n");
        sb.Append("\tManagedStatus SetComponentProperty(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, const void* value, int32_t valueSize);\n");
        sb.Append("\tManagedStatus GetComponentStringProperty(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, char* buffer, int32_t capacity, int32_t* written);\n");
        sb.Append("\tManagedStatus SetComponentStringProperty(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, const char* utf8, int32_t length);\n\n");
        sb.Append("} // namespace Engine::GeneratedComponentBindings\n");
        return sb.ToString();
    }

    internal static string EmitNativeCpp(List<ComponentModel> components, Dictionary<string, EnumModel> enumByName) {
        var sb = new StringBuilder();
        sb.Append(NativeBanner());
        sb.Append("#include \"ManagedComponentBindings.generated.h\"\n\n");
        sb.Append("#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>\n");
        sb.Append("#include <Engine/Core/Foundation/Identity/UUID.h>\n");
        if (components.SelectMany(c => c.Properties).Any(p => p.Kind == "EntityRef")) {
            sb.Append("#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>\n");
            sb.Append("#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>\n");
        }
        foreach (string header in components.Select(c => c.NativeHeader).Where(h => !string.IsNullOrEmpty(h)).Distinct().OrderBy(h => h, StringComparer.Ordinal)) {
            sb.Append($"#include <{header}>\n");
        }
        sb.Append("\n#include <cstring>\n\n");
        sb.Append("namespace Engine::GeneratedComponentBindings {\n\n");

        sb.Append("\tnamespace {\n\n");
        sb.Append("\t\tint32_t BindingIndexOf(int32_t typeID) {\n");
        sb.Append("\t\t\tswitch (typeID) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\t\tcase {components[i].ID}: return {i};\n");
        }
        sb.Append("\t\t\tdefault: return -1;\n");
        sb.Append("\t\t\t}\n");
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

    internal static void EmitCppComponentHandlers(StringBuilder sb, ComponentModel comp, Dictionary<string, EnumModel> enumByName) {

        string nt = comp.NativeType;

        // 対象のない処理は未使用引数を明示して不正引数を返す
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
        string unused = podGet.Count == 0 ? "[[maybe_unused]] " : string.Empty;
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_GetProp({unused}ECSWorld& world, {unused}const Entity& entity, " +
            $"{unused}int32_t propertyID, {unused}void* out, {unused}int32_t size) {{\n");
        if (podGet.Count == 0) {
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponentForBinding<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyID) {\n");
            foreach (int p in podGet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                EmitCppGet(sb, comp.Properties[p], enumByName);
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }

        // --- POD set ---
        unused = podSet.Count == 0 ? "[[maybe_unused]] " : string.Empty;
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_SetProp({unused}ECSWorld& world, {unused}const Entity& entity, " +
            $"{unused}int32_t propertyID, {unused}const void* value, {unused}int32_t size) {{\n");
        if (podSet.Count == 0) {
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponentForBinding<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyID) {\n");
            foreach (int p in podSet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                EmitCppSet(sb, comp.Properties[p], enumByName, nt);
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }

        // --- string get ---
        unused = strGet.Count == 0 ? "[[maybe_unused]] " : string.Empty;
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_GetStr({unused}ECSWorld& world, {unused}const Entity& entity, " +
            $"{unused}int32_t propertyID, {unused}char* buffer, {unused}int32_t capacity, {unused}int32_t* written) {{\n");
        if (strGet.Count == 0) {
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponentForBinding<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyID) {\n");
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
        unused = strSet.Count == 0 ? "[[maybe_unused]] " : string.Empty;
        sb.Append($"\t\tManagedStatus {comp.ManagedType}_SetStr({unused}ECSWorld& world, {unused}const Entity& entity, " +
            $"{unused}int32_t propertyID, {unused}const char* utf8, {unused}int32_t length) {{\n");
        if (strSet.Count == 0) {
            sb.Append("\t\t\treturn ManagedStatus::InvalidArgument;\n\t\t}\n\n");
        } else {
            sb.Append($"\t\t\t{nt}* c = world.TryGetComponentForBinding<{nt}>(entity);\n");
            sb.Append("\t\t\tif (!c) { return ManagedStatus::InvalidArgument; }\n");
            sb.Append("\t\t\tswitch (propertyID) {\n");
            foreach (int p in strSet) {
                sb.Append($"\t\t\tcase {p}: {{\n");
                sb.Append($"\t\t\t\tc->{comp.Properties[p].NativeMember} = (utf8 && length > 0) ? std::string(utf8, static_cast<size_t>(length)) : std::string();\n");
                sb.Append($"\t\t\t\tworld.MarkComponentModified<{nt}>(entity);\n");
                sb.Append("\t\t\t\treturn ManagedStatus::Ok;\n");
                sb.Append("\t\t\t}\n");
            }
            sb.Append("\t\t\tdefault: return ManagedStatus::InvalidArgument;\n");
            sb.Append("\t\t\t}\n\t\t}\n\n");
        }
    }

    internal static void EmitCppGet(StringBuilder sb, PropertyModel prop, Dictionary<string, EnumModel> enumByName) {
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
                sb.Append("\t\t\t\tif (size < static_cast<int32_t>(sizeof(ManagedAssetGUID))) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\t*reinterpret_cast<ManagedAssetGUID*>(out) = ToManagedAssetGUID(c->{m});\n");
                break;
            case "EntityRef":
                sb.Append("\t\t\t\tif (size < static_cast<int32_t>(sizeof(ManagedNativeEntity))) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\tconst Entity target = SceneObjectUtility::FindByLocalFileID(world, c->{m});\n");
                sb.Append("\t\t\t\t*reinterpret_cast<ManagedNativeEntity*>(out) = world.IsAlive(target) ? MakeNativeEntity(world, target) : MakeNullNativeEntity();\n");
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

    internal static void EmitCppSet(StringBuilder sb, PropertyModel prop,
        Dictionary<string, EnumModel> enumByName, string nativeType) {
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
                sb.Append("\t\t\t\tif (size < static_cast<int32_t>(sizeof(ManagedAssetGUID))) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append($"\t\t\t\tc->{m} = ToAssetID(*reinterpret_cast<const ManagedAssetGUID*>(value));\n");
                break;
            case "EntityRef":
                sb.Append("\t\t\t\tif (size < static_cast<int32_t>(sizeof(ManagedNativeEntity))) { return ManagedStatus::InvalidArgument; }\n");
                sb.Append("\t\t\t\tconst ManagedNativeEntity targetNative = *reinterpret_cast<const ManagedNativeEntity*>(value);\n");
                sb.Append("\t\t\t\tconst Entity target = ResolveEntity(targetNative);\n");
                sb.Append("\t\t\t\tconst SceneObjectComponent* sceneObject = ResolveWorld(targetNative) == &world && world.IsAlive(target) ? world.TryGetComponent<SceneObjectComponent>(target) : nullptr;\n");
                sb.Append($"\t\t\t\tc->{m} = sceneObject ? sceneObject->localFileID : UUID{{}};\n");
                break;
            default: {
                (_, int podSize) = PodInfo(prop.Kind);
                sb.Append($"\t\t\t\tif (size < {podSize}) {{ return ManagedStatus::InvalidArgument; }}\n");
                sb.Append($"\t\t\t\tstd::memcpy(&(c->{m}), value, {podSize});\n");
                break;
            }
        }
        sb.Append($"\t\t\t\tworld.MarkComponentModified<{nativeType}>(entity);\n");
        sb.Append("\t\t\t\treturn ManagedStatus::Ok;\n");
    }

    internal static void EmitCppDispatch(StringBuilder sb, List<ComponentModel> components, string fnName, string handler,
        string extraParams, string extraArgs, bool checkOut, string? valueArg = null) {

        sb.Append($"\tManagedStatus {fnName}(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, {extraParams}) {{\n");
        sb.Append("\t\tECSWorld* world = ResolveWorld(entity);\n");
        sb.Append("\t\tconst Entity resolved = ResolveEntity(entity);\n");
        sb.Append("\t\tif (!world || !world->IsAlive(resolved)) { return ManagedStatus::InvalidEntityHandle; }\n");
        if (checkOut) {
            sb.Append("\t\tif (!outValue) { return ManagedStatus::InvalidArgument; }\n");
        } else if (valueArg != null) {
            sb.Append($"\t\tif (!{valueArg}) {{ return ManagedStatus::InvalidArgument; }}\n");
        }
        sb.Append("\t\tswitch (BindingIndexOf(typeID)) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\tcase {i}: return {components[i].ManagedType}_{handler}(*world, resolved, propertyID, {extraArgs});\n");
        }
        sb.Append("\t\tdefault: return ManagedStatus::InvalidArgument;\n");
        sb.Append("\t\t}\n\t}\n\n");
    }

    internal static void EmitCppStringDispatch(StringBuilder sb, List<ComponentModel> components) {
        sb.Append("\tManagedStatus GetComponentStringProperty(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, char* buffer, int32_t capacity, int32_t* written) {\n");
        sb.Append("\t\tECSWorld* world = ResolveWorld(entity);\n");
        sb.Append("\t\tconst Entity resolved = ResolveEntity(entity);\n");
        sb.Append("\t\tif (!world || !world->IsAlive(resolved)) { return ManagedStatus::InvalidEntityHandle; }\n");
        sb.Append("\t\tswitch (BindingIndexOf(typeID)) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\tcase {i}: return {components[i].ManagedType}_GetStr(*world, resolved, propertyID, buffer, capacity, written);\n");
        }
        sb.Append("\t\tdefault: return ManagedStatus::InvalidArgument;\n");
        sb.Append("\t\t}\n\t}\n\n");

        sb.Append("\tManagedStatus SetComponentStringProperty(ManagedNativeEntity entity, int32_t typeID, int32_t propertyID, const char* utf8, int32_t length) {\n");
        sb.Append("\t\tECSWorld* world = ResolveWorld(entity);\n");
        sb.Append("\t\tconst Entity resolved = ResolveEntity(entity);\n");
        sb.Append("\t\tif (!world || !world->IsAlive(resolved)) { return ManagedStatus::InvalidEntityHandle; }\n");
        sb.Append("\t\tswitch (BindingIndexOf(typeID)) {\n");
        for (int i = 0; i < components.Count; ++i) {
            sb.Append($"\t\tcase {i}: return {components[i].ManagedType}_SetStr(*world, resolved, propertyID, utf8, length);\n");
        }
        sb.Append("\t\tdefault: return ManagedStatus::InvalidArgument;\n");
        sb.Append("\t\t}\n\t}\n\n");
    }

    internal static string EmitComponentRegistryHeader() {
        var sb = new StringBuilder();
        sb.Append(NativeBanner());
        sb.Append("#pragma once\n\n");
        sb.Append("namespace Engine {\n\n");
        sb.Append("\tclass ComponentTypeRegistry;\n\n");
        sb.Append("\t// ComponentManifestの固定ID順で組込みComponentを明示登録する\n");
        sb.Append("\tvoid RegisterBuiltinComponents(ComponentTypeRegistry& registry);\n\n");
        sb.Append("} // Engine\n");
        return sb.ToString();
    }

    internal static string EmitComponentRegistryCpp(List<ComponentModel> components) {
        var sb = new StringBuilder();
        sb.Append(NativeBanner());
        sb.Append("#include \"BuiltinComponentRegistry.generated.h\"\n\n");
        sb.Append("#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>\n");
        foreach (string header in components.Select(component => component.NativeHeader)
            .Distinct().OrderBy(header => header, StringComparer.Ordinal)) {
            sb.Append($"#include <{header}>\n");
        }
        sb.Append("\nvoid Engine::RegisterBuiltinComponents(ComponentTypeRegistry& registry) {\n\n");
        foreach (ComponentModel component in components) {
            sb.Append($"\tregistry.Register<{component.NativeType}>({component.ID}, \"{component.RegistryName}\");\n");
        }
        sb.Append("}\n");
        return sb.ToString();
    }

    internal static string EmitNativeAPIFields(List<AbiFieldModel> fields, ulong fingerprint = 0) {
        var sb = new StringBuilder();
        sb.Append("// AUTO-GENERATED FROM ManagedNativeAPI.json\n");
        sb.Append($"\t\tinline static constexpr uint64_t kBindingFingerprint = 0x{fingerprint:x16}ull;\n");
        foreach (AbiFieldModel field in fields) {
            sb.Append($"\t\t{field.NativeType} {field.Name} = nullptr;\n");
        }
        return sb.ToString();
    }
}
