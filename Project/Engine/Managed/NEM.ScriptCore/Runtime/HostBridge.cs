using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace NEMEngine;

public static unsafe class HostBridge {

    private const int MaxNameBytes = 128;
    private const int MaxJsonBytes = 512;

    private static readonly JsonSerializerOptions jsonOptions = new() {
        IncludeFields = true
    };
    private static readonly Dictionary<int, ScriptBehaviour> scripts = new();
    private static readonly List<Type> scriptTypes = new();
    private static readonly Dictionary<string, List<SerializedFieldInfo>> fieldCache = new();

    private static Assembly? gameAssembly;
    private static GameScriptLoadContext? gameLoadContext;
    private static int nextScriptID = 1;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InitializeNativeApi(NativeApiTable* callbacks) {

        // C++から渡されたECSアクセス関数をScriptCore全体で使えるようにする
        NativeApi.SetCallbacks(callbacks);
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadGameAssembly(byte* assemblyPath) {

        // ゲーム側DLLをロードして、ScriptBehaviour派生型を再収集する
        string? path = PtrToString(assemblyPath);
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) {
            return 0;
        }

        try {
            ReleaseGameAssembly(collect: true);
            gameLoadContext = new GameScriptLoadContext(path);
            gameAssembly = gameLoadContext.LoadFromAssemblyPath(path);
            RebuildScriptTypes();
            return 1;
        }
        catch {
            ReleaseGameAssembly(collect: true);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void UnloadGameAssembly() {
        ReleaseGameAssembly(collect: true);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptTypeCount() {
        return scriptTypes.Count;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptTypeName(int index, byte* buffer, int capacity) {
        if (index < 0 || scriptTypes.Count <= index || buffer == null || capacity <= 0) {
            return 0;
        }
        return CopyString(scriptTypes[index].FullName ?? scriptTypes[index].Name, buffer, capacity);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetSerializedFieldCount(byte* typeName) {
        Type? type = FindScriptType(PtrToString(typeName));
        return type == null ? 0 : GetSerializedFields(type).Count;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopySerializedFieldInfo(byte* typeName, int index, NativeSerializedFieldInfo* outInfo) {

        // C++ Inspectorが描画できるよう、フィールド名・型・初期値だけを固定長ABIで返す
        Type? type = FindScriptType(PtrToString(typeName));
        if (type == null || outInfo == null) {
            return 0;
        }

        List<SerializedFieldInfo> fields = GetSerializedFields(type);
        if (index < 0 || fields.Count <= index) {
            return 0;
        }

        SerializedFieldInfo field = fields[index];
        outInfo->kind = (int)field.kind;
        outInfo->isPublic = field.isPublic ? 1 : 0;
        CopyFixed(field.name, outInfo->name, MaxNameBytes);
        CopyFixed(field.displayName, outInfo->displayName, MaxNameBytes);
        CopyFixed(field.defaultValueJson, outInfo->defaultValueJson, MaxJsonBytes);
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CreateInstance(byte* typeName, NativeEntity entity, byte* serializedJson) {

        // ECSのEntity参照を持つScriptBehaviourを生成し、Inspector保存値を適用する
        Type? type = FindScriptType(PtrToString(typeName));
        if (type == null) {
            return 0;
        }

        if (Activator.CreateInstance(type) is not ScriptBehaviour script) {
            return 0;
        }

        script.entity = new Entity(entity);
        ApplySerializedFields(script, PtrToString(serializedJson));

        int id = nextScriptID++;
        scripts[id] = script;
        return id;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void DestroyInstance(int handle) {
        scripts.Remove(handle);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeAwake(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.Awake();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeStart(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.Start();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeOnEnable(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.OnEnable();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeOnDisable(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.OnDisable();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeOnDestroy(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.OnDestroy();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeFixedUpdate(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.FixedUpdate();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeUpdate(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.Update();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeLateUpdate(int handle) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.LateUpdate();
        }
    }

    private static void RebuildScriptTypes() {

        // DLLを読み込んだ時点のScriptBehaviour派生型だけを表示対象にする
        scriptTypes.Clear();
        fieldCache.Clear();

        if (gameAssembly == null) {
            return;
        }

        foreach (Type type in gameAssembly.GetTypes()) {
            if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type)) {
                continue;
            }
            if (type.GetConstructor(Type.EmptyTypes) == null) {
                continue;
            }
            scriptTypes.Add(type);
        }
        scriptTypes.Sort((a, b) => string.CompareOrdinal(a.FullName, b.FullName));
    }

    private static void ReleaseGameAssembly(bool collect) {

        scripts.Clear();
        scriptTypes.Clear();
        fieldCache.Clear();
        nextScriptID = 1;
        gameAssembly = null;

        GameScriptLoadContext? loadContext = gameLoadContext;
        gameLoadContext = null;
        loadContext?.Unload();

        if (collect) {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }
    }

    private static Type? FindScriptType(string? typeName) {
        if (string.IsNullOrEmpty(typeName)) {
            return null;
        }
        return scriptTypes.FirstOrDefault(type => type.FullName == typeName || type.Name == typeName);
    }

    private static List<SerializedFieldInfo> GetSerializedFields(Type type) {

        // Unityと同じ方針で、public fieldまたは[SerializeField]付きfieldだけをInspector対象にする
        if (fieldCache.TryGetValue(type.FullName ?? type.Name, out List<SerializedFieldInfo>? cached)) {
            return cached;
        }

        object? defaults = Activator.CreateInstance(type);
        List<SerializedFieldInfo> fields = new();
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        foreach (FieldInfo field in type.GetFields(flags)) {
            if (field.IsStatic || field.IsInitOnly) {
                continue;
            }

            bool isPublic = field.IsPublic;
            bool serializeField = field.GetCustomAttribute<SerializeFieldAttribute>() != null;
            if (!isPublic && !serializeField) {
                continue;
            }

            SerializedFieldKind kind = ResolveFieldKind(field.FieldType);
            if (kind == SerializedFieldKind.None) {
                continue;
            }

            object? defaultValue = defaults != null ? field.GetValue(defaults) : null;
            fields.Add(new SerializedFieldInfo(
                field.Name,
                field.Name,
                kind,
                isPublic,
                SerializeValue(defaultValue, field.FieldType)
            ));
        }

        fieldCache[type.FullName ?? type.Name] = fields;
        return fields;
    }

    private static void ApplySerializedFields(ScriptBehaviour script, string? json) {

        // C++側のScriptEntry.serializedFieldsから、同名フィールドへJSON値を復元する
        if (string.IsNullOrWhiteSpace(json)) {
            return;
        }

        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object) {
            return;
        }

        Type type = script.GetType();
        foreach (SerializedFieldInfo field in GetSerializedFields(type)) {
            if (!root.TryGetProperty(field.name, out JsonElement valueElement)) {
                continue;
            }

            FieldInfo? fieldInfo = type.GetField(field.name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (fieldInfo == null) {
                continue;
            }

            object? value = JsonSerializer.Deserialize(valueElement.GetRawText(), fieldInfo.FieldType, jsonOptions);
            fieldInfo.SetValue(script, value);
        }
    }

    private static SerializedFieldKind ResolveFieldKind(Type type) {
        if (type == typeof(bool)) {
            return SerializedFieldKind.Bool;
        }
        if (type == typeof(int)) {
            return SerializedFieldKind.Int;
        }
        if (type == typeof(float)) {
            return SerializedFieldKind.Float;
        }
        if (type == typeof(double)) {
            return SerializedFieldKind.Double;
        }
        if (type == typeof(string)) {
            return SerializedFieldKind.String;
        }
        if (type == typeof(Vector3)) {
            return SerializedFieldKind.Vector3;
        }
        if (type == typeof(Vector2)) {
            return SerializedFieldKind.Vector2;
        }
        return SerializedFieldKind.None;
    }

    private static string SerializeValue(object? value, Type type) {
        object actualValue = value ?? (type == typeof(string) ? string.Empty : Activator.CreateInstance(type)!);
        return JsonSerializer.Serialize(actualValue, type, jsonOptions);
    }

    private static string? PtrToString(byte* ptr) {
        return ptr == null ? null : Marshal.PtrToStringUTF8((IntPtr)ptr);
    }

    private static int CopyString(string value, byte* buffer, int capacity) {
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        int length = Math.Min(bytes.Length, capacity - 1);
        for (int i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }
        buffer[length] = 0;
        return length;
    }

    private static void CopyFixed(string value, byte* buffer, int capacity) {
        CopyString(value, buffer, capacity);
    }

    private readonly record struct SerializedFieldInfo(
        string name,
        string displayName,
        SerializedFieldKind kind,
        bool isPublic,
        string defaultValueJson
    );

    private enum SerializedFieldKind {
        None = 0,
        Bool,
        Int,
        Float,
        Double,
        String,
        Vector3,
        Vector2
    }

    private sealed class GameScriptLoadContext : AssemblyLoadContext {

        private readonly AssemblyDependencyResolver resolver;
        private readonly Assembly scriptCoreAssembly = typeof(ScriptBehaviour).Assembly;

        public GameScriptLoadContext(string mainAssemblyPath) : base("NEMEngine.GameScripts", isCollectible: true) {
            resolver = new AssemblyDependencyResolver(mainAssemblyPath);
        }

        protected override Assembly? Load(AssemblyName assemblyName) {
            if (assemblyName.Name == scriptCoreAssembly.GetName().Name) {
                return scriptCoreAssembly;
            }

            string? assemblyPath = resolver.ResolveAssemblyToPath(assemblyName);
            return assemblyPath == null ? null : LoadFromAssemblyPath(assemblyPath);
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeSerializedFieldInfo {

    public int kind;
    public int isPublic;
    public fixed byte name[128];
    public fixed byte displayName[128];
    public fixed byte defaultValueJson[512];
}
