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

    // generation 0 はゼロ初期化Entityなので無効、有効ハンドルは1始まり
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

// ランタイム操作用のEntity
public readonly struct Entity : IEquatable<Entity> {

    internal readonly NativeEntity native;

    public static Entity nullEntity => new(NativeEntity.Null);

    internal Entity(NativeEntity native) {
        this.native = native;
    }

    public bool isValid => native.world.isValid && native.index != 0xffffffffu;
    public bool isAlive => isValid && NativeApi.ReadIsAlive(native);

    public string name {
        get => NativeApi.ReadName(native);
        set => NativeApi.WriteName(native, value);
    }

    public bool activeSelf {
        get => NativeApi.ReadActiveSelf(native);
        set => NativeApi.WriteActiveSelf(native, value);
    }

    public bool activeInHierarchy => NativeApi.ReadActiveInHierarchy(native);
    public Entity parent {
        get => NativeApi.ReadParent(native);
        set => NativeApi.WriteParent(native, value.native);
    }
    public Entity firstChild => NativeApi.ReadFirstChild(native);
    public Entity nextSibling => NativeApi.ReadNextSibling(native);
    public Transform transform => new(this);

    // ゲームプレイ用タグから選ぶ
    public string tag {
        get => NativeApi.ReadTag(native);
        set => NativeApi.WriteTag(native, value);
    }

    // 指定タグと一致するか
    public bool CompareTag(string other) => NativeApi.ReadTag(native) == other;

    // 描画カリング用のレイヤーマスク、カメラのcullingMaskと照合される
    public uint visibilityLayerMask {
        get => NativeApi.ReadVisibilityLayerMask(native);
        set => NativeApi.WriteVisibilityLayerMask(native, value);
    }

    // 衝突フィルタ用のタイプビットマスク、CollisionComponentが無ければ0
    public uint collisionTypeMask {
        get => NativeApi.ReadCollisionTypeMask(native);
        set => NativeApi.WriteCollisionTypeMask(native, value);
    }

    // gameplay 向け PascalCase エイリアス（既存 lowercase へ委譲。二重ロジックは持たない）
    public Transform Transform => new(this);
    public bool ActiveSelf {
        get => activeSelf;
        set => activeSelf = value;
    }
    public string Name {
        get => name;
        set => name = value;
    }
    public string Tag {
        get => tag;
        set => tag = value;
    }

    public void SetActive(bool active) {
        activeSelf = active;
    }

    public void SetParent(Entity parent) {
        this.parent = parent;
    }

    //========================================================================
    //	component / script access
    //	組込みcomponentもScriptBehaviourも同じGetComponent<T>で扱う（Unity準拠）
    //	型カテゴリはComponentKind<T>が一度だけ判定してキャッシュする（hot pathでreflectionしない）
    //========================================================================

    // 指定componentを返す。未attach / invalidはnull
    public T? GetComponent<T>() where T : Component {
        if (!isValid) {
            return null;
        }
        if (ComponentKind<T>.isScript) {
            return HostBridge.FindScriptAs<T>(native);
        }
        if (ComponentKind<T>.typeId < 0 || !NativeApi.ReadHasComponent(native, ComponentKind<T>.typeId)) {
            return null;
        }
        return ComponentKind<T>.CreateWrapper(this);
    }

    // 持っていればtrueでoutへ返す。未attachはfalse
    public bool TryGetComponent<T>(out T component) where T : Component {
        component = GetComponent<T>()!;
        return component != null;
    }

    // 指定componentを持つか。invalid Entityは常にfalse
    public bool HasComponent<T>() where T : Component {
        return GetComponent<T>() != null;
    }

    // component追加。構造変更はWorldCommandBuffer経由で遅延適用され、flushまで他のAPIからは見えない。
    // ScriptBehaviourのruntime attachは未対応でnullを返す
    public T? AddComponent<T>() where T : Component {
        if (!isValid || ComponentKind<T>.isScript || ComponentKind<T>.typeId < 0) {
            return null;
        }
        NativeApi.EnqueueAddComponent(native, ComponentKind<T>.typeId);
        return ComponentKind<T>.CreateWrapper(this);
    }

    // component削除。同じく遅延適用（missing removeは安全にno-op）
    public void RemoveComponent<T>() where T : Component {
        if (isValid && !ComponentKind<T>.isScript && ComponentKind<T>.typeId >= 0) {
            NativeApi.EnqueueRemoveComponent(native, ComponentKind<T>.typeId);
        }
    }

    // Entity 破棄。callback 中の即時破棄は走査を壊すため WorldCommandBuffer 経由で遅延適用される。
    // flush 後に isAlive == false。invalid / 二重破棄は安全に扱われる
    public void Destroy() {
        if (isValid) {
            NativeApi.EnqueueDestroyEntity(native);
        }
    }

    //========================================================================
    //	階層を辿る component 取得
    //	firstChild/nextSibling/parent だけで辿る。scriptもcomponentも同じAPIで扱う
    //========================================================================

    // 自身か子孫から最初に見つかったcomponentを返す。Unityと同じく自身も対象に含める
    public T? GetComponentInChildren<T>() where T : Component {
        if (GetComponent<T>() is T found) {
            return found;
        }
        for (Entity child = firstChild; child.isAlive; child = child.nextSibling) {
            if (child.GetComponentInChildren<T>() is T inChild) {
                return inChild;
            }
        }
        return null;
    }

    // 自身か祖先から最初に見つかったcomponentを返す。自身も対象に含める
    public T? GetComponentInParent<T>() where T : Component {
        for (Entity current = this; current.isAlive; current = current.parent) {
            if (current.GetComponent<T>() is T found) {
                return found;
            }
        }
        return null;
    }

    // 自身と全子孫のcomponentを集めて返す
    public List<T> GetComponentsInChildren<T>() where T : Component {
        var result = new List<T>();
        CollectComponentsInChildren(this, result);
        return result;
    }

    private static void CollectComponentsInChildren<T>(Entity entity, List<T> result) where T : Component {
        if (entity.GetComponent<T>() is T component) {
            result.Add(component);
        }
        for (Entity child = entity.firstChild; child.isAlive; child = child.nextSibling) {
            CollectComponentsInChildren(child, result);
        }
    }

    //========================================================================
    //	等価判定（world handle + entity index + entity generation）
    //========================================================================

    public bool Equals(Entity other) {
        return native.world.index == other.native.world.index
            && native.world.generation == other.native.world.generation
            && native.index == other.native.index
            && native.generation == other.native.generation;
    }

    public override bool Equals(object? obj) => obj is Entity other && Equals(other);

    public override int GetHashCode() {
        return HashCode.Combine(native.world.index, native.world.generation, native.index, native.generation);
    }

    public static bool operator ==(Entity left, Entity right) => left.Equals(right);
    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);
}
