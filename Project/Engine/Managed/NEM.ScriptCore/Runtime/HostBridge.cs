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
    // ManagedScriptTypeDescriptor の固定長フィールド（C++側と一致させる）
    private const int ScriptTypeIdBytes = 40;
    private const int FullTypeNameBytes = 256;
    private const int SourcePathBytes = 260;
    // manifest schema version
    private const int ManifestSchemaVersion = 1;

    // public fieldをJSONへ含めるための共通設定
    private static readonly JsonSerializerOptions jsonOptions = new() {
        IncludeFields = true,
        // MathTypesのlength/normalizedなどは保存値ではないのでJSON化しない
        IgnoreReadOnlyProperties = true
    };

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
        internal string[] formerlyKnown = Array.Empty<string>();
    }

    // 現在ロード中のゲームDLLの ScriptBehaviour 型一覧（native へは CopyScriptTypeInfo で順次渡す）
    private static readonly List<ScriptTypeEntry> scriptTypeEntries = new();
    // Stable GUID -> entry（instance 作成・field 取得の解決に使う）
    private static readonly Dictionary<string, ScriptTypeEntry> guidToEntry = new(StringComparer.Ordinal);
    // Stable GUID ごとのInspector表示フィールド情報キャッシュ
    private static readonly Dictionary<string, List<SerializedFieldInfo>> fieldCache = new();

    // ゲーム側スクリプトDLL
    private static Assembly? gameAssembly;
    // ゲーム側DLLをアンロード可能にする専用LoadContext
    private static GameScriptLoadContext? gameLoadContext;
    // reload 診断用の連番（ALC unload ログに使う）
    private static int reloadCounter = 0;

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
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetSerializedFieldCount(byte* scriptTypeId, int* outCount) {

        return (int)Guard(nameof(GetSerializedFieldCount), () => {
            if (outCount == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outCount = TryGetEntry(PtrToString(scriptTypeId), out ScriptTypeEntry entry)
                ? GetSerializedFields(entry.type).Count : 0;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopySerializedFieldInfo(byte* scriptTypeId, int index, NativeSerializedFieldInfo* outInfo) {

        return (int)Guard(nameof(CopySerializedFieldInfo), () => {

            // C++ Inspectorが描画できるよう、フィールド名・型・初期値だけを固定長ABIで返す
            if (outInfo == null || !TryGetEntry(PtrToString(scriptTypeId), out ScriptTypeEntry entry)) {
                return ManagedStatus.InvalidArgument;
            }

            List<SerializedFieldInfo> fields = GetSerializedFields(entry.type);
            if (index < 0 || fields.Count <= index) {
                return ManagedStatus.InvalidArgument;
            }

            SerializedFieldInfo field = fields[index];

            // enumはC++側のManagedSerializedFieldKindと同じ値で扱う
            outInfo->kind = (int)field.kind;
            outInfo->isPublic = field.isPublic ? 1 : 0;

            // 文字列はC++側の固定長char配列へコピーする
            CopyFixed(field.name, outInfo->name, MaxNameBytes);
            CopyFixed(field.displayName, outInfo->displayName, MaxNameBytes);
            CopyFixed(field.defaultValueJson, outInfo->defaultValueJson, MaxJsonBytes);
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CreateInstance(byte* scriptTypeId, NativeEntity entity, byte* serializedJson, NativeScriptInstanceHandle* outHandle) {

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

            script.entity = new Entity(entity);
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

        // 07で導入予定のcoroutine/timer/tracked disposableのcancelはこの位置で行う（現状は対象システム未実装）
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
        // (script type stable IDとscene local IDは後続タスクで導入予定のため、ここではfull type nameを使う)
        Entity owner = script.entity;
        string entityHandle = $"{owner.native.index}:{owner.native.generation}";
        string entityName = owner.isValid ? owner.name : string.Empty;

        NativeApi.WriteLog(2,
            $"[ScriptException] callback={callbackName} type={typeName} entity={entityHandle} name=\"{entityName}\"\n{ex}");
    }

    private static void RebuildScriptTypes() {

        scriptTypeEntries.Clear();
        guidToEntry.Clear();
        fieldCache.Clear();

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

        if (scriptTypeEntries.Count == 0) {
            NativeApi.WriteLog(1, "GameScripts loaded, but no ScriptBehaviour types were found.");
        }
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

        var entry = new ScriptTypeEntry {
            scriptTypeId = normalized,
            type = type,
            fullTypeName = fullName,
            displayName = string.IsNullOrEmpty(displayName) ? type.Name : displayName,
            sourcePath = sourcePath ?? string.Empty,
            hasExplicitId = hasExplicitId,
            formerlyKnown = formerlyKnown ?? Array.Empty<string>(),
        };
        scriptTypeEntries.Add(entry);
        guidToEntry[normalized] = entry;
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

        // 一時 collectible ALC で対象 DLL を反射して manifest を組む（現行 gameLoadContext には触れない）
        (ManifestRoot root, bool valid) = LoadAndCollectManifest(dllPath, assemblyName);

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
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            NativeApi.WriteLog(2, $"Failed to write script manifest: {outPath}\n{ex}");
            return ManagedStatus.SerializationError;
        }
    }

    // 一時 ALC へ DLL をロードして manifest 内容を収集し、ロード解除する。
    // strong reference を本メソッドのフレームへ閉じ込め、return 後に回収可能にする。
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (ManifestRoot root, bool valid) LoadAndCollectManifest(string dllPath, string assemblyName) {

        var loadContext = new GameScriptLoadContext(dllPath);
        try {
            Assembly assembly = loadContext.LoadFromAssemblyPath(dllPath);
            var root = new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName };
            var seenGuids = new HashSet<string>(StringComparer.Ordinal);
            bool valid = true;

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
            return (root, valid);
        }
        catch (Exception ex) {
            NativeApi.WriteLog(2, $"manifest: failed to inspect assembly '{dllPath}'\n{ex}");
            return (new ManifestRoot { schemaVersion = ManifestSchemaVersion, assemblyName = assemblyName }, false);
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
        fieldCache.Clear();
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
            NativeApi.WriteLog(1,
                $"[ALC leak] GameScripts load context was not collected. reloadId={reloadId} " +
                $"context=\"{contextName}\" attempts={attempts}. " +
                "A static field, running Task/Timer, or unmanaged callback may still reference the old assembly. " +
                "Register disposables / unsubscribes via ScriptRuntimeLifetime so they are released on reload.");
        } else {

            NativeApi.WriteLog(0, $"GameScripts load context unloaded. reloadId={reloadId} attempts={attempts}");
        }
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

    private static List<SerializedFieldInfo> GetSerializedFields(Type type) {

        // Unityと同じ方針で、public fieldまたは[SerializeField]付きfieldだけをInspector対象にする。
        // reload ごとに fieldCache はクリアされるため、セッション内で一意な FullName をキーにする
        string cacheKey = type.FullName ?? type.Name;
        if (fieldCache.TryGetValue(cacheKey, out List<SerializedFieldInfo>? cached)) {
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
                SerializeValue(defaultValue, field.FieldType),
                field,
                field.FieldType
            ));
        }

        // 同じ型の問い合わせが多いため、反射結果はキャッシュする
        fieldCache[cacheKey] = fields;
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
            // MathTypes.csのstructはIncludeFields=trueで直接復元できる
            object? value = valueElement.Deserialize(field.fieldType, jsonOptions);
            field.fieldInfo.SetValue(script, value);
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
        string defaultValueJson,
        // 値適用時に使うFieldInfo
        FieldInfo fieldInfo,
        // 値適用時に使うFieldType
        Type fieldType
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
