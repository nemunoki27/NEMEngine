using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using System.Text;

namespace NEMEngine;

//============================================================================
//	HostBridge class
//	Nativeの接続入口とManagedサービスを結ぶ
//============================================================================
public static unsafe partial class HostBridge {

    private const int MaxNameBytes = 128;
    private const int ScriptTypeIDBytes = 40;
    private const int FullTypeNameBytes = 256;
    private const int SourcePathBytes = 260;

    private static readonly ScriptInstanceStore instances = new();
    private static readonly ManagedAssemblySession session = new(instances);
    private static readonly ScriptCallbackInvoker invocations = new(instances, session);

    internal static T? FindScriptOfType<T>() where T : MonoBehaviour => instances.FindScriptOfType<T>();
    internal static T[] FindScriptsOfType<T>() where T : MonoBehaviour => instances.FindScriptsOfType<T>();
    internal static MonoBehaviour? FindScriptOfTypeByType(Type type) => instances.FindScriptOfTypeByType(type);
    internal static List<MonoBehaviour> FindScriptsOfTypeByType(Type type) => instances.FindScriptsOfTypeByType(type);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InitializeNativeAPI(NativeAPITable* callbacks) {

        // 未接続のログを使わず例外を境界内で受け止める
        try {
            if (callbacks == null) {
                return (int)ManagedStatus.InvalidArgument;
            }

            // 版と配置と必要機能を照合してから接続する
            ManagedABIHeader header = callbacks->header;
            if (header.abiVersion != ManagedABI.Version) {
                return (int)ManagedStatus.ABIMismatch;
            }
            if (header.structSize != (uint)sizeof(NativeAPITable) || header.bindingFingerprint != NativeAPITable.BindingFingerprint) {
                return (int)ManagedStatus.ABIMismatch;
            }
            if ((header.capabilities & ManagedABI.RequiredCapabilities) != ManagedABI.RequiredCapabilities) {
                return (int)ManagedStatus.ABIMismatch;
            }
            if (!GeneratedABILayout.IsValid()) { return (int)ManagedStatus.ABIMismatch; }

            // 公開APIの接続不足を起動時に検出する
            if (!callbacks->HasRequiredCallbacks()) {
                return (int)ManagedStatus.InvalidArgument;
            }

            // 検証済みNative関数をScriptCoreへ接続する
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

            // AssemblyからScript型を登録する
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

    // OnEnable後とStart前にScene通知を処理する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int PumpSceneEvents() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(PumpSceneEvents), () => {
            // SceneとApplicationの完了通知を送る
            SceneManager.PumpEvents();
            Application.PumpEvents();
            return ManagedStatus.Ok;
        });
    }

    // Applicationの終了通知を送る
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int RaiseApplicationQuitting() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(RaiseApplicationQuitting), () => {
            Application.RaiseQuitting();
            return ManagedStatus.Ok;
        });
    }

    // 更新phaseに対応する予約処理を進める
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int TickFrame(int phase) {
        try {
            ScriptServiceLifetime.TickFrame(phase);
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            // サービス例外は次の安全地点でEditorをPauseさせる
            ScriptInvocationDiagnostics.ReportRuntimeServiceException(nameof(TickFrame), ex);
            return (int)ManagedStatus.ScriptException;
        }
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

            // 登録先へ型IDと表示情報とソース位置を渡す
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

            // 実行中のScriptだけにField編集を反映する
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

            // 型IDから所有GameObjectに属するScriptを生成する
            if (!session.registry.TryGetEntry(ManagedUTF8Transfer.PtrToString(scriptTypeID), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }

            if (Activator.CreateInstance(entry.type) is not MonoBehaviour script) {
                return ManagedStatus.InternalError;
            }

            // 保存値の適用前に所有GameObjectとslotを接続する
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

        // 実行中のAssemblyに触れずManifestを生成する
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
            session.codec.FlushPendingReferenceFields(retryUnresolved: true);
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
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.Awake), static script => script.callbacks.Awake?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeStart(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.Start), static script => script.callbacks.Start?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnEnable(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.OnEnable), static script => script.callbacks.OnEnable?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDisable(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.OnDisable), static script => script.callbacks.OnDisable?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDestroy(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.OnDestroy), static script => script.callbacks.OnDestroy?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeFixedUpdate(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.FixedUpdate), static script => script.callbacks.FixedUpdate?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeUpdate(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.Update), static script => script.callbacks.Update?.Invoke(script));
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeLateUpdate(NativeScriptInstanceHandle handle) {
        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.LateUpdate), static script => script.callbacks.LateUpdate?.Invoke(script));
    }

    // 衝突情報をクロージャへ取り込まず通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionEnter(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        return (int)invocations.InvokeCollision(handle, collision, ScriptCollisionCallback.Enter);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionStay(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        return (int)invocations.InvokeCollision(handle, collision, ScriptCollisionCallback.Stay);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionExit(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        return (int)invocations.InvokeCollision(handle, collision, ScriptCollisionCallback.Exit);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAnimationEvent(NativeScriptInstanceHandle handle, byte* name, float floatParam, int intParam, byte* stringParam) {

        return (int)invocations.InvokeAnimationEvent(handle, name, floatParam, intParam, stringParam);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ConfigureScriptProfiler(byte* typeName, NativeEntity gameObject, ulong slotID) {
        return (int)ScriptInvocationDiagnostics.Guard(nameof(ConfigureScriptProfiler), () => {
            ScriptProfiler.Configure(Marshal.PtrToStringUTF8((nint)typeName) ?? string.Empty, gameObject, slotID);
            return ManagedStatus.Ok;
        });
    }

    // 所有Entityと代入可能な型でScriptを解決する
    internal static T? FindScriptAs<T>(NativeEntity owner) where T : class => instances.FindScriptAs<T>(owner);
    internal static void AppendScriptsAs<T>(NativeEntity owner, List<T> result) where T : class => instances.AppendScriptsAs(owner, result);

    // 保存参照の型IDとslotからScriptを解決する
    internal static MonoBehaviour? FindScriptByGUID(NativeEntity owner, string scriptTypeID, ulong scriptSlotID = 0) {

        if (!session.registry.TryGetEntry(scriptTypeID, out ScriptTypeEntry entry)) {
            return null;
        }
        return instances.FindScriptByIdentity(owner, entry.type, scriptSlotID);
    }

    // 保存参照に使う登録済みScript型IDを返す
    internal static string? GetScriptTypeGUID(Type type) {
        return session.registry.typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry.scriptTypeID : null;
    }

    // 所有GameObjectへ登録済みScriptを追加する
    internal static T? AttachScriptAs<T>(NativeEntity owner) where T : class {

        if (!session.registry.typeToEntry.TryGetValue(typeof(T), out ScriptTypeEntry? entry)) {
            return null;
        }
        NativeScriptInstanceHandle handle = NativeEntityAPI.AttachScriptInstance(owner, entry.scriptTypeID);
        return instances.TryResolveSlot(handle, out MonoBehaviour script) ? script as T : null;
    }

    // Assembly解放時に確定した診断結果を返す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetLastALCUnloadStatus() {

        return session.lastAlcUnloadStatus;
    }

}

//============================================================================
//	NativeScriptTypeInfo structure
//	NativeのScript型情報と配置を揃える
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeScriptTypeInfo {

    // 正規化済みScript型ID
    public fixed byte scriptTypeID[40];
    // 完全修飾型名
    public fixed byte fullTypeName[256];
    // 表示名
    public fixed byte displayName[128];
    // 定義元のソース位置
    public fixed byte sourcePath[260];
    // Script型IDの明示指定
    public int hasExplicitID;
    // 既定の実行順序
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
