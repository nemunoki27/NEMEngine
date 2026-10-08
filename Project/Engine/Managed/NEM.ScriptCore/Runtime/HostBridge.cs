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

    internal static T? FindScriptOfType<T>() where T : MonoBehaviour => instances.FindScriptOfType<T>();
    internal static T[] FindScriptsOfType<T>() where T : MonoBehaviour => instances.FindScriptsOfType<T>();
    internal static MonoBehaviour? FindScriptOfTypeByType(Type type) => instances.FindScriptOfTypeByType(type);
    internal static List<MonoBehaviour> FindScriptsOfTypeByType(Type type) => instances.FindScriptsOfTypeByType(type);

    // Nativeの版と配置を検証して接続する
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
            if (header.structSize != (uint)sizeof(NativeAPITable) ||
                header.bindingFingerprint != NativeAPITable.BindingFingerprint) {
                return (int)ManagedStatus.ABIMismatch;
            }
            if ((header.capabilities & ManagedABI.RequiredCapabilities) != ManagedABI.RequiredCapabilities) {
                return (int)ManagedStatus.ABIMismatch;
            }
            if (!GeneratedABILayout.IsValid()) {
                return (int)ManagedStatus.ABIMismatch;
            }

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

    // ゲームAssemblyの型と保存情報を登録する
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

    // ゲームAssemblyの個体と型を解放する
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

        try {
            // SceneとApplicationの完了通知を送る
            SceneManager.PumpEvents();
            if (!NativeApplicationAPI.ReadUpdateInterrupted()) {
                Application.PumpEvents();
            }
            return (int)ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException(nameof(PumpSceneEvents), ex);
            return (int)ManagedStatus.ScriptException;
        }
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

    // 登録済みScript型の件数を返す
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

    // 登録済みScript型の情報をコピーする
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

    // 保存schemaの必要容量を返す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetScriptSchemaJsonSize(byte* scriptTypeID, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetScriptSchemaJsonSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outSize = session.registry.TryGetEntry(ManagedUTF8Transfer.PtrToString(scriptTypeID), out ScriptTypeEntry entry)
                ? Encoding.UTF8.GetByteCount(entry.schemaJSON) : 0;
            return ManagedStatus.Ok;
        });
    }

    // 保存schemaを指定先へコピーする
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyScriptSchemaJson(byte* scriptTypeID, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyScriptSchemaJson), () => {
            if (!session.registry.TryGetEntry(ManagedUTF8Transfer.PtrToString(scriptTypeID), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }
            return ManagedUTF8Transfer.WriteUtf8Blob(entry.schemaJSON, buffer, capacity, written);
        });
    }

    // 実行中のField編集と参照解決を行う
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
            // 参照解決中に解放された個体の結果を公開しない
            if (!instances.TryResolveSlot(handle, out MonoBehaviour current) || !ReferenceEquals(script, current)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            instances.slots[(int)handle.index].runtimeStateSnapshot = null;
            return ManagedStatus.Ok;
        });
    }

    // 所有GameObjectへScript個体を登録する
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

    // 別AssemblyからManifestを生成する
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

    // 実行中のScriptへ保存値を適用する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetSerializedFields(NativeScriptInstanceHandle handle, byte* serializedJson) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(SetSerializedFields), () => {

            // Inspectorの保存値を実行中のScriptへ反映する
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            session.codec.ApplySerializedFields(script, ManagedUTF8Transfer.PtrToString(serializedJson));
            return ManagedStatus.Ok;
        });
    }

    // 保留中の保存参照を再解決する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int FlushPendingReferences() {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(FlushPendingReferences), () => {
            session.codec.FlushPendingReferenceFields(retryUnresolved: true);
            return ManagedStatus.Ok;
        });
    }

    // Scriptの参照と個体slotを解放する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyInstance(NativeScriptInstanceHandle handle) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(DestroyInstance), () => {
            if (instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                session.codec.ReleaseInstance(script);
            }
            instances.ReleaseSlot(handle);
            return ManagedStatus.Ok;
        });
    }

    // ScriptのAwakeを呼び出す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAwake(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.Awake),
            static script => script.callbacks.Awake?.Invoke(script));
    }

    // ScriptのStartを呼び出す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeStart(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.Start),
            static script => script.callbacks.Start?.Invoke(script));
    }

    // Scriptの有効化を通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnEnable(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.OnEnable),
            static script => script.callbacks.OnEnable?.Invoke(script));
    }

    // Scriptの無効化を通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDisable(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.OnDisable),
            static script => script.callbacks.OnDisable?.Invoke(script));
    }

    // Scriptの破棄を通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeOnDestroy(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.OnDestroy),
            static script => script.callbacks.OnDestroy?.Invoke(script));
    }

    // Scriptの固定更新を呼び出す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeFixedUpdate(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.FixedUpdate),
            static script => script.callbacks.FixedUpdate?.Invoke(script));
    }

    // Scriptの更新を呼び出す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeUpdate(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.Update),
            static script => script.callbacks.Update?.Invoke(script));
    }

    // Scriptの後段更新を呼び出す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeLateUpdate(NativeScriptInstanceHandle handle) {

        return (int)invocations.Invoke(handle, nameof(ScriptCallbacks.LateUpdate),
            static script => script.callbacks.LateUpdate?.Invoke(script));
    }

    // 衝突情報をクロージャへ取り込まず通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionEnter(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        return (int)invocations.InvokeCollision(handle, collision, ScriptCollisionCallback.Enter);
    }

    // 接触継続をScriptへ通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionStay(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        return (int)invocations.InvokeCollision(handle, collision, ScriptCollisionCallback.Stay);
    }

    // 接触終了をScriptへ通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeCollisionExit(NativeScriptInstanceHandle handle, NativeCollisionEvent collision) {

        return (int)invocations.InvokeCollision(handle, collision, ScriptCollisionCallback.Exit);
    }

    // AnimationのEventをScriptへ通知する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int InvokeAnimationEvent(NativeScriptInstanceHandle handle,
        byte* name, float floatParam, int intParam, byte* stringParam) {

        return (int)invocations.InvokeAnimationEvent(handle, name, floatParam, intParam, stringParam);
    }

    // Script計測の所有個体を設定する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ConfigureScriptProfiler(byte* typeName, NativeEntity gameObject, ulong slotID) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(ConfigureScriptProfiler), () => {
            ScriptProfiler.Configure(ManagedUTF8Transfer.PtrToString(typeName) ?? string.Empty, gameObject, slotID);
            return ManagedStatus.Ok;
        });
    }

    // 所有Entityと代入可能な型でScriptを解決する
    internal static T? FindScriptAs<T>(NativeEntity owner) where T : class => instances.FindScriptAs<T>(owner);

    // 所有Entityの代入可能なScriptを追加
    internal static void AppendScriptsAs<T>(NativeEntity owner, List<T> result) where T : class =>
        instances.AppendScriptsAs(owner, result);

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

        return session.lastALCUnloadStatus;
    }

    // Native転送先の固定容量
    private const int MaxNameBytes = 128;
    private const int ScriptTypeIDBytes = 40;
    private const int FullTypeNameBytes = 256;
    private const int SourcePathBytes = 260;

    private static readonly ScriptInstanceStore instances = new();
    private static readonly ManagedAssemblySession session = new(instances);
    private static readonly ScriptCallbackInvoker invocations = new(instances, session);

}
