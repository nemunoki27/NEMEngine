using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace NEMEngine;

//============================================================================
//	HostBridge class
//============================================================================
public static unsafe class HostBridge {

    // C++側へ渡す名前文字列の最大バイト数
    private const int MaxNameBytes = 128;
    // C++側へ渡す初期値JSONの最大バイト数
    private const int MaxJsonBytes = 512;

    // public fieldをJSONへ含めるための共通設定
    private static readonly JsonSerializerOptions jsonOptions = new() {
        IncludeFields = true,
        // MathTypesのlength/normalizedなどは保存値ではないのでJSON化しない
        IgnoreReadOnlyProperties = true
    };

    // C++側に返すscript handleとC#インスタンスの対応表
    private static readonly Dictionary<int, ScriptBehaviour> scripts = new();
    // 現在ロード中のゲームDLLに含まれるScriptBehaviour派生型一覧
    private static readonly List<Type> scriptTypes = new();
    // type nameごとのInspector表示フィールド情報キャッシュ
    private static readonly Dictionary<string, List<SerializedFieldInfo>> fieldCache = new();

    // ゲーム側スクリプトDLL
    private static Assembly? gameAssembly;
    // ゲーム側DLLをアンロード可能にする専用LoadContext
    private static GameScriptLoadContext? gameLoadContext;
    // C++側へ返すscript handleの採番値
    private static int nextScriptID = 1;

    //========================================================================
    //	public Methods
    //========================================================================

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
            WaitForManagedDebuggerIfRequested();

            // 既存DLLを解放してから新しいDLLを読み込む
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

        // C++側の固定長バッファへtype nameをコピーする
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

        // enumはC++側のManagedSerializedFieldKindと同じ値で扱う
        outInfo->kind = (int)field.kind;
        outInfo->isPublic = field.isPublic ? 1 : 0;

        // 文字列はC++側の固定長char配列へコピーする
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

        // C++側はこのIDだけを保持して、以後のイベント呼び出しに使う
        int id = nextScriptID++;
        scripts[id] = script;
        return id;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void SetSerializedFields(int handle, byte* serializedJson) {

        // Play中にInspectorで変更された保存値を、既存のC#インスタンスへ再適用する
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            ApplySerializedFields(script, PtrToString(serializedJson));
        }
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

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeCollisionEnter(int handle, NativeCollisionEvent collision) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.OnCollisionEnter(new Collision(collision));
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeCollisionStay(int handle, NativeCollisionEvent collision) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.OnCollisionStay(new Collision(collision));
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void InvokeCollisionExit(int handle, NativeCollisionEvent collision) {
        if (scripts.TryGetValue(handle, out ScriptBehaviour? script)) {
            script.OnCollisionExit(new Collision(collision));
        }
    }

    //========================================================================
    //	private Methods
    //========================================================================

    private static void RebuildScriptTypes() {

        // DLLを読み込んだ時点のScriptBehaviour派生型だけを表示対象にする
        scriptTypes.Clear();
        fieldCache.Clear();

        if (gameAssembly == null) {
            return;
        }

        foreach (Type type in gameAssembly.GetTypes()) {

            // 抽象クラスやScriptBehaviour以外はC# Scriptとして扱わない
            if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type)) {
                continue;
            }

            // C++側から引数なしで生成できる型だけに限定する
            if (type.GetConstructor(Type.EmptyTypes) == null) {
                continue;
            }
            scriptTypes.Add(type);
        }
        scriptTypes.Sort((a, b) => string.CompareOrdinal(a.FullName, b.FullName));
    }

    private static void WaitForManagedDebuggerIfRequested() {

        // 環境変数が立っている時だけ、C#デバッガのAttachを待つ
        string? wait = Environment.GetEnvironmentVariable("NEM_MANAGED_WAIT_FOR_DEBUGGER");
        if (wait != "1" || Debugger.IsAttached) {
            return;
        }

        if (IsDebuggerPresent()) {
            NativeApi.WriteLog(1,
                "Managed debugger wait skipped: process is already debugged. " +
                "If you want C# breakpoints in GameScripts Visual Studio, run Sandbox without native C++ debugging and then Attach to Process.");
            return;
        }

        // 待機時間は環境変数で上書きできる
        int timeoutMs = 15000;
        string? timeoutText = Environment.GetEnvironmentVariable("NEM_MANAGED_WAIT_TIMEOUT_MS");
        if (!string.IsNullOrWhiteSpace(timeoutText) &&
            int.TryParse(timeoutText, out int parsedTimeout) &&
            0 < parsedTimeout) {
            timeoutMs = parsedTimeout;
        }

        NativeApi.WriteLog(0, $"Waiting for managed debugger attach... timeout={timeoutMs}ms");

        // C++側の実行を止めすぎないよう、タイムアウト付きでAttachを待つ
        Stopwatch stopwatch = Stopwatch.StartNew();
        while (!Debugger.IsAttached && stopwatch.ElapsedMilliseconds < timeoutMs) {
            Thread.Sleep(100);
        }

        if (Debugger.IsAttached) {
            NativeApi.WriteLog(0, "Managed debugger attached.");
        } else {
            NativeApi.WriteLog(1, "Managed debugger was not attached before timeout. Continue execution.");
        }
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsDebuggerPresent();

    private static void ReleaseGameAssembly(bool collect) {

        // ロード済みインスタンスや型情報をすべて破棄する
        scripts.Clear();
        scriptTypes.Clear();
        fieldCache.Clear();
        nextScriptID = 1;
        gameAssembly = null;

        // LoadContextをUnloadすることでDLL差し替えを可能にする
        GameScriptLoadContext? loadContext = gameLoadContext;
        gameLoadContext = null;
        loadContext?.Unload();

        if (collect) {
            // collect=trueのときはDLLファイルロックを外しやすくするためGCを明示実行する
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }
    }

    private static Type? FindScriptType(string? typeName) {

        // FullName優先だが、C++側からNameだけが来ても見つけられるようにする
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

        // public/private両方を見て、privateは[SerializeField]付きだけ通す
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        foreach (FieldInfo field in type.GetFields(flags)) {

            // staticとreadonlyはインスタンスごとのInspector値として扱わない
            if (field.IsStatic || field.IsInitOnly) {
                continue;
            }

            // Unity寄りのルール: public fieldまたは[SerializeField]付きprivate field
            bool isPublic = field.IsPublic;
            bool serializeField = field.GetCustomAttribute<SerializeFieldAttribute>() != null;
            if (!isPublic && !serializeField) {
                continue;
            }

            // C++側のInspectorで描画できる型だけを登録する
            SerializedFieldKind kind = ResolveFieldKind(field.FieldType);
            if (kind == SerializedFieldKind.None) {
                continue;
            }

            // インスタンス生成直後の値をInspectorの初期値として保存する
            object? defaultValue = defaults != null ? field.GetValue(defaults) : null;
            fields.Add(new SerializedFieldInfo(
                field.Name,
                field.Name,
                kind,
                isPublic,
                SerializeValue(defaultValue, field.FieldType)
            ));
        }

        // 同じ型の問い合わせが多いため、反射結果はキャッシュする
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

            // 保存されていないフィールドはC#側の初期値をそのまま使う
            if (!root.TryGetProperty(field.name, out JsonElement valueElement)) {
                continue;
            }

            // private fieldにも値を戻すため、public/non-public両方を検索する
            FieldInfo? fieldInfo = type.GetField(field.name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (fieldInfo == null) {
                continue;
            }

            // MathTypes.csのstructはIncludeFields=trueで直接復元できる
            object? value = JsonSerializer.Deserialize(valueElement.GetRawText(), fieldInfo.FieldType, jsonOptions);
            fieldInfo.SetValue(script, value);
        }
    }

    private static SerializedFieldKind ResolveFieldKind(Type type) {

        // C++ Inspector側と同じ種類に分類する
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
        if (type == typeof(Vector4)) {
            return SerializedFieldKind.Vector4;
        }
        if (type == typeof(Quaternion)) {
            return SerializedFieldKind.Quaternion;
        }
        if (type == typeof(Color3)) {
            return SerializedFieldKind.Color3;
        }
        if (type == typeof(Color4)) {
            return SerializedFieldKind.Color4;
        }
        return SerializedFieldKind.None;
    }

    private static string SerializeValue(object? value, Type type) {

        // nullは型ごとのデフォルト値に置き換えてJSON化する
        object actualValue = value ?? (type == typeof(string) ? string.Empty : Activator.CreateInstance(type)!);
        return JsonSerializer.Serialize(actualValue, type, jsonOptions);
    }

    private static string? PtrToString(byte* ptr) {

        // C++側のUTF-8 null終端文字列をC#文字列へ変換する
        return ptr == null ? null : Marshal.PtrToStringUTF8((IntPtr)ptr);
    }

    private static int CopyString(string value, byte* buffer, int capacity) {

        // C++側の固定長バッファへUTF-8でコピーする
        byte[] bytes = Encoding.UTF8.GetBytes(value);

        // 末尾null用に1byte空ける
        int length = Math.Min(bytes.Length, capacity - 1);
        for (int i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }

        // C++側で扱いやすいようnull終端する
        buffer[length] = 0;
        return length;
    }

    private static void CopyFixed(string value, byte* buffer, int capacity) {
        CopyString(value, buffer, capacity);
    }

    // C#側からC++ Inspectorへ渡すフィールド情報
    private readonly record struct SerializedFieldInfo(
        // 実際のC#フィールド名
        string name,
        // Inspectorに表示する名前
        string displayName,
        // C++側で描画方法を選ぶための種類
        SerializedFieldKind kind,
        // public fieldかどうか
        bool isPublic,
        // C#インスタンス生成直後の初期値JSON
        string defaultValueJson
    );

    // C++側のManagedSerializedFieldKindと同じ順番にする
    private enum SerializedFieldKind {
        None = 0,
        Bool,
        Int,
        Float,
        Double,
        String,
        Vector3,
        Vector2,
        Vector4,
        Quaternion,
        Color3,
        Color4
    }

    // ゲーム側DLL専用のAssemblyLoadContext
    private sealed class GameScriptLoadContext : AssemblyLoadContext {

        // ゲームDLLの依存関係解決に使うResolver
        private readonly AssemblyDependencyResolver resolver;
        // ScriptCore本体はホスト側で読み込まれているものを共有する
        private readonly Assembly scriptCoreAssembly = typeof(ScriptBehaviour).Assembly;

        public GameScriptLoadContext(string mainAssemblyPath) : base("NEMEngine.GameScripts", isCollectible: true) {
            resolver = new AssemblyDependencyResolver(mainAssemblyPath);
        }

        protected override Assembly? Load(AssemblyName assemblyName) {

            // NEM.ScriptCoreはゲームDLL側へ重複ロードしない
            if (assemblyName.Name == scriptCoreAssembly.GetName().Name) {
                return scriptCoreAssembly;
            }

            // GameScripts.dllの横にある依存DLLを解決する
            string? assemblyPath = resolver.ResolveAssemblyToPath(assemblyName);
            return assemblyPath == null ? null : LoadFromAssemblyPath(assemblyPath);
        }
    }
}

//============================================================================
//	NativeSerializedFieldInfo structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeSerializedFieldInfo {

    // SerializedFieldKind
    public int kind;
    // public fieldなら1、private SerializeFieldなら0
    public int isPublic;
    // フィールド名
    public fixed byte name[128];
    // Inspector表示名
    public fixed byte displayName[128];
    // 初期値JSON
    public fixed byte defaultValueJson[512];
}

//============================================================================
//	NativeCollisionEvent structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct NativeCollisionEvent {

    // コールバックを受け取るEntityと相手Entity
    public NativeEntity self;
    public NativeEntity other;
    // 接触情報
    public NativeVector3 normal;
    public NativeVector3 point;
    public float penetration;
    // 衝突した形状インデックス
    public int selfShapeIndex;
    public int otherShapeIndex;
    // Trigger接触なら1
    public int isTrigger;
}
