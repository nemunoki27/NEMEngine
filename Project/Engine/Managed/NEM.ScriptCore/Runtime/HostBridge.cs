using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;

namespace NEMEngine;

//============================================================================
//	HostBridge class
//============================================================================
public static unsafe class HostBridge {

    // C++側へ渡す名前文字列の最大バイト数（ScriptTypeDescriptor の displayName 用）
    private const int MaxNameBytes = 128;
    // ManagedScriptTypeDescriptor の固定長フィールド（C++側と一致させる）
    private const int ScriptTypeIdBytes = 40;
    private const int FullTypeNameBytes = 256;
    private const int SourcePathBytes = 260;
    // manifest schema version
    private const int ManifestSchemaVersion = 1;
    // script 例外報告で native へ載せる stack frame の上限（native 側 store の上限と揃える）
    private const int MaxExceptionFrames = 24;

    // public fieldをJSONへ含めるための共通設定。
    // AssetRef/EntityRef/ScriptRef/UUID は専用 converter で identity だけを round-trip する。
    private static readonly JsonSerializerOptions jsonOptions = CreateJsonOptions();

    private static JsonSerializerOptions CreateJsonOptions() {

        var options = new JsonSerializerOptions {
            IncludeFields = true,
            // MathTypesのlength/normalizedなどは保存値ではないのでJSON化しない
            IgnoreReadOnlyProperties = true
        };
        options.Converters.Add(new UUIDJsonConverter());
        options.Converters.Add(new EntityRefJsonConverter());
        options.Converters.Add(new AssetRefJsonConverterFactory());
        options.Converters.Add(new ScriptRefJsonConverterFactory());
        return options;
    }

    // managed script instanceの世代付き格納枠。生成/解放を繰り返しても
    // 古いhandleが再利用後の別instanceを指さないようにgenerationで識別する
    private sealed class ScriptInstanceSlot {

        // 1始まりの世代。0は無効handleを表す
        internal uint generation;
        internal ScriptBehaviour? instance;
        internal bool inUse;
        // generation枯渇でこの枠を永久欠番にした（再利用しない）
        internal bool retired;
    }

    // slot配列とfree list。slot解放時にgenerationを進め、indexはfree listで再利用する
    private static readonly List<ScriptInstanceSlot> slots = new();
    private static readonly Stack<uint> freeSlots = new();

    // 1 つの concrete ScriptBehaviour 型の登録情報（Stable GUID 主キー）
    private sealed class ScriptTypeEntry {

        internal string scriptTypeId = string.Empty;   // 正規化済み GUID
        internal Type type = null!;
        internal string fullTypeName = string.Empty;
        internal string displayName = string.Empty;
        internal string sourcePath = string.Empty;
        internal bool hasExplicitId;
        internal int defaultExecutionOrder;   // [DefaultExecutionOrder] の値（未指定は 0）
        internal string[] formerlyKnown = Array.Empty<string>();

        // serialized field schema（defaultValueJson を含む完成形 JSON）。C++ へ blob で渡す。
        // 構築は load 時の一度きり。null は schema 未構築（reflection fallback でも生成する）。
        internal string schemaJson = string.Empty;
        // Stable Field GUID -> FieldInfo。runtime get/set と authoring 適用に使う（hot path では reflection しない）
        internal Dictionary<string, FieldInfo> fieldMap = new(StringComparer.Ordinal);
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
    public static int InitializeNativeApi(NativeApiTable* callbacks) {

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
            if (header.structSize < (uint)sizeof(NativeApiTable)) {
                return (int)ManagedStatus.AbiMismatch;
            }
            if ((header.capabilities & ManagedAbi.RequiredCapabilities) != ManagedAbi.RequiredCapabilities) {
                return (int)ManagedStatus.AbiMismatch;
            }

            // C++から渡されたECSアクセス関数をScriptCore全体で使えるようにする
            NativeApi.SetCallbacks(callbacks);
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
                NativeApi.WriteLog(0, $"Loaded GameScripts: {path}, scriptTypes={scriptTypeEntries.Count}");
                return ManagedStatus.Ok;
            }
            catch (Exception ex) {
                NativeApi.WriteLog(2, $"Failed to load GameScripts: {path}\n{ex}");
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
    // Update で Timer tick + Coroutine(Update)、FixedUpdate で Coroutine(Fixed)、EndOfFrame で Coroutine(EndOfFrame)。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int TickFrame(int phase) {

        return (int)Guard(nameof(TickFrame), () => {
            switch (phase) {
            case 0:
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
            CopyFixed(entry.scriptTypeId, outInfo->scriptTypeId, ScriptTypeIdBytes);
            CopyFixed(entry.fullTypeName, outInfo->fullTypeName, FullTypeNameBytes);
            CopyFixed(entry.displayName, outInfo->displayName, MaxNameBytes);
            CopyFixed(entry.sourcePath, outInfo->sourcePath, SourcePathBytes);
            outInfo->hasExplicitId = entry.hasExplicitId ? 1 : 0;
            outInfo->defaultExecutionOrder = entry.defaultExecutionOrder;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptSchemaJsonSize(byte* scriptTypeId, int* outSize) {

        return (int)Guard(nameof(GetScriptSchemaJsonSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outSize = TryGetEntry(PtrToString(scriptTypeId), out ScriptTypeEntry entry)
                ? Encoding.UTF8.GetByteCount(entry.schemaJson) : 0;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptSchemaJson(byte* scriptTypeId, byte* buffer, int capacity, int* written) {

        return (int)Guard(nameof(CopyScriptSchemaJson), () => {
            if (!TryGetEntry(PtrToString(scriptTypeId), out ScriptTypeEntry entry)) {
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
            *outSize = Encoding.UTF8.GetByteCount(BuildRuntimeStateJson(script));
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyRuntimeSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)Guard(nameof(CopyRuntimeSerializedState), () => {
            if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            return WriteUtf8Blob(BuildRuntimeStateJson(script), buffer, capacity, written);
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetRuntimeSerializedField(NativeScriptInstanceHandle handle, byte* fieldId, byte* valueJson) {

        return (int)Guard(nameof(SetRuntimeSerializedField), () => {

            // Play中 runtime Inspector の単一 field 編集。live instance のみへ反映する
            if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            string? guid = PtrToString(fieldId);
            if (string.IsNullOrEmpty(guid) || !TryGetFieldInfo(script.GetType(), guid!, out FieldInfo field)) {
                return ManagedStatus.InvalidArgument;
            }
            ApplyFieldValue(script, field, PtrToString(valueJson));
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CreateInstance(byte* scriptTypeId, NativeEntity entity, byte* serializedJson,
        ulong scriptSlotId, NativeScriptInstanceHandle* outHandle) {

        return (int)Guard(nameof(CreateInstance), () => {

            if (outHandle == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outHandle = NativeScriptInstanceHandle.Null;

            // Stable GUID から型を解決し、ECSのEntity参照を持つScriptBehaviourを生成する
            if (!TryGetEntry(PtrToString(scriptTypeId), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }

            if (Activator.CreateInstance(entry.type) is not ScriptBehaviour script) {
                return ManagedStatus.InternalError;
            }

            // serialized field 適用 / Awake より前に Entity と scriptSlotID を設定する
            script.entity = new Entity(entity);
            script.scriptSlotId = scriptSlotId;
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
        try {
            script.OnCollisionExit(new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            LogScriptException(script, nameof(ScriptBehaviour.OnCollisionExit), ex);
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
            NativeApi.WriteLog(2, $"[NativeExport:{apiName}] unhandled managed exception\n{ex}");
            return ManagedStatus.InternalError;
        }
    }

    // script callback専用ラッパー。例外時は対象instanceのみScriptExceptionを返し、診断情報を残す
    private static ManagedStatus GuardInstance(NativeScriptInstanceHandle handle, string callbackName, Action<ScriptBehaviour> body) {

        if (!TryResolveSlot(handle, out ScriptBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }

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

    // 同 Entity 上の T 型スクリプト instance を引く（Entity.GetComponent<T> / ScriptBehaviour.GetComponent<T> から呼ぶ）。
    // 型 -> Stable GUID を解決し、native registry から handle を引いて managed instance へ戻す。未解決は null。
    internal static T? FindScript<T>(NativeEntity owner) where T : ScriptBehaviour {

        if (!typeToEntry.TryGetValue(typeof(T), out ScriptTypeEntry? entry)) {
            return null;
        }
        NativeScriptInstanceHandle handle = NativeApi.FindScriptInstance(owner, entry.scriptTypeId);
        return TryResolveSlot(handle, out ScriptBehaviour script) ? script as T : null;
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

        // owner script 破棄時に、その owner に紐づく coroutine / timer を停止・cancel する
        if (slot.instance != null) {
            Coroutines.StopAllForOwner(slot.instance);
            Timers.CancelOwnedBy(slot.instance);
        }
        slot.instance = null;
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

        NativeApi.WriteLog(2,
            $"[ScriptException] callback={callbackName} type={typeName} entity={entityHandle} name=\"{entityName}\"\n{ex}");

        // Console ログとは別に、構造化 DTO を native の exception store へ 1 件報告する。
        // UI はこの store を source of truth にする（ログ文字列の再解析はしない）。
        ReportScriptExceptionDto(script, type, typeName, callbackName, ex, owner, entityName);
    }

    // script 例外を JSON DTO 化して native へ渡す。値の組み立て・stack 取得は例外時のみ実行する。
    private static void ReportScriptExceptionDto(ScriptBehaviour script, Type type, string typeName,
        string callbackName, Exception ex, Entity owner, string entityName) {

        // canonical identity は .cs.meta 由来の scriptTypeId。登録 entry から引く（表示名には使わない）。
        string scriptTypeId = typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry!.scriptTypeId : string.Empty;

        var dto = new JsonObject {
            ["callback"] = callbackName,
            ["slotId"] = script.scriptSlotId,
            ["scriptTypeId"] = scriptTypeId,
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

        NativeApi.ReportScriptExceptionJson(dto.ToJsonString());
    }

    private static void RebuildScriptTypes() {

        scriptTypeEntries.Clear();
        guidToEntry.Clear();
        typeToEntry.Clear();
        defaultInstanceCache.Clear();

        if (gameAssembly == null) {
            return;
        }

        // 生成 registry（GeneratedScriptManifest）を優先し、無ければ reflection へ fallback する。
        // 生成 registry のみ sourcePath を持つため drag&drop の source 照合に必要。
        ScriptTypeDescriptor[]? generated = TryReadGeneratedManifest(gameAssembly);
        if (generated != null) {

            foreach (ScriptTypeDescriptor descriptor in generated) {

                Type? type = gameAssembly.GetType(descriptor.FullTypeName, throwOnError: false);
                if (type == null) {
                    NativeApi.WriteLog(2, $"Generated manifest type not found in assembly: {descriptor.FullTypeName}");
                    continue;
                }
                AddScriptTypeEntry(descriptor.ScriptTypeId, type, descriptor.FullTypeName,
                    descriptor.DisplayName, descriptor.SourcePath, descriptor.HasExplicitId, descriptor.FormerlyKnownTypeNames);
            }
        } else {

            // 生成 registry が無い（generator 未適用）。reflection で fallback（sourcePath なし）
            NativeApi.WriteLog(1,
                "GeneratedScriptManifest was not found; falling back to reflection scan " +
                "(drag&drop source mapping is unavailable; add the NEM.ScriptCodeGen analyzer to GameScripts).");

            foreach (Type type in gameAssembly.GetTypes()) {

                if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type)) {
                    continue;
                }
                if (type.GetConstructor(Type.EmptyTypes) == null) {
                    continue;
                }
                string fullName = type.FullName ?? type.Name;
                (string guid, bool explicitId) = ResolveGuidFromAttribute(type, fullName);
                AddScriptTypeEntry(guid, type, fullName, type.Name, string.Empty, explicitId, ReadFormerlyKnown(type));
            }
        }

        // 表示・登録順を安定させる（full type name 昇順）
        scriptTypeEntries.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));

        // 型登録が確定したので serialized field schema と field map を一度だけ構築する（hot path 外）
        BuildSchemaRegistry();

        if (scriptTypeEntries.Count == 0) {
            NativeApi.WriteLog(1, "GameScripts loaded, but no ScriptBehaviour types were found.");
        }
    }

    // 各 ScriptTypeEntry の schemaJson（defaultValueJson 付き）と fieldMap を構築する。
    // 生成 schema（GeneratedScriptSchema）を優先し、無ければ reflection で最小限を生成する。
    private static void BuildSchemaRegistry() {

        JsonObject? generatedByType = TryReadGeneratedSchema(gameAssembly!);

        foreach (ScriptTypeEntry entry in scriptTypeEntries) {

            JsonObject? typeNode = null;
            if (generatedByType != null && generatedByType.TryGetPropertyValue(entry.scriptTypeId, out JsonNode? n) && n is JsonObject obj) {
                typeNode = obj;
            }

            if (typeNode == null) {
                // 生成 schema が無い型は reflection fallback で最小 schema を作る
                typeNode = BuildReflectionSchema(entry);
            }

            // field map を作りつつ defaultValueJson を埋める
            object? defaults = CreateDefaultInstance(entry.type);
            if (typeNode["fields"] is JsonArray fields) {
                foreach (JsonNode? fieldNode in fields) {
                    if (fieldNode is not JsonObject fieldObj) {
                        continue;
                    }
                    string fieldId = fieldObj["fieldId"]?.GetValue<string>() ?? string.Empty;
                    string fieldName = fieldObj["name"]?.GetValue<string>() ?? string.Empty;
                    string declaringType = fieldObj["declaringType"]?.GetValue<string>() ?? string.Empty;
                    FieldInfo? info = ResolveFieldInfo(entry.type, declaringType, fieldName);
                    if (info != null && !string.IsNullOrEmpty(fieldId)) {
                        entry.fieldMap[fieldId] = info;
                    }
                    // 既定値（authoring 未設定時の初期値）を埋める
                    fieldObj["defaultValueJson"] = SerializeFieldDefault(info, defaults);
                }
            }

            entry.schemaJson = typeNode.ToJsonString();
        }
    }

    // 生成 schema JSON を scriptTypeId -> typeNode の JsonObject へ変換する。無ければ null
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
            NativeApi.WriteLog(2, $"Failed to read GeneratedScriptSchema\n{ex}");
            return null;
        }
    }

    // 生成 schema が無い型のための最小 schema（reflection）。属性は反映しない。
    private static JsonObject BuildReflectionSchema(ScriptTypeEntry entry) {

        var fields = new JsonArray();
        foreach (FieldInfo field in EnumerateSerializedFields(entry.type)) {
            string declaringType = field.DeclaringType?.FullName ?? entry.fullTypeName;
            string fieldId = DeterministicGuid("NEMEngine.ScriptField:" + entry.scriptTypeId + "/" + declaringType + "/" + field.Name);
            var node = new JsonObject {
                ["fieldId"] = fieldId,
                ["name"] = field.Name,
                ["declaringType"] = declaringType,
                ["isPublic"] = field.IsPublic,
                ["isReadOnly"] = false,
                ["isHidden"] = false,
                ["multiline"] = false,
                ["kind"] = ReflectionKindName(field.FieldType),
            };
            fields.Add(node);
        }
        return new JsonObject {
            ["scriptTypeId"] = entry.scriptTypeId,
            ["fullTypeName"] = entry.fullTypeName,
            ["fields"] = fields,
        };
    }

    // 1 型分の entry を登録する。GUID 重複は warning を出して後勝ちを避ける（先勝ち維持）
    private static void AddScriptTypeEntry(string rawGuid, Type type, string fullName, string displayName,
        string sourcePath, bool hasExplicitId, string[] formerlyKnown) {

        string? normalized = NormalizeGuid(rawGuid);
        if (normalized == null) {
            // 不正 GUID は決定的 fallback で救済（generator 側でも error 診断済み）
            normalized = DeterministicGuid(fullName);
            hasExplicitId = false;
            NativeApi.WriteLog(2, $"Invalid Script Type GUID for '{fullName}'. Using a fallback GUID.");
        }

        if (guidToEntry.ContainsKey(normalized)) {

            // 重複 GUID。最初の型を維持し、後続は登録しない（manifest validation でも検出する）
            NativeApi.WriteLog(2,
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
            scriptTypeId = normalized,
            type = type,
            fullTypeName = fullName,
            displayName = string.IsNullOrEmpty(displayName) ? type.Name : displayName,
            sourcePath = sourcePath ?? string.Empty,
            hasExplicitId = hasExplicitId,
            defaultExecutionOrder = defaultExecutionOrder,
            formerlyKnown = formerlyKnown ?? Array.Empty<string>(),
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

    // 型から [ScriptTypeId] を読み正規化する。無ければ決定的 fallback（hasExplicit=false）
    private static (string guid, bool hasExplicit) ResolveGuidFromAttribute(Type type, string fullName) {

        ScriptTypeIdAttribute? attribute = type.GetCustomAttribute<ScriptTypeIdAttribute>(inherit: false);
        if (attribute != null) {

            string? normalized = NormalizeGuid(attribute.Value);
            if (normalized != null) {
                return (normalized, true);
            }
            NativeApi.WriteLog(2, $"Invalid [ScriptTypeId] on '{fullName}'. Using a fallback GUID.");
            return (DeterministicGuid(fullName), false);
        }
        NativeApi.WriteLog(1, $"Script type '{fullName}' has no [ScriptTypeId]; using a migration fallback GUID.");
        return (DeterministicGuid(fullName), false);
    }

    // 型から [FormerlyKnownScriptType] の旧 full type name を集める
    private static string[] ReadFormerlyKnown(Type type) {

        var names = new List<string>();
        foreach (FormerlyKnownScriptTypeAttribute attribute in type.GetCustomAttributes<FormerlyKnownScriptTypeAttribute>(inherit: false)) {
            if (!string.IsNullOrWhiteSpace(attribute.FullTypeName)) {
                names.Add(attribute.FullTypeName);
            }
        }
        return names.Count == 0 ? Array.Empty<string>() : names.ToArray();
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
        public string scriptTypeId { get; set; } = string.Empty;
        public string fullTypeName { get; set; } = string.Empty;
        public string displayName { get; set; } = string.Empty;
        public string sourcePath { get; set; } = string.Empty;
        public string sourceAssetId { get; set; } = string.Empty;
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
            NativeApi.WriteLog(0, $"Generated script manifest: {outPath} (assembly={assemblyName} scripts={root.scripts.Count})");

            // companion: serialized field schema を manifest と同じディレクトリへ出力する（staging artifact）
            string schemaPath = Path.Combine(Path.GetDirectoryName(outPath) ?? string.Empty, "GameScripts.scriptschema.json");
            File.WriteAllText(schemaPath, string.IsNullOrEmpty(schemaJson) ? "{\"schemaVersion\":2,\"scripts\":[]}" : schemaJson);
            NativeApi.WriteLog(0, $"Generated script schema: {schemaPath}");
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            NativeApi.WriteLog(2, $"Failed to write script manifest/schema: {outPath}\n{ex}");
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
                    NativeApi.WriteLog(2, $"manifest: invalid Script Type GUID for '{fullName}'.");
                    valid = false;
                    return;
                }
                if (!seenGuids.Add(normalized)) {
                    NativeApi.WriteLog(2, $"manifest: duplicate Script Type GUID '{normalized}' ('{fullName}').");
                    valid = false;
                    return;
                }
                root.scripts.Add(new ManifestScript {
                    scriptTypeId = normalized,
                    fullTypeName = fullName,
                    displayName = string.IsNullOrEmpty(displayName) ? fullName : displayName,
                    sourcePath = sourcePath ?? string.Empty,
                    sourceAssetId = string.Empty,
                });
            }

            // 生成 registry 優先（sourcePath 付き）→ 無ければ reflection
            ScriptTypeDescriptor[]? generated = TryReadGeneratedManifest(assembly);
            if (generated != null) {
                foreach (ScriptTypeDescriptor descriptor in generated) {
                    AddScript(descriptor.ScriptTypeId, descriptor.FullTypeName, descriptor.DisplayName, descriptor.SourcePath);
                }
            } else {
                foreach (Type type in assembly.GetTypes()) {
                    if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type)) {
                        continue;
                    }
                    if (type.GetConstructor(Type.EmptyTypes) == null) {
                        continue;
                    }
                    string fullName = type.FullName ?? type.Name;
                    (string guid, _) = ResolveGuidFromAttribute(type, fullName);
                    AddScript(guid, fullName, type.Name, string.Empty);
                }
            }

            root.scripts.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));
            return (root, valid, schemaJson);
        }
        catch (Exception ex) {
            NativeApi.WriteLog(2, $"manifest: failed to inspect assembly '{dllPath}'\n{ex}");
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
        int reloadId = ++reloadCounter;
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
            NativeApi.WriteLog(1,
                $"[ALC leak] GameScripts load context was not collected. reloadId={reloadId} " +
                $"context=\"{contextName}\" attempts={attempts}. " +
                "A static field, running Task/Timer, or unmanaged callback may still reference the old assembly. " +
                "Register disposables / unsubscribes via ScriptRuntimeLifetime so they are released on reload.");
        } else {

            lastAlcUnloadStatus = 1; // UnloadSucceeded
            NativeApi.WriteLog(0, $"GameScripts load context unloaded. reloadId={reloadId} attempts={attempts}");
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
    private static bool TryGetEntry(string? scriptTypeId, out ScriptTypeEntry entry) {

        entry = null!;
        string? normalized = NormalizeGuid(scriptTypeId);
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

    // full type name から決定的な移行用 fallback GUID を作る（明示 [ScriptTypeId] 推奨）
    private static string DeterministicGuid(string fullTypeName) {

        using System.Security.Cryptography.MD5 md5 = System.Security.Cryptography.MD5.Create();
        byte[] hash = md5.ComputeHash(Encoding.UTF8.GetBytes("NEMEngine.ScriptType:" + fullTypeName));
        return new Guid(hash).ToString("D");
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
            slot.inUse = false;
            RetireOrRecycle(slot, (uint)i);
        }
    }

    // authoring の field 値（{ "<fieldGuid>": <value> } 形式）を instance へ適用する。
    // Stable Field GUID で fieldMap を引くので field 名変更に強い。未知 GUID は skip（C++側で unresolved 保持）。
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
            if (entry.fieldMap.TryGetValue(prop.Name, out FieldInfo? field)) {
                SetFieldFromElement(script, field, prop.Value);
            }
        }
    }

    // JsonElement を field 型へ復元して設定する。reference 型は専用 converter 経由。
    private static void SetFieldFromElement(ScriptBehaviour script, FieldInfo field, JsonElement value) {

        try {
            object? deserialized = value.Deserialize(field.FieldType, jsonOptions);
            field.SetValue(script, deserialized);
        }
        catch (Exception ex) {
            // 型不一致などはその field だけ skip し、他 field と instance を壊さない
            NativeApi.WriteLog(1, $"Failed to apply field '{field.Name}' on '{script.GetType().FullName}': {ex.Message}");
        }
    }

    // 単一 field（value のみの JSON）を runtime instance へ設定する
    private static void ApplyFieldValue(ScriptBehaviour script, FieldInfo field, string? valueJson) {

        if (valueJson == null) {
            return;
        }
        using JsonDocument document = JsonDocument.Parse(valueJson);
        SetFieldFromElement(script, field, document.RootElement);
    }

    // runtime instance の現在値を { "<fieldGuid>": <value> } で返す（runtime Inspector 用）
    private static string BuildRuntimeStateJson(ScriptBehaviour script) {

        if (!typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            return "{}";
        }
        var obj = new JsonObject();
        foreach (KeyValuePair<string, FieldInfo> kv in entry.fieldMap) {
            try {
                object? value = kv.Value.GetValue(script);
                obj[kv.Key] = JsonSerializer.SerializeToNode(value, kv.Value.FieldType, jsonOptions);
            }
            catch {
                // 取得できない field は省略する
            }
        }
        return obj.ToJsonString();
    }

    private static bool TryGetFieldInfo(Type type, string fieldId, out FieldInfo field) {

        field = null!;
        if (typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) &&
            entry.fieldMap.TryGetValue(fieldId, out FieldInfo? info)) {
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

    // public field または [SerializeField] 付き field を継承込みで列挙する（reflection fallback 用）
    private static IEnumerable<FieldInfo> EnumerateSerializedFields(Type type) {

        var chain = new List<Type>();
        for (Type? t = type; t != null && t != typeof(ScriptBehaviour) && t != typeof(object); t = t.BaseType) {
            chain.Add(t);
        }
        chain.Reverse();

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        foreach (Type t in chain) {
            foreach (FieldInfo field in t.GetFields(flags)) {
                if (field.IsStatic || field.IsInitOnly || field.IsLiteral) {
                    continue;
                }
                bool serialize = field.GetCustomAttribute<SerializeFieldAttribute>() != null;
                if (!field.IsPublic && !serialize) {
                    continue;
                }
                yield return field;
            }
        }
    }

    // reflection fallback 用の kind 名（属性・element は反映しない degraded schema）
    private static string ReflectionKindName(Type type) {

        if (type.IsEnum) return "Enum";
        if (type.IsArray) return "Array";
        if (type.IsGenericType) {
            Type def = type.GetGenericTypeDefinition();
            if (def == typeof(List<>)) return "List";
            if (def == typeof(Nullable<>)) return "Nullable";
            if (def == typeof(AssetRef<>)) return "AssetRef";
            if (def == typeof(ScriptRef<>)) return "ScriptRef";
        }
        if (type == typeof(bool)) return "Bool";
        if (type == typeof(byte)) return "Byte";
        if (type == typeof(sbyte)) return "SByte";
        if (type == typeof(short)) return "Short";
        if (type == typeof(ushort)) return "UShort";
        if (type == typeof(int)) return "Int";
        if (type == typeof(uint)) return "UInt";
        if (type == typeof(long)) return "Long";
        if (type == typeof(ulong)) return "ULong";
        if (type == typeof(float)) return "Float";
        if (type == typeof(double)) return "Double";
        if (type == typeof(string)) return "String";
        if (type == typeof(Vector2)) return "Vector2";
        if (type == typeof(Vector3)) return "Vector3";
        if (type == typeof(Vector4)) return "Vector4";
        if (type == typeof(Quaternion)) return "Quaternion";
        if (type == typeof(Color3)) return "Color3";
        if (type == typeof(Color4)) return "Color4";
        if (type == typeof(EntityRef)) return "EntityRef";
        return "Unsupported";
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
            NativeApi.WriteLog(1, $"Failed to create default instance for '{type.FullName}': {ex.Message}");
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
            return JsonSerializer.Serialize(value, field.FieldType, jsonOptions);
        }
        catch {
            return "null";
        }
    }

    // 二段階 blob API の出力。buffer 不足は BufferTooSmall。written に必要 byte 数を返す
    private static ManagedStatus WriteUtf8Blob(string text, byte* buffer, int capacity, int* written) {

        byte[] bytes = Encoding.UTF8.GetBytes(text);
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
    public fixed byte scriptTypeId[40];
    // 完全修飾型名
    public fixed byte fullTypeName[256];
    // 表示名
    public fixed byte displayName[128];
    // 定義元 .cs パス（drag&drop の source 照合用）
    public fixed byte sourcePath[260];
    // [ScriptTypeId] が明示されていたか
    public int hasExplicitId;
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
