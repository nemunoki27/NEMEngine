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

    public bool isValid => index != 0xffffffffu;
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
    //	generic component access
    //	compact native component type id ベース。hot path で reflection / string lookup をしない
    //	（型ごとの id は ComponentType<T> が一度だけ解決して static キャッシュする）
    //========================================================================

    // 指定 component を持つか。invalid Entity は常に false
    public bool Has<T>() where T : struct, IComponentRef<T> {
        return isValid && NativeApi.ReadHasComponent(native, ComponentType<T>.Id);
    }

    // 持っていれば wrapper を返して true。invalid / 欠落は false
    public bool TryGet<T>(out T component) where T : struct, IComponentRef<T> {
        if (Has<T>()) {
            component = T.FromEntity(this);
            return true;
        }
        component = default;
        return false;
    }

    // wrapper を返す。欠落時は明確な例外
    public T Get<T>() where T : struct, IComponentRef<T> {
        if (!Has<T>()) {
            throw new InvalidOperationException(
                $"Entity does not have component '{T.componentTypeName}'. Use Has<T>() or TryGet<T>() first.");
        }
        return T.FromEntity(this);
    }

    // component 追加
    public void Add<T>() where T : struct, IComponentRef<T> {
        if (isValid) {
            NativeApi.EnqueueAddComponent(native, ComponentType<T>.Id);
        }
    }

    // component 削除。同じく遅延適用（missing remove は安全に no-op）
    public void Remove<T>() where T : struct, IComponentRef<T> {
        if (isValid) {
            NativeApi.EnqueueRemoveComponent(native, ComponentType<T>.Id);
        }
    }

    //========================================================================
    //	script(ScriptBehaviour) 取得
    //	同 Entity 上の C# スクリプト instance を型で引く（Unity の GetComponent<Script> 相当）
    //	component(struct)とは別経路で、handle は native の script registry が保持する
    //========================================================================

    // 同 Entity 上の指定スクリプトを返す。未 attach / 型不一致 / invalid は null
    public T? GetComponent<T>() where T : ScriptBehaviour {
        return isValid ? HostBridge.FindScript<T>(native) : null;
    }

    // 持っていれば true で out へ返す。未 attach は false
    public bool TryGetComponent<T>(out T script) where T : ScriptBehaviour {
        script = (isValid ? HostBridge.FindScript<T>(native) : null)!;
        return script != null;
    }

    // Entity 破棄。callback 中の即時破棄は走査を壊すため WorldCommandBuffer 経由で遅延適用される。
    // flush 後に isAlive == false。invalid / 二重破棄は安全に扱われる
    public void Destroy() {
        if (isValid) {
            NativeApi.EnqueueDestroyEntity(native);
        }
    }

    //========================================================================
    //	階層を辿る component 取得（GetComponentInChildren / InParent 相当）
    //	既存の Has<T>/Get<T> と firstChild/nextSibling/parent だけで辿る（reflection しない）
    //========================================================================

    // 自身か子孫から最初に見つかった component を返す。Unity と同じく自身も対象に含める
    public bool TryGetInChildren<T>(out T component) where T : struct, IComponentRef<T> {
        if (TryGet(out component)) {
            return true;
        }
        for (Entity child = firstChild; child.isAlive; child = child.nextSibling) {
            if (child.TryGetInChildren(out component)) {
                return true;
            }
        }
        component = default;
        return false;
    }

    // 自身か子孫から component を返す。欠落時は例外（Has / TryGetInChildren で確認するのが推奨）
    public T GetInChildren<T>() where T : struct, IComponentRef<T> {
        if (!TryGetInChildren(out T component)) {
            throw new InvalidOperationException(
                $"No component '{T.componentTypeName}' was found in this Entity or its children.");
        }
        return component;
    }

    // 自身か祖先から最初に見つかった component を返す。自身も対象に含める
    public bool TryGetInParent<T>(out T component) where T : struct, IComponentRef<T> {
        for (Entity current = this; current.isAlive; current = current.parent) {
            if (current.TryGet(out component)) {
                return true;
            }
        }
        component = default;
        return false;
    }

    // 自身か祖先から component を返す。欠落時は例外
    public T GetInParent<T>() where T : struct, IComponentRef<T> {
        if (!TryGetInParent(out T component)) {
            throw new InvalidOperationException(
                $"No component '{T.componentTypeName}' was found in this Entity or its parents.");
        }
        return component;
    }

    // 自身と全子孫の component を集めて返す
    public List<T> GetAllInChildren<T>() where T : struct, IComponentRef<T> {
        var result = new List<T>();
        CollectInChildren(this, result);
        return result;
    }

    private static void CollectInChildren<T>(Entity entity, List<T> result) where T : struct, IComponentRef<T> {
        if (entity.TryGet(out T component)) {
            result.Add(component);
        }
        for (Entity child = entity.firstChild; child.isAlive; child = child.nextSibling) {
            CollectInChildren(child, result);
        }
    }

    //========================================================================
    //	階層を辿る script 取得（GetComponentInChildren / InParent の script 版）
    //	component 版と同じく自身も対象に含め、firstChild/nextSibling/parent だけで辿る
    //========================================================================

    // 自身か子孫から最初に見つかった T 型スクリプトを返す。見つからなければ null
    public T? GetComponentInChildren<T>() where T : ScriptBehaviour {
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

    // 自身か祖先から最初に見つかった T 型スクリプトを返す。見つからなければ null
    public T? GetComponentInParent<T>() where T : ScriptBehaviour {
        for (Entity current = this; current.isAlive; current = current.parent) {
            if (current.GetComponent<T>() is T found) {
                return found;
            }
        }
        return null;
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
