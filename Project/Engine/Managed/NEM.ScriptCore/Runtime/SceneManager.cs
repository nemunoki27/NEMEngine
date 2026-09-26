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

    internal readonly ulong instanceID;

    internal SceneHandle(ulong instanceID) {
        this.instanceID = instanceID;
    }

    // instance が現在も存在するか（unload 済み/未生成は false）
    public bool IsValid => instanceID != 0 && NativeEntityAPI.SceneInstanceAlive(instanceID);

    public bool Equals(SceneHandle other) => instanceID == other.instanceID;
    public override bool Equals(object? obj) => obj is SceneHandle other && Equals(other);
    public override int GetHashCode() => instanceID.GetHashCode();
}

// SceneLoaded / SceneUnloaded で渡すイベント情報。native pointer は含めない。
public readonly struct SceneEvent {

    public SceneHandle Scene { get; }
    public AssetGUID SceneAssetID { get; }

    internal SceneEvent(SceneHandle scene, AssetGUID sceneAssetID) {
        Scene = scene;
        SceneAssetID = sceneAssetID;
    }
}

// 追加シーンの load / unload と lifecycle イベント。load/unload は WorldCommandBuffer 経由で遅延適用される。
// SceneLoaded は lifecycle の挿入位置（Awake 全→OnEnable/OnDisable 全→SceneLoaded→Start 全）で発火する。
// SceneUnloaded は対象 script の OnDisable/OnDestroy → scene gameObject 破棄の後に発火する。
// イベント購読は DLL reload 前に自動解除される（ResetForReload）。
public static class SceneManager {

    // Play中のルートと子孫を同じ実体のまま常駐させる
    public static unsafe bool DontDestroyOnLoad(GameObject root) {
        return NativeAPI.DontDestroyOnLoad(root.native) != 0;
    }

    public static event Action<SceneEvent>? SceneLoaded;
    public static event Action<SceneEvent>? SceneUnloaded;

    // load/unload 要求を出した handle を、native の生存状態が確定するまで保持する（poll で検出して発火）
    private static readonly List<(SceneHandle handle, AssetGUID asset)> pendingLoad = new();
    private static readonly List<(SceneHandle handle, AssetGUID asset)> pendingUnload = new();

    // 追加シーンを load する。SceneHandle を即時返す（load 自体は次の flush で適用）。
    public static SceneHandle LoadAdditive(SceneAsset? scene) {
        if (scene == null) {
            return default;
        }
        ulong id = NativeEntityAPI.SceneLoadAdditive(scene.id);
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

        ulong id = NativeEntityAPI.SceneLoadSingle(scene.id);
        if (id == 0) {
            return default;
        }
        var handle = new SceneHandle(id);
        pendingLoad.Add((handle, scene.id));
        return handle;
    }

    // 現在のアクティブシーンを単一ロードで読み直す
    public static SceneHandle ReloadActiveScene() {
        ulong id = NativeEntityAPI.SceneReloadActive();
        if (id == 0) {
            return default;
        }
        var handle = new SceneHandle(id);
        pendingLoad.Add((handle, default));
        return handle;
    }

    // シーンインスタンスを unload する。stale/invalid handle は false。
    public static bool Unload(SceneHandle scene) {
        if (scene.instanceID == 0) {
            return false;
        }
        NativeEntityAPI.SceneUnload(scene.instanceID);
        pendingUnload.Add((scene, default));
        return true;
    }

    // BehaviorSystem が SynchronizeLifecycle の Pass4(OnEnable 後・Start 前)で呼ぶ。
    // native の instance 生存状態の変化を検出して SceneLoaded / SceneUnloaded を発火する。
    internal static void PumpEvents() {

        // load 完了検出（instance が alive になった）
        for (int i = pendingLoad.Count - 1; i >= 0; --i) {
            if (NativeEntityAPI.SceneInstanceAlive(pendingLoad[i].handle.instanceID)) {
                SceneEvent ev = new(pendingLoad[i].handle, pendingLoad[i].asset);
                pendingLoad.RemoveAt(i);
                EventDispatch.Raise(SceneLoaded, ev, "SceneManager");
            }
        }
        // unload 完了検出（instance が alive でなくなった）
        for (int i = pendingUnload.Count - 1; i >= 0; --i) {
            if (!NativeEntityAPI.SceneInstanceAlive(pendingUnload[i].handle.instanceID)) {
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
