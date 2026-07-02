namespace NEMEngine;

// シーンの読み込みモード。Unity の LoadSceneMode 相当。
public enum LoadSceneMode {

    // 現在の scene を全て unload してから読み込み、新 scene を active にする（完全切り替え）
    Single,
    // 現在の scene を残したまま加算で読み込む
    Additive,
}

// シーンインスタンスへの opaque handle。native の scene instance UUID を保持する（pointer は持たない）。
public readonly struct SceneHandle : IEquatable<SceneHandle> {

    internal readonly ulong instanceId;

    internal SceneHandle(ulong instanceId) {
        this.instanceId = instanceId;
    }

    // instance が現在も存在するか（unload 済み/未生成は false）
    public bool IsValid => instanceId != 0 && NativeApi.SceneInstanceAlive(instanceId);

    public bool Equals(SceneHandle other) => instanceId == other.instanceId;
    public override bool Equals(object? obj) => obj is SceneHandle other && Equals(other);
    public override int GetHashCode() => instanceId.GetHashCode();
}

// SceneLoaded / SceneUnloaded で渡すイベント情報。native pointer は含めない。
public readonly struct SceneEvent {

    public SceneHandle Scene { get; }
    public UUID SceneAssetId { get; }

    internal SceneEvent(SceneHandle scene, UUID sceneAssetId) {
        Scene = scene;
        SceneAssetId = sceneAssetId;
    }
}

// 追加シーンの load / unload と lifecycle イベント。load/unload は WorldCommandBuffer 経由で遅延適用される。
// SceneLoaded は lifecycle の挿入位置（Awake 全→OnEnable/OnDisable 全→SceneLoaded→Start 全）で発火する。
// SceneUnloaded は対象 script の OnDisable/OnDestroy → scene entity 破棄の後に発火する。
// イベント購読は DLL reload 前に自動解除される（ResetForReload）。
public static class SceneManager {

    public static event Action<SceneEvent>? SceneLoaded;
    public static event Action<SceneEvent>? SceneUnloaded;

    // load/unload 要求を出した handle を、native の生存状態が確定するまで保持する（poll で検出して発火）
    private static readonly List<(SceneHandle handle, UUID asset)> pendingLoad = new();
    private static readonly List<(SceneHandle handle, UUID asset)> pendingUnload = new();

    // 追加シーンを load する。SceneHandle を即時返す（load 自体は次の flush で適用）。
    public static SceneHandle LoadAdditive(SceneAsset? scene) {
        if (scene == null) {
            return default;
        }
        ulong id = NativeApi.SceneLoadAdditive(scene.id.value);
        if (id == 0) {
            return default;
        }
        var handle = new SceneHandle(id);
        pendingLoad.Add((handle, scene.id));
        return handle;
    }

    // Unity の SceneManager.LoadScene 相当。mode で単一/加算を切り替える。
    // Single は新 scene を active にし、それまでに load 済みの scene を全て unload する（完全切り替え）。
    // Additive は現在の scene を残したまま新 scene を読み込む（LoadAdditive と同じ）。
    public static SceneHandle LoadScene(SceneAsset? scene, LoadSceneMode mode = LoadSceneMode.Single) {
        if (scene == null) {
            return default;
        }
        if (mode == LoadSceneMode.Additive) {
            return LoadAdditive(scene);
        }

        ulong id = NativeApi.SceneLoadSingle(scene.id.value);
        if (id == 0) {
            return default;
        }
        var handle = new SceneHandle(id);
        pendingLoad.Add((handle, scene.id));
        return handle;
    }

    // シーンインスタンスを unload する。stale/invalid handle は false。
    public static bool Unload(SceneHandle scene) {
        if (scene.instanceId == 0) {
            return false;
        }
        NativeApi.SceneUnload(scene.instanceId);
        pendingUnload.Add((scene, default));
        return true;
    }

    // BehaviorSystem が SynchronizeLifecycle の Pass4(OnEnable 後・Start 前)で呼ぶ。
    // native の instance 生存状態の変化を検出して SceneLoaded / SceneUnloaded を発火する。
    internal static void PumpEvents() {

        // load 完了検出（instance が alive になった）
        for (int i = pendingLoad.Count - 1; i >= 0; --i) {
            if (NativeApi.SceneInstanceAlive(pendingLoad[i].handle.instanceId)) {
                SceneEvent ev = new(pendingLoad[i].handle, pendingLoad[i].asset);
                pendingLoad.RemoveAt(i);
                EventDispatch.Raise(SceneLoaded, ev, "SceneManager");
            }
        }
        // unload 完了検出（instance が alive でなくなった）
        for (int i = pendingUnload.Count - 1; i >= 0; --i) {
            if (!NativeApi.SceneInstanceAlive(pendingUnload[i].handle.instanceId)) {
                SceneEvent ev = new(pendingUnload[i].handle, pendingUnload[i].asset);
                pendingUnload.RemoveAt(i);
                EventDispatch.Raise(SceneUnloaded, ev, "SceneManager");
            }
        }
    }

    // DLL reload(unload) 前に呼ばれ、古い assembly の delegate を手放す。
    internal static void ResetForReload() {
        SceneLoaded = null;
        SceneUnloaded = null;
        pendingLoad.Clear();
        pendingUnload.Clear();
    }
}
