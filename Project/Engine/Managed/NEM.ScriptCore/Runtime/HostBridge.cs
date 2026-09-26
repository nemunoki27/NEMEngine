using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using System.Text;





namespace NEMEngine;

//============================================================================
//	HostBridge class
//============================================================================
public static unsafe class HostBridge {

    private const int MaxNameBytes = 128;
    private const int ScriptTypeIDBytes = 40;
    private const int FullTypeNameBytes = 256;
    private const int SourcePathBytes = 260;

    private static readonly ScriptInstanceStore instances = new();
    private static readonly ManagedAssemblySession session = new(instances);

    internal static T? FindScriptOfType<T>() where T : MonoBehaviour => instances.FindScriptOfType<T>();
    internal static T[] FindScriptsOfType<T>() where T : MonoBehaviour => instances.FindScriptsOfType<T>();
    internal static MonoBehaviour? FindScriptOfTypeByType(Type type) => instances.FindScriptOfTypeByType(type);
    internal static List<MonoBehaviour> FindScriptsOfTypeByType(Type type) => instances.FindScriptsOfTypeByType(type);

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
            if (header.structSize != (uint)sizeof(NativeAPITable) || header.bindingFingerprint != NativeAPITable.BindingFingerprint) {
                return (int)ManagedStatus.AbiMismatch;
            }
            if ((header.capabilities & ManagedAbi.RequiredCapabilities) != ManagedAbi.RequiredCapabilities) {
                return (int)ManagedStatus.AbiMismatch;
            }
            if (!GeneratedABILayout.IsValid()) { return (int)ManagedStatus.AbiMismatch; }

            // 公開APIの接続不足を起動時に検出する
            if (!callbacks->HasRequiredCallbacks()) {
                return (int)ManagedStatus.InvalidArgument;
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

        return (int)ScriptInvocationDiagnostics.Guard(nameof(LoadGameAssembly), () => {

            // ゲーム側DLLをロードして、MonoBehaviour派生型を再収集する
            string? path = ManagedUTF8Transfer.PtrToString(assemblyPath);
            if (string.IsNullOrEmpty(path) || !File.Exists(path)) {
                return ManagedStatus.InvalidArgument;
            }

            return session.Load(path);
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int UnloadGameAssembly() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(UnloadGameAssembly), () => {
            session.ReleaseGameAssembly(collect: true);
            return ManagedStatus.Ok;
        });
    }

    // BehaviorSystem が SynchronizeLifecycle の Pass4(OnEnable 後・Start 前)で呼ぶ。
    // Scene の load/unload 完了を検出して SceneManager の SceneLoaded/SceneUnloaded を発火する。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int PumpSceneEvents() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(PumpSceneEvents), () => {
            // Pass4(OnEnable 後・Start 前)の per-frame pump。Scene / Application イベントを発火する。
            SceneManager.PumpEvents();
            Application.PumpEvents();
            return ManagedStatus.Ok;
        });
    }

    // application shutdown 前に native から一度だけ呼ばれ、Application.Quitting を発火する。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int RaiseApplicationQuitting() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(RaiseApplicationQuitting), () => {
            Application.RaiseQuitting();
            return ManagedStatus.Ok;
        });
    }

    // BehaviorSystem の各 phase 末から呼ばれる per-frame tick。phase: 0=Update, 1=FixedUpdate, 2=EndOfFrame。
    // Update で 遅延イベント flush + Timer tick + Coroutine(Update)、FixedUpdate で Coroutine(Fixed)、EndOfFrame で Coroutine(EndOfFrame)。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int TickFrame(int phase) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(TickFrame), () => {
            ScriptServiceLifetime.TickFrame(phase);
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptTypeCount(int* outCount) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetScriptTypeCount), () => {
            if (outCount == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outCount = session.registry.scriptTypeEntries.Count;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptTypeInfo(int index, NativeScriptTypeInfo* outInfo) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyScriptTypeInfo), () => {

            if (index < 0 || session.registry.scriptTypeEntries.Count <= index || outInfo == null) {
                return ManagedStatus.InvalidArgument;
            }
            ScriptTypeEntry entry = session.registry.scriptTypeEntries[index];

            // native registry へ Stable GUID と表示用情報・source path を渡す
            ManagedUTF8Transfer.CopyFixed(entry.scriptTypeID, outInfo->scriptTypeID, ScriptTypeIDBytes);
            ManagedUTF8Transfer.CopyFixed(entry.fullTypeName, outInfo->fullTypeName, FullTypeNameBytes);
            ManagedUTF8Transfer.CopyFixed(entry.displayName, outInfo->displayName, MaxNameBytes);
            ManagedUTF8Transfer.CopyFixed(entry.sourcePath, outInfo->sourcePath, SourcePathBytes);
            outInfo->hasExplicitID = entry.hasExplicitID ? 1 : 0;
            outInfo->defaultExecutionOrder = entry.defaultExecutionOrder;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptSchemaJsonSize(byte* scriptTypeID, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetScriptSchemaJsonSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outSize = session.registry.TryGetEntry(ManagedUTF8Transfer.PtrToString(scriptTypeID), out ScriptTypeEntry entry)
                ? Encoding.UTF8.GetByteCount(entry.schemaJson) : 0;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptSchemaJson(byte* scriptTypeID, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyScriptSchemaJson), () => {
            if (!session.registry.TryGetEntry(ManagedUTF8Transfer.PtrToString(scriptTypeID), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }
            return ManagedUTF8Transfer.WriteUtf8Blob(entry.schemaJson, buffer, capacity, written);
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetRuntimeSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetRuntimeSerializedStateSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            *outSize = session.codec.GetRuntimeStateSnapshot(instances.slots[(int)handle.index], true).Length;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyRuntimeSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyRuntimeSerializedState), () => {
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ScriptInstanceSlot slot = instances.slots[(int)handle.index];
            byte[] snapshot = session.codec.GetRuntimeStateSnapshot(slot, false);
            ManagedStatus status = ManagedUTF8Transfer.WriteUtf8Blob(snapshot, buffer, capacity, written);
            if (status == ManagedStatus.Ok) {
                slot.runtimeStateSnapshot = null;
            }
            return status;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetRuntimeSerializedField(NativeScriptInstanceHandle handle, byte* fieldID, byte* valueJson) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(SetRuntimeSerializedField), () => {

            // Play中 runtime Inspector の単一 field 編集。live instance のみへ反映する
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            string? guid = ManagedUTF8Transfer.PtrToString(fieldID);
            if (string.IsNullOrEmpty(guid) || !session.registry.TryGetFieldInfo(script.GetType(), guid!, out FieldInfo field)) {
                return ManagedStatus.InvalidArgument;
            }
            session.codec.ApplyFieldValue(script, field, ManagedUTF8Transfer.PtrToString(valueJson));
            instances.slots[(int)handle.index].runtimeStateSnapshot = null;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CreateInstance(byte* scriptTypeID, NativeEntity gameObject, byte* serializedJson,
        ulong scriptSlotID, NativeScriptInstanceHandle* outHandle) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CreateInstance), () => {

            if (outHandle == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outHandle = NativeScriptInstanceHandle.Null;

            // Stable GUID から型を解決し、ECSのGameObject参照を持つMonoBehaviourを生成する
            if (!session.registry.TryGetEntry(ManagedUTF8Transfer.PtrToString(scriptTypeID), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }

            if (Activator.CreateInstance(entry.type) is not MonoBehaviour script) {
                return ManagedStatus.InternalError;
            }

            // serialized field 適用 / Awake より前に GameObject と scriptSlotID を設定する
            script.gameObject = GameObject.FromNative(gameObject) ?? throw new ArgumentException("所有GameObjectが無効です");
            script.scriptSlotID = scriptSlotID;
            script.callbacks = entry.callbacks;
            // 保存callbackからも個体を確認できるよう先に登録する
            NativeScriptInstanceHandle handle = instances.AllocateSlot(script);
            try {
                session.codec.ApplySerializedFields(script, ManagedUTF8Transfer.PtrToString(serializedJson));
            } catch {
                session.codec.ReleaseInstance(script);
                instances.ReleaseSlot(handle);
                throw;
            }
            *outHandle = handle;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GenerateScriptManifest(byte* assemblyPath, byte* manifestOutputPath) {

        // build/reload 時のみ。対象 DLL を一時 collectible ALC（default ALC 非汚染）で反射して
        // Script Type GUID / fullTypeName / sourcePath を集め、検証して manifest JSON を出力する。
        // 現在ロード中の assembly（gameLoadContext）には触れない＝失敗しても現行 DLL を unload しない。
        return (int)ScriptInvocationDiagnostics.Guard(nameof(GenerateScriptManifest), () => {

            string? dll = ManagedUTF8Transfer.PtrToString(assemblyPath);
            string? outPath = ManagedUTF8Transfer.PtrToString(manifestOutputPath);
            if (string.IsNullOrEmpty(dll) || !File.Exists(dll) || string.IsNullOrEmpty(outPath)) {
                return ManagedStatus.InvalidArgument;
            }
            return ScriptManifestWriter.GenerateManifestIsolated(dll!, outPath!);
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetSerializedFields(NativeScriptInstanceHandle handle, byte* serializedJson) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(SetSerializedFields), () => {

            // Play中にInspectorで変更された保存値を、既存のC#インスタンスへ再適用する
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            session.codec.ApplySerializedFields(script, ManagedUTF8Transfer.PtrToString(serializedJson));
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int FlushPendingReferences() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(FlushPendingReferences), () => {
            session.codec.FlushPendingReferenceFields();
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyInstance(NativeScriptInstanceHandle handle) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(DestroyInstance), () => {
            if (instances.TryResolveSlot(handle, out MonoBehaviour script)) { session.codec.ReleaseInstance(script); }
            instances.ReleaseSlot(handle);
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAwake(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.Awake), static script => script.callbacks.Awake?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeStart(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.Start), static script => script.callbacks.Start?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnEnable(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.OnEnable), static script => script.callbacks.OnEnable?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDisable(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.OnDisable), static script => script.callbacks.OnDisable?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDestroy(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.OnDestroy), static script => script.callbacks.OnDestroy?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeFixedUpdate(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.FixedUpdate), static script => script.callbacks.FixedUpdate?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeUpdate(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.Update), static script => script.callbacks.Update?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeLateUpdate(NativeScriptInstanceHandle handle) {
        return (int)GuardInstance(handle, nameof(ScriptCallbacks.LateUpdate), static script => script.callbacks.LateUpdate?.Invoke(script));
    }

    // Collision系はcollisionをキャプチャするとクロージャがヒープ確保されるため、
    // 高頻度callbackでの確保を避けてGuardInstanceを使わず明示的にguardする
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionEnter(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        try {
            session.codec.FlushPendingReferenceFields();
            script.callbacks.OnCollisionEnter?.Invoke(script, new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, nameof(ScriptCallbacks.OnCollisionEnter), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionStay(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        try {
            session.codec.FlushPendingReferenceFields();
            script.callbacks.OnCollisionStay?.Invoke(script, new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, nameof(ScriptCallbacks.OnCollisionStay), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionExit(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        try {
            session.codec.FlushPendingReferenceFields();
            script.callbacks.OnCollisionExit?.Invoke(script, new Collision(collision));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, nameof(ScriptCallbacks.OnCollisionExit), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAnimationEvent(NativeScriptInstanceHandle handle, byte* name, float floatParam, int intParam, byte* stringParam) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return (int)ManagedStatus.InvalidInstanceHandle;
        }
        try {
            session.codec.FlushPendingReferenceFields();
            script.callbacks.OnAnimationEvent?.Invoke(script, new AnimationEvent(ManagedUTF8Transfer.PtrToString(name) ?? string.Empty, floatParam, intParam, ManagedUTF8Transfer.PtrToString(stringParam) ?? string.Empty));
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, nameof(ScriptCallbacks.OnAnimationEvent), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ConfigureScriptProfiler(byte* typeName, NativeEntity gameObject, ulong slotID) {
        return (int)ScriptInvocationDiagnostics.Guard(nameof(ConfigureScriptProfiler), () => {
            ScriptProfiler.Configure(Marshal.PtrToStringUTF8((nint)typeName) ?? string.Empty, gameObject, slotID);
            return ManagedStatus.Ok;
        });
    }

    // script callback専用ラッパー。例外時は対象instanceのみScriptExceptionを返し、診断情報を残す
    private static ManagedStatus GuardInstance(NativeScriptInstanceHandle handle, string callbackName, Action<MonoBehaviour> body) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }

        try {
            // 保留値と保存callbackも例外境界内で処理する
            session.codec.FlushPendingReferenceFields();
            body(script);
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, callbackName, ex);
            return ManagedStatus.ScriptException;
        }
    }

    // 同 GameObject 上の T 型スクリプト instance を引く（GameObject.GetComponent<T> から呼ぶ）。
    // 所有Entityと代入可能な型で解決する
    internal static T? FindScriptAs<T>(NativeEntity owner) where T : class => instances.FindScriptAs<T>(owner);
    internal static void AppendScriptsAs<T>(NativeEntity owner, List<T> result) where T : class => instances.AppendScriptsAs(owner, result);

    // Stable GUID 指定で同 GameObject 上の script instance を引く（参照フィールドの復元用）
    internal static MonoBehaviour? FindScriptByGuid(NativeEntity owner, string scriptTypeID) {

        if (string.IsNullOrEmpty(scriptTypeID)) {
            return null;
        }
        NativeScriptInstanceHandle handle = NativeEntityAPI.FindScriptInstance(owner, scriptTypeID);
        return instances.TryResolveSlot(handle, out MonoBehaviour script) ? script : null;
    }

    // 型の Stable Script Type GUID を返す。未登録型は null（参照フィールドの保存用）
    internal static string? GetScriptTypeGuid(Type type) {
        return session.registry.typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry.scriptTypeID : null;
    }

    // AddComponent<Script>用に owner GameObject へ T を runtime attach し、生成した managed instance を返す。未登録/失敗は null
    internal static T? AttachScriptAs<T>(NativeEntity owner) where T : class {

        if (!session.registry.typeToEntry.TryGetValue(typeof(T), out ScriptTypeEntry? entry)) {
            return null;
        }
        return NativeEntityAPI.TryAttachScript(owner, entry.scriptTypeID) ? FindScriptAs<T>(owner) : null;
    }

    // 直近の collectible ALC unload の typed status を返す（0=Unknown, 1=UnloadSucceeded, 2=LeakSuspected）。
    // reload/unload path でのみ更新され、gameplay frame hot path に GC probe を入れない。
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetLastAlcUnloadStatus() {

        return session.lastAlcUnloadStatus;
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

    // コールバックを受け取るGameObjectと相手GameObject
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
