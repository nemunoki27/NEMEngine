using System.Runtime.InteropServices;

namespace NEMEngine;

// C++側 ManagedWorldHandle と同一レイアウト。生ポインタの代わりに世代付きハンドルを持つ
[StructLayout(LayoutKind.Sequential)]
public struct ManagedWorldHandle {

    public uint index;
    public uint generation;

    public static ManagedWorldHandle Null => new() {
        index = 0xffffffffu,
        generation = 0
    };

    // generation 0 はゼロ初期化GameObjectなので無効、有効ハンドルは1始まり
    public bool isValid => index != 0xffffffffu && generation != 0;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeEntity {

    public ManagedWorldHandle world;
    public uint index;
    public uint generation;

    public static NativeEntity Null => new() {
        world = ManagedWorldHandle.Null,
        index = 0xffffffffu,
        generation = 0
    };
}

// ランタイム操作用のGameObject
public sealed class GameObject : Object, IEquatable<GameObject> {

    internal readonly NativeEntity native;

    // Entityの初期世代0は有効で、WorldとEntity番号から空参照を判定する
    internal static GameObject? FromNative(NativeEntity value) =>
        value.world.isValid && value.index != uint.MaxValue ? new GameObject(value) : null;

    internal static NativeEntity RawNative(GameObject? value) => value?.native ?? NativeEntity.Null;

    public GameObject(string name = "GameObject") {
        native = NativeEntityAPI.CreateGameObjectHandle(name);
        if (!objectAlive) {
            throw new InvalidOperationException("GameObjectを生成できません");
        }
    }

    internal GameObject(NativeEntity native) {
        this.native = native;
    }

    // ハンドルが非nullかの構造チェックのみ。破棄済みでもtrueを返すので破棄判定には使わない
    internal bool isValid => native.world.isValid && native.index != 0xffffffffu;
    // world上に生存しているか。破棄済み / 世代不一致はfalse。破棄判定はこちらを使う
    internal override bool objectAlive => isValid && NativeEntityAPI.ReadIsAlive(native);

    internal void EnsureAlive() {
        if (!objectAlive) {
            throw new MissingReferenceException("GameObjectの参照先は破棄されています");
        }
    }

    private NativeEntity checkedNative {
        get { EnsureAlive(); return native; }
    }

    public static GameObject? Find(string name) => World.Find(name);
    public static GameObject? FindWithTag(string tag) => World.FindWithTag(tag);
    public static GameObject[] FindGameObjectsWithTag(string tag) => World.FindEntitiesWithTag(tag);

    public string name {
        get => NativeEntityAPI.ReadName(checkedNative);
        set => NativeEntityAPI.WriteName(checkedNative, value);
    }

    public bool activeSelf {
        get => NativeEntityAPI.ReadActiveSelf(checkedNative);
        set => NativeEntityAPI.WriteActiveSelf(checkedNative, value);
    }

    public bool activeInHierarchy => NativeEntityAPI.ReadActiveInHierarchy(checkedNative);
    internal GameObject? parent {
        get => NativeTransformAPI.ReadParent(checkedNative);
        set => NativeTransformAPI.WriteParent(checkedNative, RawNative(value));
    }
    internal GameObject? firstChild => NativeTransformAPI.ReadFirstChild(checkedNative);
    internal GameObject? nextSibling => NativeTransformAPI.ReadNextSibling(checkedNative);
    private Transform? cachedTransform;
    public Transform transform {
        get {
            EnsureAlive();
            if (cachedTransform == null) {
                cachedTransform = GetComponent<Transform>() ?? throw new MissingReferenceException("Transformがありません");
            }
            return cachedTransform;
        }
    }

    // ゲームプレイ用タグから選ぶ
    public string tag {
        get => NativeEntityAPI.ReadTag(checkedNative);
        set => NativeEntityAPI.WriteTag(checkedNative, value);
    }

    // 指定タグと一致するか
    public bool CompareTag(string other) => NativeEntityAPI.ReadTag(checkedNative) == other;

    // 描画カリング用のレイヤーマスク、カメラのcullingMaskと照合される
    public uint visibilityLayerMask {
        get => NativeEntityAPI.ReadVisibilityLayerMask(checkedNative);
        set => NativeEntityAPI.WriteVisibilityLayerMask(checkedNative, value);
    }

    // 衝突フィルタ用のタイプビットマスク、CollisionComponentが無ければ0
    public uint collisionTypeMask {
        get => NativePhysicsAPI.ReadCollisionTypeMask(checkedNative);
        set => NativePhysicsAPI.WriteCollisionTypeMask(checkedNative, value);
    }

    public void SetActive(bool active) {
        activeSelf = active;
    }

    public void SetParent(GameObject? parent) {
        this.parent = parent;
    }

    //========================================================================
    //	component / script access
    //	組込みcomponentもMonoBehaviourも同じGetComponent<T>で扱う（Unity準拠）
    //	型カテゴリはComponentKind<T>が一度だけ判定してキャッシュする（hot pathでreflectionしない）
    //========================================================================

    // 指定componentを返す。未attach / invalidはnull
    public T? GetComponent<T>() where T : class {
        EnsureAlive();
        if (ComponentKind<T>.isScript) {
            return HostBridge.FindScriptAs<T>(checkedNative);
        }
        if (ComponentKind<T>.typeID < 0) {
            var matches = new List<T>();
            AppendComponents(matches);
            return matches.Count == 0 ? null : matches[0];
        }
        if (!NativeEntityAPI.ReadHasComponent(checkedNative, ComponentKind<T>.typeID)) {
            return null;
        }
        return ComponentKind<T>.CreateWrapper(this);
    }

    // 持っていればtrueでoutへ返す。未attachはfalse
    public bool TryGetComponent<T>(out T component) where T : class {
        component = GetComponent<T>()!;
        return component != null;
    }

    // 指定componentを持つか。invalid GameObjectは常にfalse
    public bool HasComponent<T>() where T : class {
        return GetComponent<T>() != null;
    }

    // 追加予約を含め、直後から読み書きできるComponentを返す
    public T AddComponent<T>() where T : Component {
        EnsureAlive();
        if (ComponentKind<T>.isScript) {
            return HostBridge.AttachScriptAs<T>(checkedNative) ??
                throw new InvalidOperationException($"{typeof(T).Name}を追加できません");
        }
        if (ComponentKind<T>.typeID < 0) {
            throw new ArgumentException($"{typeof(T).Name}は追加可能なComponentではありません");
        }
        NativeEntityAPI.EnqueueAddComponent(checkedNative, ComponentKind<T>.typeID);
        T? component = ComponentKind<T>.CreateWrapper(this);
        if (component == null) { throw new InvalidOperationException($"{typeof(T).Name}を追加できません"); }
        return component;
    }

    // component削除。同じく遅延適用（missing removeは安全にno-op）
    public void RemoveComponent<T>() where T : Component {
        if (isValid && !ComponentKind<T>.isScript && ComponentKind<T>.typeID >= 0) {
            NativeEntityAPI.EnqueueRemoveComponent(checkedNative, ComponentKind<T>.typeID);
        }
    }

    // DynamicBufferはチャンクアドレスを保持せず、各操作で現在位置を再解決する
    public DynamicBuffer<T> GetBuffer<T>()
        where T : unmanaged, IBufferElementData<T> {

        return new DynamicBuffer<T>(this);
    }

    public bool HasBuffer<T>()
        where T : unmanaged, IBufferElementData<T> {

        return isValid && new DynamicBuffer<T>(this).IsCreated;
    }

    // Bufferの追加削除も通常Componentと同じ安全地点で反映する
    public DynamicBuffer<T> AddBuffer<T>()
        where T : unmanaged, IBufferElementData<T> {

        if (isValid) {
            NativeEntityAPI.EnqueueAddComponent(checkedNative, T.componentTypeID);
        }
        return new DynamicBuffer<T>(this);
    }

    public void RemoveBuffer<T>()
        where T : unmanaged, IBufferElementData<T> {

        if (isValid) {
            NativeEntityAPI.EnqueueRemoveComponent(checkedNative, T.componentTypeID);
        }
    }

    // GameObject 破棄。callback 中の即時破棄は走査を壊すため WorldCommandBuffer 経由で遅延適用される。
    // flush 後に isAlive == false。invalid / 二重破棄は安全に扱われる
    public void Destroy() {
        if (objectAlive) {
            NativeEntityAPI.EnqueueDestroyEntity(native);
        }
    }

    //========================================================================
    //	階層を辿る component 取得
    //	firstChild/nextSibling/parent だけで辿る。scriptもcomponentも同じAPIで扱う
    //========================================================================

    // 自身か子孫から最初に見つかったcomponentを返す。Unityと同じく自身も対象に含める
    public T? GetComponentInChildren<T>(bool includeInactive = false) where T : class {
        if (GetComponent<T>() is T found) {
            return found;
        }
        for (GameObject? child = firstChild; child != null; child = child.nextSibling) {
            if ((includeInactive || child.activeInHierarchy) && child.GetComponentInChildren<T>(includeInactive) is T inChild) {
                return inChild;
            }
        }
        return null;
    }

    // 自身か祖先から最初に見つかったcomponentを返す。自身も対象に含める
    public T? GetComponentInParent<T>(bool includeInactive = false) where T : class {
        for (GameObject? current = this; current != null; current = current.parent) {
            if ((current == this || includeInactive || current.activeInHierarchy) && current.GetComponent<T>() is T found) {
                return found;
            }
        }
        return null;
    }

    // 自身と全子孫のcomponentを集めて返す
    public T[] GetComponents<T>() where T : class {
        var result = new List<T>();
        GetComponents(result);
        return result.ToArray();
    }

    public void GetComponents<T>(List<T> results) where T : class {
        ArgumentNullException.ThrowIfNull(results);
        EnsureAlive();
        results.Clear();
        AppendComponents(results);
    }

    private void AppendComponents<T>(List<T> results) where T : class {
        if (!typeof(T).IsInterface && !typeof(Component).IsAssignableFrom(typeof(T))) {
            throw new ArgumentException($"{typeof(T).Name}はComponentまたはinterfaceではありません");
        }
        GeneratedComponentTypeMap.Append(this, results);
        HostBridge.AppendScriptsAs(checkedNative, results);
    }

    public T[] GetComponentsInChildren<T>(bool includeInactive = false) where T : class {
        var result = new List<T>();
        GetComponentsInChildren(includeInactive, result);
        return result.ToArray();
    }

    public void GetComponentsInChildren<T>(List<T> results) where T : class => GetComponentsInChildren(false, results);

    public void GetComponentsInChildren<T>(bool includeInactive, List<T> results) where T : class {
        ArgumentNullException.ThrowIfNull(results);
        EnsureAlive();
        results.Clear();
        CollectComponentsInChildren(this, includeInactive, results);
    }

    private static void CollectComponentsInChildren<T>(GameObject gameObject, bool includeInactive, List<T> result) where T : class {
        gameObject.AppendComponents(result);
        for (GameObject? child = gameObject.firstChild; child != null; child = child.nextSibling) {
            if (includeInactive || child.activeInHierarchy) { CollectComponentsInChildren(child, includeInactive, result); }
        }
    }

    public T[] GetComponentsInParent<T>(bool includeInactive = false) where T : class {
        var result = new List<T>();
        GetComponentsInParent(includeInactive, result);
        return result.ToArray();
    }

    public void GetComponentsInParent<T>(bool includeInactive, List<T> results) where T : class {
        ArgumentNullException.ThrowIfNull(results);
        EnsureAlive();
        results.Clear();
        for (GameObject? current = this; current != null; current = current.parent) {
            if (current == this || includeInactive || current.activeInHierarchy) { current.AppendComponents(results); }
        }
    }

    //========================================================================
    //	等価判定（world handle + gameObject index + gameObject generation）
    //========================================================================

    public bool Equals(GameObject? other) => this == other;

    private protected override bool EqualsObject(Object other) => other is GameObject target &&
        native.world.index == target.native.world.index && native.world.generation == target.native.world.generation &&
        native.index == target.native.index && native.generation == target.native.generation;

    public override int GetHashCode() =>
        HashCode.Combine(native.world.index, native.world.generation, native.index, native.generation);
}
