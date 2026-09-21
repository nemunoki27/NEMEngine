using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization.Metadata;
using System.Threading;

namespace NEMEngine;

//============================================================================
//	HostBridge class
//============================================================================
public static unsafe class HostBridge {

    // C++側へ渡す名前文字列の最大バイト数（ScriptTypeDescriptor の displayName 用）
    private const int MaxNameBytes = 128;
    // ManagedScriptTypeDescriptor の固定長フィールド（C++側と一致させる）
    private const int ScriptTypeIDBytes = 40;
    private const int FullTypeNameBytes = 256;
    private const int SourcePathBytes = 260;
    // manifest schema version
    private const int ManifestSchemaVersion = 1;
    // script 例外報告で native へ載せる stack frame の上限（native 側 store の上限と揃える）
    private const int MaxExceptionFrames = 24;

    // public fieldをJSONへ含めるための共通設定。
    // Asset/Entity/Component/ScriptBehaviour/UUID は専用 converter で identity だけを round-trip する。
    private static readonly JsonSerializerOptions jsonOptions = CreateJsonOptions();

    private static JsonSerializerOptions CreateJsonOptions() {

        var options = new JsonSerializerOptions {
            IncludeFields = true,
            // MathTypesのlength/normalizedなどは保存値ではないのでJSON化しない
            IgnoreReadOnlyProperties = true,
            TypeInfoResolver = CreateTypeInfoResolver()
        };
        options.Converters.Add(new UUIDJsonConverter());
        options.Converters.Add(new EntityRefJsonConverter());
        options.Converters.Add(new EntityJsonConverter());
        options.Converters.Add(new AssetJsonConverterFactory());
        options.Converters.Add(new ScriptBehaviourJsonConverterFactory());
        options.Converters.Add(new ComponentJsonConverterFactory());
        return options;
    }

    // IncludeFieldsはpublicフィールドしか対象にしないため、
    // [Serializable]型のprivate [SerializeField]フィールドもJSONへ含めるresolverを作る
    private static IJsonTypeInfoResolver CreateTypeInfoResolver() {

        var resolver = new DefaultJsonTypeInfoResolver();
        resolver.Modifiers.Add(static typeInfo => {

            if (typeInfo.Kind != JsonTypeInfoKind.Object || !HasSerializableFlag(typeInfo.Type)) {
                return;
            }
            // 基底クラスのprivateフィールドはGetFieldsで列挙されないため継承チェーンを辿る
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            for (Type? t = typeInfo.Type; t != null && t != typeof(object); t = t.BaseType) {
                foreach (FieldInfo fieldInfo in t.GetFields(flags)) {
                    if (fieldInfo.GetCustomAttribute<SerializeFieldAttribute>() == null) {
                        continue;
                    }
                    JsonPropertyInfo property = typeInfo.CreateJsonPropertyInfo(fieldInfo.FieldType, fieldInfo.Name);
                    property.Get = fieldInfo.GetValue;
                    property.Set = fieldInfo.SetValue;
                    typeInfo.Properties.Add(property);
                }
            }
        });
        return resolver;
    }

    // 参照解決型フィールドの適用保留リスト。シーンロード時は参照先のscript instanceが未生成のことがあるため、
    // 全インスタンス生成後の最初のlifecycle呼び出し前に一括適用する
    private static readonly List<(ScriptBehaviour script, FieldInfo field, JsonElement value)> pendingReferenceFields = new();

    // managed script instanceの世代付き格納枠。生成/解放を繰り返しても
    // 古いhandleが再利用後の別instanceを指さないようにgenerationで識別する
    private sealed class ScriptInstanceSlot {

        // 1始まりの世代。0は無効handleを表す
        internal uint generation;
        internal ScriptBehaviour? instance;
        internal bool inUse;
        // generation枯渇でこの枠を永久欠番にした（再利用しない）
        internal bool retired;
        // サイズ取得とコピーで共有する実行時データ
        internal byte[]? runtimeStateSnapshot;
    }

    // slot配列とfree list。slot解放時にgenerationを進め、indexはfree listで再利用する
    private static readonly List<ScriptInstanceSlot> slots = new();
    private static readonly Stack<uint> freeSlots = new();

    // 1 つの concrete ScriptBehaviour 型の登録情報（Stable GUID 主キー）
    private sealed class ScriptTypeEntry {

        internal string scriptTypeID = string.Empty;   // 正規化済み GUID
        internal Type type = null!;
        internal string fullTypeName = string.Empty;
        internal string displayName = string.Empty;
        internal string sourcePath = string.Empty;
        internal bool hasExplicitID;
        internal int defaultExecutionOrder;   // [DefaultExecutionOrder] の値（未指定は 0）

        // serialized field schema（defaultValueJson を含む完成形 JSON）。C++ へ blob で渡す。
        // 構築は load 時の一度きり。
        internal string schemaJson = string.Empty;
        // Stable Field GUID -> FieldInfo。runtime get/set と authoring 適用に使う（hot path では reflection しない）
        internal Dictionary<string, FieldInfo> fieldMap = new(StringComparer.Ordinal);
        // Inspectorで表示できるフィールドだけを取得する
        internal Dictionary<string, FieldInfo> runtimeFieldMap = new(StringComparer.Ordinal);
        // 参照解決を全インスタンス生成後まで遅らせるフィールドのGUID集合(Entity/Component/ScriptBehaviour参照)
        internal HashSet<string> deferredFields = new(StringComparer.Ordinal);
    }

    // 現在ロード中のゲームDLLの ScriptBehaviour 型一覧（native へは CopyScriptTypeInfo で順次渡す）
    private static readonly List<ScriptTypeEntry> scriptTypeEntries = new();
    // Stable GUID -> entry（instance 作成・field 取得の解決に使う）
    private static readonly Dictionary<string, ScriptTypeEntry> guidToEntry = new(StringComparer.Ordinal);
    // Type -> entry（runtime instance から schema / field map を引く）
    private static readonly Dictionary<Type, ScriptTypeEntry> typeToEntry = new();
    // authoring default 抽出用の一時 instance キャッシュ（型ごと。reload で破棄）
    private static readonly Dictionary<Type, object?> defaultInstanceCache = new();

    // ゲーム側スクリプトDLL
    private static Assembly? gameAssembly;
    // ゲーム側DLLをアンロード可能にする専用LoadContext
    private static GameScriptLoadContext? gameLoadContext;
    // reload 診断用の連番（ALC unload ログに使う）
    private static int reloadCounter = 0;
    // 直近の collectible ALC unload の typed status（0=Unknown, 1=UnloadSucceeded, 2=LeakSuspected）。
    // Editor は log scraping ではなくこの typed status を参照する。
    private static int lastAlcUnloadStatus = 0;

    //========================================================================
    //	public Methods
    //========================================================================

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InitializeNativeAPI(NativeAPITable* callbacks) {

        // ここでは例外を境界外へ出さない。log callbackは未設定の可能性があるためtry内で使わない
        try {
            if (callbacks == null) {
                return (int)ManagedStatus.InvalidArgument;
            }

            // ABIヘッダでversion / 構造体サイズ / capabilityを検証し、不一致なら関数ポインタを読まない
            ManagedAbiHeader header = callbacks->header;
            if (header.abiVersion != ManagedAbi.Version) {
                return (int)ManagedStatus.AbiMismatch;
            }
            if (header.structSize < (uint)sizeof(NativeAPITable)) {
                return (int)ManagedStatus.AbiMismatch;
            }
            if ((header.capabilities & ManagedAbi.RequiredCapabilities) != ManagedAbi.RequiredCapabilities) {
                return (int)ManagedStatus.AbiMismatch;
            }

            // C++から渡されたECSアクセス関数をScriptCore全体で使えるようにする
            NativeAPI.SetCallbacks(callbacks);
            return (int)ManagedStatus.Ok;
        }
        catch {
            return (int)ManagedStatus.InternalError;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadGameAssembly(byte* assemblyPath) {

        return (int)Guard(nameof(LoadGameAssembly), () => {

            // ゲーム側DLLをロードして、ScriptBehaviour派生型を再収集する
            string? path = PtrToString(assemblyPath);
            if (string.IsNullOrEmpty(path) || !File.Exists(path)) {
                return ManagedStatus.InvalidArgument;
            }

            try {
                WaitForManagedDebuggerIfRequested();

                // 既存DLLを解放してから新しいDLLを読み込む
                ReleaseGameAssembly(collect: true);
                gameLoadContext = new GameScriptLoadContext(path);
                gameAssembly = gameLoadContext.LoadFromAssemblyPath(path);
                RebuildScriptTypes();
                // 新しい assembly の寿命を開始する（unload 前に停止/解放するための起点）
                ScriptRuntimeLifetime.BeginAssemblyLifetime();
                NativeAPI.WriteLog(0, $"Loaded GameScripts: {path}, scriptTypes={scriptTypeEntries.Count}");
                return ManagedStatus.Ok;
            }
            catch (Exception ex) {
                NativeAPI.WriteLog(2, $"Failed to load GameScripts: {path}\n{ex}");
                ReleaseGameAssembly(collect: true);
                return ManagedStatus.InternalError;
            }
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int UnloadGameAssembly() {

        return (int)Guard(nameof(UnloadGameAssembly), () => {
            ReleaseGameAssembly(collect: true);
            return ManagedStatus.Ok;
        });
    }

    // BehaviorSystem が SynchronizeLifecycle の Pass4(OnEnable 後・Start 前)で呼ぶ。
    // Scene の load/unload 完了を検出して SceneManager の SceneLoaded/SceneUnloaded を発火する。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int PumpSceneEvents() {

        return (int)Guard(nameof(PumpSceneEvents), () => {
            // Pass4(OnEnable 後・Start 前)の per-frame pump。Scene / Application イベントを発火する。
            SceneManager.PumpEvents();
            Application.PumpEvents();
            return ManagedStatus.Ok;
        });
    }

    // application shutdown 前に native から一度だけ呼ばれ、Application.Quitting を発火する。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int RaiseApplicationQuitting() {

        return (int)Guard(nameof(RaiseApplicationQuitting), () => {
            Application.RaiseQuitting();
            return ManagedStatus.Ok;
        });
    }

    // BehaviorSystem の各 phase 末から呼ばれる per-frame tick。phase: 0=Update, 1=FixedUpdate, 2=EndOfFrame。
    // Update で 遅延イベント flush + Timer tick + Coroutine(Update)、FixedUpdate で Coroutine(Fixed)、EndOfFrame で Coroutine(EndOfFrame)。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int TickFrame(int phase) {

        return (int)Guard(nameof(TickFrame), () => {
            switch (phase) {
            case 0:
                // 前フレームに積まれた遅延イベントをフレーム頭で 1 回ドレインする
                EventDispatch.FlushDeferred();
                Timers.Tick();
                Coroutines.Tick(CoroutinePhase.Update);
                break;
            case 1:
                Coroutines.Tick(CoroutinePhase.Fixed);
                break;
            case 2:
                Coroutines.Tick(CoroutinePhase.EndOfFrame);
                break;
            }
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptTypeCount(int* outCount) {

        return (int)Guard(nameof(GetScriptTypeCount), () => {
            if (outCount == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outCount = scriptTypeEntries.Count;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptTypeInfo(int index, NativeScriptTypeInfo* outInfo) {

        return (int)Guard(nameof(CopyScriptTypeInfo), () => {

            if (index < 0 || scriptTypeEntries.Count <= index || outInfo == null) {
                return ManagedStatus.InvalidArgument;
            }
            ScriptTypeEntry entry = scriptTypeEntries[index];

            // native registry へ Stable GUID と表示用情報・source path を渡す
            CopyFixed(entry.scriptTypeID, outInfo->scriptTypeID, ScriptTypeIDBytes);
            CopyFixed(entry.fullTypeName, outInfo->fullTypeName, FullTypeNameBytes);
            CopyFixed(entry.displayName, outInfo->displayName, MaxNameBytes);
            CopyFixed(entry.sourcePath, outInfo->sourcePath, SourcePathBytes);
            outInfo->hasExplicitID = entry.hasExplicitID ? 1 : 0;
            outInfo->defaultExecutionOrder = entry.defaultExecutionOrder;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptSchemaJsonSize(byte* scriptTypeID, int* outSize) {

        return (int)Guard(nameof(GetScriptSchemaJsonSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outSize = TryGetEntry(PtrToString(scriptTypeID), out ScriptTypeEntry entry)
                ? Encoding.UTF8.GetByteCount(entry.schemaJson) : 0;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptSchemaJson(byte* scriptTypeID, byte* buffer, int capacity, int* written) {

        return (int)Guard(nameof(CopyScriptSchemaJson), () => {
            if (!TryGetEntry(PtrToString(scriptTypeID), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }
            return WriteUtf8Blob(entry.schemaJson, buffer, capacity, written);
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetRuntimeSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)Guard(nameof(GetRuntimeSerializedStateSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            *outSize = GetRuntimeStateSnapshot(slots[(int)handle.index], true).Length;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyRuntimeSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)Guard(nameof(CopyRuntimeSerializedState), () => {
            if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ScriptInstanceSlot slot = slots[(int)handle.index];
            byte[] snapshot = GetRuntimeStateSnapshot(slot, false);
            ManagedStatus status = WriteUtf8Blob(snapshot, buffer, capacity, written);
            if (status == ManagedStatus.Ok) {
                slot.runtimeStateSnapshot = null;
            }
            return status;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetRuntimeSerializedField(NativeScriptInstanceHandle handle, byte* fieldID, byte* valueJson) {

        return (int)Guard(nameof(SetRuntimeSerializedField), () => {

            // Play中 runtime Inspector の単一 field 編集。live instance のみへ反映する
            if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            string? guid = PtrToString(fieldID);
            if (string.IsNullOrEmpty(guid) || !TryGetFieldInfo(script.GetType(), guid!, out FieldInfo field)) {
                return ManagedStatus.InvalidArgument;
            }
            ApplyFieldValue(script, field, PtrToString(valueJson));
            slots[(int)handle.index].runtimeStateSnapshot = null;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CreateInstance(byte* scriptTypeID, NativeEntity entity, byte* serializedJson,
        ulong scriptSlotID, NativeScriptInstanceHandle* outHandle) {

        return (int)Guard(nameof(CreateInstance), () => {

            if (outHandle == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outHandle = NativeScriptInstanceHandle.Null;

            // Stable GUID から型を解決し、ECSのEntity参照を持つScriptBehaviourを生成する
            if (!TryGetEntry(PtrToString(scriptTypeID), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }

            if (Activator.CreateInstance(entry.type) is not ScriptBehaviour script) {
                return ManagedStatus.InternalError;
            }

            // serialized field 適用 / Awake より前に Entity と scriptSlotID を設定する
            script.entity = new Entity(entity);
            script.scriptSlotID = scriptSlotID;
            ApplySerializedFields(script, PtrToString(serializedJson));

            // 世代付きhandleを発行する。C++側はこのhandleを保持して以後のイベント呼び出しに使う
            *outHandle = AllocateSlot(script);
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GenerateScriptManifest(byte* assemblyPath, byte* manifestOutputPath) {

        // build/reload 時のみ。対象 DLL を一時 collectible ALC（default ALC 非汚染）で反射して
        // Script Type GUID / fullTypeName / sourcePath を集め、検証して manifest JSON を出力する。
        // 現在ロード中の assembly（gameLoadContext）には触れない＝失敗しても現行 DLL を unload しない。
        return (int)Guard(nameof(GenerateScriptManifest), () => {

            string? dll = PtrToString(assemblyPath);
            string? outPath = PtrToString(manifestOutputPath);
            if (string.IsNullOrEmpty(dll) || !File.Exists(dll) || string.IsNullOrEmpty(outPath)) {
                return ManagedStatus.InvalidArgument;
            }
            return GenerateManifestIsolated(dll!, outPath!);
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetSerializedFields(NativeScriptInstanceHandle handle, byte* serializedJson) {

        return (int)Guard(nameof(SetSerializedFields), () => {

            // Play中にInspectorで変更された保存値を、既存のC#インスタンスへ再適用する
            if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ApplySerializedFields(script, PtrToString(serializedJson));
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int FlushPendingReferences() {

        return (int)Guard(nameof(FlushPendingReferences), () => {
            FlushPendingReferenceFields();
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyInstance(NativeScriptInstanceHandle handle) {

        return (int)Guard(nameof(DestroyInstance), () => {
            ReleaseSlot(handle);
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAwake(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.Awake), static script => script.Awake());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeStart(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.Start), static script => script.Start());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnEnable(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.OnEnable), static script => script.OnEnable());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDisable(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.OnDisable), static script => script.OnDisable());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDestroy(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.OnDestroy), static script => script.OnDestroy());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeFixedUpdate(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.FixedUpdate), static script => script.FixedUpdate());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeUpdate(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.Update), static script => script.Update());
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeLateUpdate(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptBehaviour.LateUpdate), static script => script.LateUpdate());
    }

    // Collision系はcollisionをキャプチャするとクロージャがヒープ確保されるため、
    // 高頻度callbackでの確保を避けてGuardInstanceを使わず明示的にguardする
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionEnter(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        FlushPendingReferenceFields();
        try {
            script.OnCollisionEnter(new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            LogScriptException(script, nameof(ScriptBehaviour.OnCollisionEnter), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionStay(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        FlushPendingReferenceFields();
        try {
            script.OnCollisionStay(new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            LogScriptException(script, nameof(ScriptBehaviour.OnCollisionStay), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionExit(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        FlushPendingReferenceFields();
        try {
            script.OnCollisionExit(new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            LogScriptException(script, nameof(ScriptBehaviour.OnCollisionExit), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAnimationEvent(NativeScriptInstanceHandle handle, byte* name, float floatParam, int intParam, byte* stringParam) {

        if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        FlushPendingReferenceFields();
        try {
            script.OnAnimationEvent(new AnimationEvent(PtrToString(name) ?? string.Empty, floatParam, intParam, PtrToString(stringParam) ?? string.Empty));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            LogScriptException(script, nameof(ScriptBehaviour.OnAnimationEvent), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    //========================================================================
    //	private Methods
    //========================================================================

    // export共通の例外封じ込めラッパー。例外を境界外へ出さずManagedStatusへ変換する
    private static ManagedStatus Guard(string apiName, Func<ManagedStatus> body) {

        try {
            return body();
        }
        catch (Exception ex) {
            NativeAPI.WriteLog(2, $"[NativeExport:{apiName}] unhandled managed exception\n{ex}");
            return ManagedStatus.InternalError;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ConfigureScriptProfiler(byte* typeName, NativeEntity entity, ulong slotID) {
        return (int)Guard(nameof(ConfigureScriptProfiler), () => {
            ScriptProfiler.Configure(Marshal.PtrToStringUTF8((nint)typeName) ?? string.Empty, entity, slotID);
            return ManagedStatus.Ok;
        });
    }

    // script callback専用ラッパー。例外時は対象instanceのみScriptExceptionを返し、診断情報を残す
    private static ManagedStatus GuardInstance(NativeScriptInstanceHandle handle, string callbackName, Action<ScriptBehaviour> body) {

        if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }

        // 保留中の参照フィールドをlifecycle実行前に解決する
        FlushPendingReferenceFields();

        try {
            body(script);
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            LogScriptException(script, callbackName, ex);
            return ManagedStatus.ScriptException;
        }
    }

    //========================================================================
    //	script instance slot store
    //========================================================================

    // instanceを格納し、世代付きhandleを発行する。free slotがあれば再利用する
    private static NativeScriptInstanceHandle AllocateSlot(ScriptBehaviour script) {

        if (freeSlots.Count > 0) {

            uint index = freeSlots.Pop();
            ScriptInstanceSlot reused = slots[(int)index];
            // generationはrelease時に進めた値（必ず1以上、retiredは積まれない）
            reused.instance = script;
            reused.inUse = true;
            return new NativeScriptInstanceHandle(index, reused.generation);
        }

        // 空きが無ければ新しい枠を追加する。generationは1始まり
        var slot = new ScriptInstanceSlot { generation = 1, instance = script, inUse = true, retired = false };
        slots.Add(slot);
        return new NativeScriptInstanceHandle((uint)(slots.Count - 1), slot.generation);
    }

    // 同 Entity 上の T 型スクリプト instance を引く（Entity.GetComponent<T> から呼ぶ）。
    // 型 -> Stable GUID を解決し、native registry から handle を引いて managed instance へ戻す。未解決は null。
    // GetComponent<T>のTはComponent制約のためclass制約で受けてcastする
    internal static T? FindScriptAs<T>(NativeEntity owner) where T : class {

        if (!typeToEntry.TryGetValue(typeof(T), out ScriptTypeEntry? entry)) {
            return null;
        }
        NativeScriptInstanceHandle handle = NativeAPI.FindScriptInstance(owner, entry.scriptTypeID);
        return TryResolveSlot(handle, out ScriptBehaviour script) ? script as T : null;
    }

    // Stable GUID 指定で同 Entity 上の script instance を引く（参照フィールドの復元用）
    internal static ScriptBehaviour? FindScriptByGuid(NativeEntity owner, string scriptTypeID) {

        if (string.IsNullOrEmpty(scriptTypeID)) {
            return null;
        }
        NativeScriptInstanceHandle handle = NativeAPI.FindScriptInstance(owner, scriptTypeID);
        return TryResolveSlot(handle, out ScriptBehaviour script) ? script : null;
    }

    // 型の Stable Script Type GUID を返す。未登録型は null（参照フィールドの保存用）
    internal static string? GetScriptTypeGuid(Type type) {
        return typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry.scriptTypeID : null;
    }

    // AddComponent<Script>用に owner Entity へ T を runtime attach し、生成した managed instance を返す。未登録/失敗は null
    internal static T? AttachScriptAs<T>(NativeEntity owner) where T : class {

        if (!typeToEntry.TryGetValue(typeof(T), out ScriptTypeEntry? entry)) {
            return null;
        }
        return NativeAPI.TryAttachScript(owner, entry.scriptTypeID) ? FindScriptAs<T>(owner) : null;
    }

    // 生存する全script instanceから指定型の最初の1件を返す（World.FindEntityWithComponent<Script>用）
    internal static ScriptBehaviour? FindScriptOfTypeByType(Type type) {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                return slot.instance;
            }
        }
        return null;
    }

    // 生存する全script instanceから指定型を全て返す
    internal static List<ScriptBehaviour> FindScriptsOfTypeByType(Type type) {

        var result = new List<ScriptBehaviour>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                result.Add(slot.instance);
            }
        }
        return result;
    }

    // 生存する全script instanceから指定型の最初の1件を返す、未発見はnull。FindObjectOfType用のO(n)走査
    internal static T? FindScriptOfType<T>() where T : ScriptBehaviour {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                return match;
            }
        }
        return null;
    }

    // 生存する全script instanceから指定型を全て返す。FindObjectsOfType用のO(n)走査
    internal static T[] FindScriptsOfType<T>() where T : ScriptBehaviour {

        var result = new List<T>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                result.Add(match);
            }
        }
        return result.ToArray();
    }

    // handleからinstanceをO(1)で解決する。範囲・retired・inUse・instance・generationを全て検証する
    private static bool TryResolveSlot(NativeScriptInstanceHandle handle, out ScriptBehaviour script) {

        script = null!;
        if (!handle.IsValid || handle.index >= (uint)slots.Count) {
            return false;
        }
        ScriptInstanceSlot slot = slots[(int)handle.index];
        if (slot.retired || !slot.inUse || slot.instance is null || slot.generation != handle.generation) {
            return false;
        }
        script = slot.instance;
        return true;
    }

    // slotを解放する。generationを進め、枯渇したslotはretireして二度と再利用しない
    private static void ReleaseSlot(NativeScriptInstanceHandle handle) {

        if (!handle.IsValid || handle.index >= (uint)slots.Count) {
            return;
        }
        ScriptInstanceSlot slot = slots[(int)handle.index];
        if (slot.retired || !slot.inUse || slot.generation != handle.generation) {
            return;
        }

        // owner script 破棄時に、その owner に紐づく coroutine / timer / event 購読を停止・解除する
        if (slot.instance != null) {
            Coroutines.StopAllForOwner(slot.instance);
            Timers.CancelOwnedBy(slot.instance);
            EventOwnerTracker.CancelOwnedBy(slot.instance);
        }
        slot.instance = null;
        slot.runtimeStateSnapshot = null;
        slot.inUse = false;
        RetireOrRecycle(slot, handle.index);
    }

    // generationを進めてfree listへ戻す。uint.MaxValueに達したらwraparoundせずretireする
    private static void RetireOrRecycle(ScriptInstanceSlot slot, uint index) {

        if (slot.generation == uint.MaxValue) {

            // wraparoundすると過去handleとgenerationが再一致し得るため、この枠は永久欠番にする
            slot.retired = true;
            return;
        }
        ++slot.generation;
        freeSlots.Push(index);
    }

    // faultedになったscriptの診断情報をログへ出す
    private static void LogScriptException(ScriptBehaviour script, string callbackName, Exception ex) {

        Type type = script.GetType();
        string typeName = type.FullName ?? type.Name;

        // owner entity handle。解決できればentity名も付ける
        Entity owner = script.entity;
        string entityHandle = $"{owner.native.index}:{owner.native.generation}";
        string entityName = owner.isValid ? owner.name : string.Empty;

        NativeAPI.WriteLog(2,
            $"[ScriptException] callback={callbackName} type={typeName} entity={entityHandle} name=\"{entityName}\"\n{ex}");

        // Console ログとは別に、構造化 DTO を native の exception store へ 1 件報告する。
        // UI はこの store を source of truth にする（ログ文字列の再解析はしない）。
        ReportScriptExceptionDto(script, type, typeName, callbackName, ex, owner, entityName);
    }

    // script 例外を JSON DTO 化して native へ渡す。値の組み立て・stack 取得は例外時のみ実行する。
    private static void ReportScriptExceptionDto(ScriptBehaviour script, Type type, string typeName,
        string callbackName, Exception ex, Entity owner, string entityName) {

        // canonical identity は .cs.meta 由来の scriptTypeID。登録 entry から引く（表示名には使わない）。
        string scriptTypeID = typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry!.scriptTypeID : string.Empty;

        var dto = new JsonObject {
            ["callback"] = callbackName,
            ["slotId"] = script.scriptSlotID,
            ["scriptTypeId"] = scriptTypeID,
            ["typeName"] = typeName,
            ["exceptionType"] = ex.GetType().FullName ?? ex.GetType().Name,
            ["message"] = ex.Message ?? string.Empty,
            ["entityIndex"] = owner.native.index,
            ["entityGeneration"] = owner.native.generation,
            ["entityName"] = entityName,
        };

        // stack frame は file/line 付きで取得し、上限件数だけ載せる（深い stack で肥大させない）。
        var frames = new JsonArray();
        var trace = new StackTrace(ex, true);
        int frameCount = trace.FrameCount;
        for (int i = 0; i < frameCount && frames.Count < MaxExceptionFrames; ++i) {

            StackFrame? frame = trace.GetFrame(i);
            MethodBase? method = frame?.GetMethod();
            if (method == null) {
                continue;
            }
            string memberName = method.DeclaringType != null
                ? $"{method.DeclaringType.FullName}.{method.Name}"
                : method.Name;
            frames.Add(new JsonObject {
                ["method"] = memberName,
                ["file"] = frame?.GetFileName() ?? string.Empty,
                ["line"] = frame?.GetFileLineNumber() ?? 0,
                ["column"] = frame?.GetFileColumnNumber() ?? 0,
            });
        }
        dto["frames"] = frames;

        NativeAPI.ReportScriptExceptionJson(dto.ToJsonString());
    }

    private static void RebuildScriptTypes() {

        scriptTypeEntries.Clear();
        guidToEntry.Clear();
        typeToEntry.Clear();
        defaultInstanceCache.Clear();
        // 旧assemblyのinstanceを指す保留参照はreloadで無効になるため破棄する
        pendingReferenceFields.Clear();

        if (gameAssembly == null) {
            return;
        }

        // 生成 registryから型を登録する
        ScriptTypeDescriptor[]? generated = TryReadGeneratedManifest(gameAssembly);
        if (generated == null) {
            NativeAPI.WriteLog(2,
                "GeneratedScriptManifest was not found. Add the NEM.ScriptCodeGen analyzer to GameScripts.");
            return;
        }
        foreach (ScriptTypeDescriptor descriptor in generated) {

            Type? type = gameAssembly.GetType(descriptor.FullTypeName, throwOnError: false);
            if (type == null) {
                NativeAPI.WriteLog(2, $"Generated manifest type not found in assembly: {descriptor.FullTypeName}");
                continue;
            }
            AddScriptTypeEntry(descriptor.ScriptTypeID, type, descriptor.FullTypeName,
                descriptor.DisplayName, descriptor.SourcePath, descriptor.HasExplicitID);
        }

        // 表示・登録順を安定させる（full type name 昇順）
        scriptTypeEntries.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));

        // 型登録が確定したので serialized field schema と field map を一度だけ構築する（hot path 外）
        if (!BuildSchemaRegistry()) {
            scriptTypeEntries.Clear();
            guidToEntry.Clear();
            typeToEntry.Clear();
            return;
        }

        if (scriptTypeEntries.Count == 0) {
            NativeAPI.WriteLog(1, "GameScripts loaded, but no ScriptBehaviour types were found.");
        }
    }

    // 各 ScriptTypeEntry の schemaJson（defaultValueJson 付き）と fieldMap を構築する
    private static bool BuildSchemaRegistry() {

        JsonObject? generatedByType = TryReadGeneratedSchema(gameAssembly!);
        if (generatedByType == null) {
            NativeAPI.WriteLog(2,
                "GeneratedScriptSchema was not found. Add the NEM.ScriptCodeGen analyzer to GameScripts.");
            return false;
        }

        foreach (ScriptTypeEntry entry in scriptTypeEntries) {

            JsonObject? typeNode = null;
            if (generatedByType != null && generatedByType.TryGetPropertyValue(entry.scriptTypeID, out JsonNode? n) && n is JsonObject obj) {
                typeNode = obj;
            }

            if (typeNode == null) {
                NativeAPI.WriteLog(2, $"Generated script schema was not found: {entry.fullTypeName}");
                return false;
            }

            // field map を作りつつ defaultValueJson を埋める
            object? defaults = CreateDefaultInstance(entry.type);
            if (typeNode["fields"] is JsonArray fields) {
                foreach (JsonNode? fieldNode in fields) {
                    if (fieldNode is not JsonObject fieldObj) {
                        continue;
                    }
                    string fieldID = fieldObj["fieldId"]?.GetValue<string>() ?? string.Empty;
                    string fieldName = fieldObj["name"]?.GetValue<string>() ?? string.Empty;
                    string declaringType = fieldObj["declaringType"]?.GetValue<string>() ?? string.Empty;
                    FieldInfo? info = ResolveFieldInfo(entry.type, declaringType, fieldName);
                    if (info != null && !string.IsNullOrEmpty(fieldID)) {
                        entry.fieldMap[fieldID] = info;
                        if (CanReadRuntimeField(fieldObj)) {
                            entry.runtimeFieldMap[fieldID] = info;
                        }
                        // 参照解決を伴うフィールドは適用を遅延させる([SerializeReference]は候補型に参照が含まれ得る)
                        if (info.GetCustomAttribute<SerializeReferenceAttribute>() != null ||
                            IsDeferredReferenceType(info.FieldType, null)) {
                            entry.deferredFields.Add(fieldID);
                        }
                    }
                    // 既定値（authoring 未設定時の初期値）を埋める
                    fieldObj["defaultValueJson"] = IsUnsupportedField(fieldObj)
                        ? "null" : SerializeFieldDefault(info, defaults);
                }
            }

            entry.schemaJson = typeNode.ToJsonString();
        }
        return true;
    }

    // 生成 schema JSON を scriptTypeID -> typeNode の JsonObject へ変換する。無ければ null
    private static JsonObject? TryReadGeneratedSchema(Assembly assembly) {

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
            if (root?["scripts"] is JsonArray scripts) {
                foreach (JsonNode? scriptNode in scripts) {
                    if (scriptNode is JsonObject scriptObj &&
                        scriptObj["scriptTypeId"]?.GetValue<string>() is string id && !string.IsNullOrEmpty(id)) {
                        byType[id] = scriptObj.DeepClone();
                    }
                }
            }
            return byType;
        }
        catch (Exception ex) {
            NativeAPI.WriteLog(2, $"Failed to read GeneratedScriptSchema\n{ex}");
            return null;
        }
    }

    // 1 型分の entry を登録する。GUID 重複は warning を出して後勝ちを避ける（先勝ち維持）
    private static void AddScriptTypeEntry(string rawGuid, Type type, string fullName, string displayName,
        string sourcePath, bool hasExplicitID) {

        string? normalized = NormalizeGuid(rawGuid);
        if (normalized == null) {
            NativeAPI.WriteLog(2, $"Invalid Script Type GUID for '{fullName}'.");
            return;
        }

        if (guidToEntry.ContainsKey(normalized)) {

            // 重複 GUID。最初の型を維持し、後続は登録しない（manifest validation でも検出する）
            NativeAPI.WriteLog(2,
                $"Duplicate Script Type GUID '{normalized}' for '{fullName}'. Skipping the duplicate registration.");
            return;
        }

        // [DefaultExecutionOrder] を load 時に一度だけ反射で読む（hot path では参照しない）。
        // 値は native の registry まで流れ、Editor override が無いときの default order になる。
        int defaultExecutionOrder = 0;
        DefaultExecutionOrderAttribute? orderAttribute = type.GetCustomAttribute<DefaultExecutionOrderAttribute>();
        if (orderAttribute != null) {
            defaultExecutionOrder = orderAttribute.Order;
        }

        var entry = new ScriptTypeEntry {
            scriptTypeID = normalized,
            type = type,
            fullTypeName = fullName,
            displayName = string.IsNullOrEmpty(displayName) ? type.Name : displayName,
            sourcePath = sourcePath ?? string.Empty,
            hasExplicitID = hasExplicitID,
            defaultExecutionOrder = defaultExecutionOrder,
        };
        scriptTypeEntries.Add(entry);
        guidToEntry[normalized] = entry;
        typeToEntry[type] = entry;
    }

    // 生成 registry（GeneratedScriptManifest.GetDescriptors）を反射で読む。無ければ null
    private static ScriptTypeDescriptor[]? TryReadGeneratedManifest(Assembly assembly) {

        Type? generatedType = assembly.GetType("NEMEngine.GeneratedScriptManifest", throwOnError: false);
        MethodInfo? method = generatedType?.GetMethod("GetDescriptors", BindingFlags.Public | BindingFlags.Static);
        if (method == null) {
            return null;
        }
        return method.Invoke(null, null) as ScriptTypeDescriptor[];
    }

    //========================================================================
    //	Script Manifest 生成（build/reload 時のみ・default ALC 非汚染）
    //========================================================================

    // manifest JSON 出力設定（読みやすい indent）
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

    private static ManagedStatus GenerateManifestIsolated(string dllPath, string outPath) {

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
            NativeAPI.WriteLog(0, $"Generated script manifest: {outPath} (assembly={assemblyName} scripts={root.scripts.Count})");

            // companion: serialized field schema を manifest と同じディレクトリへ出力する（staging artifact）
            string schemaPath = Path.Combine(Path.GetDirectoryName(outPath) ?? string.Empty, "GameScripts.scriptschema.json");
            File.WriteAllText(schemaPath, string.IsNullOrEmpty(schemaJson) ? "{\"schemaVersion\":2,\"scripts\":[]}" : schemaJson);
            NativeAPI.WriteLog(0, $"Generated script schema: {schemaPath}");
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            NativeAPI.WriteLog(2, $"Failed to write script manifest/schema: {outPath}\n{ex}");
            return ManagedStatus.SerializationError;
        }
    }

    // 一時 ALC へ DLL をロードして manifest 内容を収集し、ロード解除する。
    // strong reference を本メソッドのフレームへ閉じ込め、return 後に回収可能にする。
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

                string? normalized = NormalizeGuid(rawGuid);
                if (normalized == null) {
                    NativeAPI.WriteLog(2, $"manifest: invalid Script Type GUID for '{fullName}'.");
                    valid = false;
                    return;
                }
                if (!seenGuids.Add(normalized)) {
                    NativeAPI.WriteLog(2, $"manifest: duplicate Script Type GUID '{normalized}' ('{fullName}').");
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

            ScriptTypeDescriptor[]? generated = TryReadGeneratedManifest(assembly);
            if (generated != null) {
                foreach (ScriptTypeDescriptor descriptor in generated) {
                    AddScript(descriptor.ScriptTypeID, descriptor.FullTypeName, descriptor.DisplayName, descriptor.SourcePath);
                }
            } else {
                NativeAPI.WriteLog(2,
                    $"manifest: GeneratedScriptManifest was not found in '{dllPath}'");
                valid = false;
            }

            root.scripts.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));
            return (root, valid, schemaJson);
        }
        catch (Exception ex) {
            NativeAPI.WriteLog(2, $"manifest: failed to inspect assembly '{dllPath}'\n{ex}");
            return (new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName }, false, schemaJson);
        }
        finally {
            loadContext.Unload();
        }
    }

    private static void WaitForManagedDebuggerIfRequested() {

        // 環境変数が立っている時だけ、C#デバッガのAttachを待つ
        string? wait = Environment.GetEnvironmentVariable("NEM_MANAGED_WAIT_FOR_DEBUGGER");
        if (wait != "1" || Debugger.IsAttached) {
            return;
        }

        if (IsDebuggerPresent()) {
            NativeAPI.WriteLog(1,
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

        NativeAPI.WriteLog(0, $"Waiting for managed debugger attach... timeout={timeoutMs}ms");

        // C++側の実行を止めすぎないよう、タイムアウト付きでAttachを待つ
        Stopwatch stopwatch = Stopwatch.StartNew();
        while (!Debugger.IsAttached && stopwatch.ElapsedMilliseconds < timeoutMs) {
            Thread.Sleep(100);
        }

        if (Debugger.IsAttached) {
            NativeAPI.WriteLog(0, "Managed debugger attached.");
        } else {
            NativeAPI.WriteLog(1, "Managed debugger was not attached before timeout. Continue execution.");
        }
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsDebuggerPresent();

    private static void ReleaseGameAssembly(bool collect) {

        // user code が登録した IDisposable / 購読解除を unload 前に実行し、reload token を cancel する。
        // 古い assembly を参照し続ける task / timer / event を止めて ALC 回収を妨げないようにする。
        ScriptRuntimeLifetime.EndAssemblyLifetime();

        // ロード済みインスタンスや型情報をすべて破棄する。
        // slot配列はclearせず全slotをreleaseしてgenerationを進める。
        // generation履歴を保つことで、reload前のhandleがreload後の別instanceへ届かない（reload epoch相当）。
        ReleaseAllSlots();
        scriptTypeEntries.Clear();
        guidToEntry.Clear();
        typeToEntry.Clear();
        defaultInstanceCache.Clear();
        gameAssembly = null;

        GameScriptLoadContext? loadContext = gameLoadContext;
        gameLoadContext = null;
        if (loadContext == null) {
            return;
        }

        // ALC への strong reference を scope 外へ追い出してから unload する（回収可能にするため）。
        int reloadID = ++reloadCounter;
        string contextName = loadContext.Name ?? "GameScripts";
        WeakReference weakContext = UnloadContextForCollection(loadContext);
        loadContext = null;

        if (!collect) {
            return;
        }

        // 限定回数だけ GC を回して回収を促す（無制限ループはしない）。Edit reload 時のみのコスト。
        const int maxAttempts = 10;
        int attempts = 0;
        for (; attempts < maxAttempts && weakContext.IsAlive; ++attempts) {

            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }

        if (weakContext.IsAlive) {

            // 回収できなかった = どこかに古い assembly への strong reference が残っている
            lastAlcUnloadStatus = 2; // LeakSuspected
            NativeAPI.WriteLog(1,
                $"[ALC leak] GameScripts load context was not collected. reloadId={reloadID} " +
                $"context=\"{contextName}\" attempts={attempts}. " +
                "A static field, running Task/Timer, or unmanaged callback may still reference the old assembly. " +
                "Register disposables / unsubscribes via ScriptRuntimeLifetime so they are released on reload.");
        } else {

            lastAlcUnloadStatus = 1; // UnloadSucceeded
            NativeAPI.WriteLog(0, $"GameScripts load context unloaded. reloadId={reloadID} attempts={attempts}");
        }
    }

    // 直近の collectible ALC unload の typed status を返す（0=Unknown, 1=UnloadSucceeded, 2=LeakSuspected）。
    // reload/unload path でのみ更新され、gameplay frame hot path に GC probe を入れない。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetLastAlcUnloadStatus() {

        return lastAlcUnloadStatus;
    }

    // ALC を unload し、回収判定用の WeakReference を返す。
    // strong reference(引数)はこのメソッドのフレームに閉じ込め、return 後に JIT へ rooted されないようにする。
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference UnloadContextForCollection(GameScriptLoadContext context) {

        context.Unload();
        return new WeakReference(context, trackResurrection: false);
    }

    // 正規化済み Stable GUID から登録 entry を引く
    private static bool TryGetEntry(string? scriptTypeID, out ScriptTypeEntry entry) {

        entry = null!;
        string? normalized = NormalizeGuid(scriptTypeID);
        if (normalized == null) {
            return false;
        }
        return guidToEntry.TryGetValue(normalized, out entry!);
    }

    // GUID を小文字ハイフン("D")形式へ正規化する。不正なら null
    private static string? NormalizeGuid(string? raw) {

        if (string.IsNullOrWhiteSpace(raw)) {
            return null;
        }
        return Guid.TryParse(raw, out Guid guid) ? guid.ToString("D") : null;
    }

    // 全slotをreleaseしてfree listを作り直す。generation履歴は維持し、retired枠は再利用しない
    private static void ReleaseAllSlots() {

        freeSlots.Clear();
        for (int i = 0; i < slots.Count; ++i) {

            ScriptInstanceSlot slot = slots[i];
            if (slot.retired) {
                // 永久欠番はfree listへ戻さない
                continue;
            }
            slot.instance = null;
            slot.runtimeStateSnapshot = null;
            slot.inUse = false;
            RetireOrRecycle(slot, (uint)i);
        }
    }

    // authoring の field 値（{ "<fieldGuid>": <value> } 形式）を instance へ適用する。
    // Stable Field GUID で fieldMap を引くので field 名変更に強い。未知 GUID は skip（C++側で unresolved 保持）。
    // 参照解決を伴うフィールドは参照先が未生成のことがあるため保留リストへ積み、lifecycle呼び出し前に適用する
    private static void ApplySerializedFields(ScriptBehaviour script, string? json) {

        if (string.IsNullOrWhiteSpace(json)) {
            return;
        }
        if (!typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            return;
        }

        using JsonDocument document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object) {
            return;
        }
        foreach (JsonProperty prop in document.RootElement.EnumerateObject()) {
            if (!entry.fieldMap.TryGetValue(prop.Name, out FieldInfo? field)) {
                continue;
            }
            if (entry.deferredFields.Contains(prop.Name)) {
                // JsonDocumentのdispose後も値を保持できるようCloneして積む
                pendingReferenceFields.Add((script, field, prop.Value.Clone()));
            } else {
                SetFieldFromElement(script, field, prop.Value);
            }
        }
    }

    // 保留していた参照フィールドを適用する。lifecycle呼び出しの前に必ず空にする
    private static void FlushPendingReferenceFields() {

        if (pendingReferenceFields.Count == 0) {
            return;
        }
        foreach ((ScriptBehaviour script, FieldInfo field, JsonElement value) in pendingReferenceFields) {
            SetFieldFromElement(script, field, value);
        }
        pendingReferenceFields.Clear();
    }

    // Entity / Component / ScriptBehaviour 参照を含む型か（配列 / List / Nullable / [Serializable]型のメンバも辿る）
    private static bool IsDeferredReferenceType(Type type, HashSet<Type>? visited) {

        if (type == typeof(Entity) || typeof(Component).IsAssignableFrom(type)) {
            return true;
        }
        if (type.IsArray) {
            return IsDeferredReferenceType(type.GetElementType()!, visited);
        }
        if (type.IsGenericType) {
            Type def = type.GetGenericTypeDefinition();
            if (def == typeof(List<>) || def == typeof(Nullable<>)) {
                return IsDeferredReferenceType(type.GetGenericArguments()[0], visited);
            }
        }
        // [Serializable]ネスト型はメンバを辿る、自己参照型は訪問済みsetで打ち切る
        if (IsSerializableObjectType(type)) {
            visited ??= new HashSet<Type>();
            if (!visited.Add(type)) {
                return false;
            }
            foreach (FieldInfo field in EnumerateNestedSerializedFields(type)) {
                if (IsDeferredReferenceType(field.FieldType, visited)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Unityの[Serializable]相当としてメンバ展開する対象の型か
    private static bool IsSerializableObjectType(Type type) {

        if (!HasSerializableFlag(type) || type.IsPrimitive || type.IsEnum || type == typeof(string) ||
            type.IsAbstract || type.IsGenericType) {
            return false;
        }
        if (!type.IsClass && !type.IsValueType) {
            return false;
        }
        // エンジンの参照型階層とBCL型は対象外
        return !typeof(Object).IsAssignableFrom(type) && type.Assembly != typeof(object).Assembly;
    }

    // [Serializable]のメタデータフラグ判定
    // BinaryFormatter廃止で旧形式扱いだが、ここではUnity互換のマーカーとしてフラグだけを読む
#pragma warning disable SYSLIB0050
    private static bool HasSerializableFlag(Type type) {
        return (type.Attributes & TypeAttributes.Serializable) != 0;
    }
#pragma warning restore SYSLIB0050

    // ネスト型の保存対象フィールド(publicまたは[SerializeField])を継承込みで列挙する
    private static IEnumerable<FieldInfo> EnumerateNestedSerializedFields(Type type) {

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? t = type; t != null && t != typeof(object); t = t.BaseType) {
            foreach (FieldInfo field in t.GetFields(flags)) {
                if (field.IsStatic || field.IsInitOnly || field.IsLiteral) {
                    continue;
                }
                if (field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null) {
                    yield return field;
                }
            }
        }
    }

    // JsonElement を field 型へ復元して設定する。reference 型は専用 converter 経由。
    private static void SetFieldFromElement(ScriptBehaviour script, FieldInfo field, JsonElement value) {

        try {
            // [SerializeReference]は保存型を候補検証してからインスタンス化する
            object? deserialized = field.GetCustomAttribute<SerializeReferenceAttribute>() != null
                ? DeserializeManagedReference(field.FieldType, value)
                : value.Deserialize(field.FieldType, jsonOptions);
            field.SetValue(script, deserialized);
        }
        catch (Exception ex) {
            // 型不一致などはその field だけ skip し、他 field と instance を壊さない
            NativeAPI.WriteLog(1, $"Failed to apply field '{field.Name}' on '{script.GetType().FullName}': {ex.Message}");
        }
    }

    // [SerializeReference]フィールドの復元。List<T>/T[]は要素単位で復元する
    private static object? DeserializeManagedReference(Type declaredType, JsonElement value) {

        if (declaredType.IsArray) {
            Type element = declaredType.GetElementType()!;
            if (value.ValueKind != JsonValueKind.Array) {
                return null;
            }
            var array = Array.CreateInstance(element, value.GetArrayLength());
            int index = 0;
            foreach (JsonElement item in value.EnumerateArray()) {
                array.SetValue(DeserializeManagedReferenceValue(element, item), index++);
            }
            return array;
        }
        if (declaredType.IsGenericType && declaredType.GetGenericTypeDefinition() == typeof(List<>)) {
            Type element = declaredType.GetGenericArguments()[0];
            var list = (System.Collections.IList)Activator.CreateInstance(declaredType)!;
            if (value.ValueKind == JsonValueKind.Array) {
                foreach (JsonElement item in value.EnumerateArray()) {
                    list.Add(DeserializeManagedReferenceValue(element, item));
                }
            }
            return list;
        }
        return DeserializeManagedReferenceValue(declaredType, value);
    }

    // {"type","value"}形式1件の復元。宣言基底へ代入できる[Serializable]具象型だけを許可する
    private static object? DeserializeManagedReferenceValue(Type baseType, JsonElement value) {

        if (value.ValueKind != JsonValueKind.Object ||
            !value.TryGetProperty("type", out JsonElement typeElement) || typeElement.ValueKind != JsonValueKind.String) {
            return null;
        }
        string typeName = typeElement.GetString() ?? string.Empty;
        if (string.IsNullOrEmpty(typeName) || gameAssembly == null) {
            return null;
        }
        Type? resolved = gameAssembly.GetType(typeName, throwOnError: false);
        if (resolved == null || resolved.IsAbstract || !HasSerializableFlag(resolved) || !baseType.IsAssignableFrom(resolved)) {
            return null;
        }
        return value.TryGetProperty("value", out JsonElement body) && body.ValueKind == JsonValueKind.Object
            ? body.Deserialize(resolved, jsonOptions)
            : Activator.CreateInstance(resolved);
    }

    // [SerializeReference]フィールドを{"type","value"}形式へ変換する(runtime Inspector表示用)
    private static JsonNode? SerializeManagedReference(Type declaredType, object? value) {

        if (declaredType.IsArray ||
            (declaredType.IsGenericType && declaredType.GetGenericTypeDefinition() == typeof(List<>))) {

            var array = new JsonArray();
            if (value is System.Collections.IEnumerable items) {
                foreach (object? item in items) {
                    array.Add(SerializeManagedReferenceValue(item));
                }
            }
            return array;
        }
        return SerializeManagedReferenceValue(value);
    }

    private static JsonNode SerializeManagedReferenceValue(object? value) {

        if (value == null) {
            return new JsonObject { ["type"] = "", ["value"] = new JsonObject() };
        }
        Type actual = value.GetType();
        return new JsonObject {
            ["type"] = actual.FullName ?? string.Empty,
            ["value"] = JsonSerializer.SerializeToNode(value, actual, jsonOptions),
        };
    }

    // 単一 field（value のみの JSON）を runtime instance へ設定する
    private static void ApplyFieldValue(ScriptBehaviour script, FieldInfo field, string? valueJson) {

        if (valueJson == null) {
            return;
        }
        using JsonDocument document = JsonDocument.Parse(valueJson);
        SetFieldFromElement(script, field, document.RootElement);
    }

    // 非対応型と非表示フィールドは実行時の値取得から除外する
    private static bool CanReadRuntimeField(JsonObject field) {

        return !IsUnsupportedField(field) &&
            field["isHidden"]?.GetValue<bool>() != true;
    }

    // スキーマの表記にかかわらず非対応型を判定する
    private static bool IsUnsupportedField(JsonObject field) {

        return string.Equals(field["kind"]?.GetValue<string>(), "Unsupported", StringComparison.OrdinalIgnoreCase);
    }

    // サイズ取得時に生成しコピー完了まで同じバイト列を保持する
    private static byte[] GetRuntimeStateSnapshot(ScriptInstanceSlot slot, bool refresh) {

        if (refresh || slot.runtimeStateSnapshot == null) {
            slot.runtimeStateSnapshot = Encoding.UTF8.GetBytes(BuildRuntimeStateJson(slot.instance!));
        }
        return slot.runtimeStateSnapshot;
    }

    // runtime instance の現在値を { "<fieldGuid>": <value> } で返す（runtime Inspector 用）
    private static string BuildRuntimeStateJson(ScriptBehaviour script) {

        // 未適用の参照フィールドが残っていると現在値が空に見えるため先に解決する
        FlushPendingReferenceFields();

        if (!typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            return "{}";
        }
        var obj = new JsonObject();
        foreach (KeyValuePair<string, FieldInfo> kv in entry.runtimeFieldMap) {
            try {
                object? value = kv.Value.GetValue(script);
                obj[kv.Key] = kv.Value.GetCustomAttribute<SerializeReferenceAttribute>() != null
                    ? SerializeManagedReference(kv.Value.FieldType, value)
                    : JsonSerializer.SerializeToNode(value, kv.Value.FieldType, jsonOptions);
            }
            catch {
                // 取得できない field は省略する
            }
        }
        return obj.ToJsonString();
    }

    private static bool TryGetFieldInfo(Type type, string fieldID, out FieldInfo field) {

        field = null!;
        if (typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) &&
            entry.fieldMap.TryGetValue(fieldID, out FieldInfo? info)) {
            field = info;
            return true;
        }
        return false;
    }

    // schema の field 名 / 宣言型から FieldInfo を解決する（継承を含めて探索）
    private static FieldInfo? ResolveFieldInfo(Type rootType, string declaringTypeName, string fieldName) {

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? t = rootType; t != null && t != typeof(object); t = t.BaseType) {
            FieldInfo? f = t.GetField(fieldName, flags);
            if (f != null) {
                if (string.IsNullOrEmpty(declaringTypeName) || (f.DeclaringType?.FullName ?? string.Empty) == declaringTypeName) {
                    return f;
                }
            }
        }
        return null;
    }

    // authoring default 抽出用の一時 instance。side-effect-free constructor 前提で cache する（reload で破棄）
    private static object? CreateDefaultInstance(Type type) {

        if (defaultInstanceCache.TryGetValue(type, out object? cached)) {
            return cached;
        }
        object? instance = null;
        try {
            instance = Activator.CreateInstance(type);
        }
        catch (Exception ex) {
            NativeAPI.WriteLog(1, $"Failed to create default instance for '{type.FullName}': {ex.Message}");
        }
        defaultInstanceCache[type] = instance;
        return instance;
    }

    // field の生成直後値を JSON 文字列にする（schema の defaultValueJson 用）
    private static string SerializeFieldDefault(FieldInfo? field, object? defaults) {

        if (field == null) {
            return "null";
        }
        try {
            object? value = defaults != null ? field.GetValue(defaults) : null;
            if (field.GetCustomAttribute<SerializeReferenceAttribute>() != null) {
                return SerializeManagedReference(field.FieldType, value)?.ToJsonString() ?? "null";
            }
            return JsonSerializer.Serialize(value, field.FieldType, jsonOptions);
        }
        catch {
            return "null";
        }
    }

    // 二段階 blob API の出力。buffer 不足は BufferTooSmall。written に必要 byte 数を返す
    private static ManagedStatus WriteUtf8Blob(string text, byte* buffer, int capacity, int* written) {

        return WriteUtf8Blob(Encoding.UTF8.GetBytes(text), buffer, capacity, written);
    }

    // 生成済みのUTF-8データを再変換せずコピーする
    private static ManagedStatus WriteUtf8Blob(byte[] bytes, byte* buffer, int capacity, int* written) {

        if (written != null) {
            *written = bytes.Length;
        }
        if (buffer == null || capacity < bytes.Length) {
            return ManagedStatus.BufferTooSmall;
        }
        for (int i = 0; i < bytes.Length; ++i) {
            buffer[i] = bytes[i];
        }
        return ManagedStatus.Ok;
    }

    private static string? PtrToString(byte* ptr) {

        // C++側のUTF-8 null終端文字列をC#文字列へ変換する
        return ptr == null ? null : Marshal.PtrToStringUTF8((IntPtr)ptr);
    }

    private static void CopyFixed(string value, byte* buffer, int capacity) {

        // C++側 ManagedScriptTypeDescriptor の固定長バッファへ UTF-8 でコピーし null 終端する
        byte[] bytes = Encoding.UTF8.GetBytes(value);

        // 末尾null用に1byte空ける
        int length = Math.Min(bytes.Length, capacity - 1);
        for (int i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }
        buffer[length] = 0;
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

        // native 依存 DLL（P/Invoke 先）を deps.json 経由で解決する。
        // managed の Load() と同じく resolver を使い、解決できた場合だけ明示ロードする。
        protected override IntPtr LoadUnmanagedDll(string unmanagedDllName) {

            string? unmanagedPath = resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
            if (unmanagedPath != null) {
                return LoadUnmanagedDllFromPath(unmanagedPath);
            }
            // 解決できなければ既定動作（OS既定検索）へ委ねる
            return IntPtr.Zero;
        }
    }
}

//============================================================================
//	NativeScriptTypeInfo structure
//	C++側 ManagedScriptTypeDescriptor と同一レイアウト
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeScriptTypeInfo {

    // 正規化済み Stable Script Type GUID
    public fixed byte scriptTypeID[40];
    // 完全修飾型名
    public fixed byte fullTypeName[256];
    // 表示名
    public fixed byte displayName[128];
    // 定義元 .cs パス（drag&drop の source 照合用）
    public fixed byte sourcePath[260];
    // [ScriptTypeID] が明示されていたか
    public int hasExplicitID;
    // [DefaultExecutionOrder] の値（未指定は 0）
    public int defaultExecutionOrder;
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
