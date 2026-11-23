#:property PublishAot=false
#:property NuGetAudit=false
#:property RestoreSources=$(MSBuildProjectDirectory)
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text.Json;

// ゲームのコードはロードせず、空の生成アセンブリだけを除外する
string directory = args[0];
using var file = File.OpenRead(Path.Combine(directory, "GameScripts.dll"));
using var pe = new PEReader(file);
var reader = pe.GetMetadataReader();
bool EmptyAssembly()
{
    if (reader.GetString(reader.GetAssemblyDefinition().Name) != "GameScripts" ||
        reader.ManifestResources.Count != 0 || reader.ExportedTypes.Count != 0 || reader.AssemblyFiles.Count != 0 ||
        pe.PEHeaders.CorHeader?.EntryPointTokenOrRelativeVirtualAddress != 0) return false;

    foreach (var reference in reader.AssemblyReferences)
    {
        if (reader.GetString(reader.GetAssemblyReference(reference).Name) is not ("System.Runtime" or "NEM.ScriptCore")) return false;
    }
    foreach (var handle in reader.TypeDefinitions)
    {
        var type = reader.GetTypeDefinition(handle);
        string name = reader.GetString(type.Name);
        if (name == "<Module>" && type.Namespace.IsNil && type.GetMethods().Count == 0 && type.GetFields().Count == 0) continue;
        if (reader.GetString(type.Namespace) != "NEMEngine" || type.GetMethods().Count != 1) return false;
        var method = reader.GetMethodDefinition(type.GetMethods().Single());
        if (method.RelativeVirtualAddress == 0) return false;
        var il = pe.GetMethodBody(method.RelativeVirtualAddress).GetILBytes();
        if (il == null) return false;
        if (name == "GeneratedScriptManifest" && reader.GetString(method.Name) == "GetDescriptors" && type.GetFields().Count == 0)
        {
            // ldc.i4.0 / newarr ScriptTypeDescriptor / ret
            if (il.Length != 7 || il[0] != 0x16 || il[1] != 0x8d || il[6] != 0x2a) return false;
            var token = MetadataTokens.EntityHandle(BitConverter.ToInt32(il, 2));
            if (token.Kind != HandleKind.TypeReference) return false;
            var element = reader.GetTypeReference((TypeReferenceHandle)token);
            if (reader.GetString(element.Namespace) != "NEMEngine" || reader.GetString(element.Name) != "ScriptTypeDescriptor") return false;
        }
        else if (name == "GeneratedScriptSchema" && reader.GetString(method.Name) == "GetSchemaJson" && type.GetFields().Count == 1)
        {
            var field = reader.GetFieldDefinition(type.GetFields().Single());
            if ((field.Attributes & FieldAttributes.Literal) == 0 || reader.GetString(field.Name) != "Json" ||
                il.Length != 6 || il[0] != 0x72 || il[5] != 0x2a || il[4] != 0x70) return false;
            string schema = reader.GetUserString(MetadataTokens.UserStringHandle(BitConverter.ToInt32(il, 1) & 0x00ffffff));
            using var parsed = JsonDocument.Parse(schema);
            if (parsed.RootElement.GetProperty("scripts").GetArrayLength() != 0) return false;
        }
        else return false;
    }
    using var deps = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(directory, "GameScripts.deps.json")));
    var libraries = deps.RootElement.GetProperty("libraries");
    foreach (var library in libraries.EnumerateObject())
    {
        if (library.Name.Split('/')[0] is not ("GameScripts" or "NEM.ScriptCore")) return false;
    }
    foreach (var target in deps.RootElement.GetProperty("targets").EnumerateObject())
    foreach (var library in target.Value.EnumerateObject())
    {
        if (!libraries.TryGetProperty(library.Name, out _)) return false;
        foreach (var section in library.Value.EnumerateObject())
        {
            if (section.Name == "dependencies") continue;
            if (section.Name != "runtime" || section.Value.EnumerateObject().Any(
                f => f.Name != library.Name.Split('/')[0] + ".dll")) return false;
        }
    }
    return true;
}

static bool ScriptReference(JsonElement element)
{
    if (element.ValueKind == JsonValueKind.Object)
    {
        foreach (var property in element.EnumerateObject())
        {
            if (property.Name is "scriptTypeId" or "scriptAsset" or "lastKnownTypeName" &&
                property.Value.ValueKind == JsonValueKind.String && !string.IsNullOrEmpty(property.Value.GetString())) return true;
            if (ScriptReference(property.Value)) return true;
        }
    }
    else if (element.ValueKind == JsonValueKind.Array)
    {
        foreach (var item in element.EnumerateArray()) if (ScriptReference(item)) return true;
    }
    return false;
}

bool ContentUsesScripts()
{
    using var manifest = JsonDocument.Parse(File.ReadAllBytes(args[1]));
    foreach (var entry in manifest.RootElement.GetProperty("files").EnumerateArray())
    {
        string path = entry.GetProperty("destination").GetString()!.ToLowerInvariant();
        if (path.EndsWith(".dll") || path.EndsWith(".cs")) return true;
        if (!new[] {".scene.json", ".actor.json", ".prefab", ".prefab.json", ".timeline.json"}.Any(path.EndsWith)) continue;
        using var document = JsonDocument.Parse(File.ReadAllBytes(entry.GetProperty("source").GetString()!));
        if (ScriptReference(document.RootElement)) return true;
    }
    return false;
}

Console.WriteLine(EmptyAssembly() && !ContentUsesScripts() ? "NEM_NATIVE_ONLY" : "NEM_MANAGED");

