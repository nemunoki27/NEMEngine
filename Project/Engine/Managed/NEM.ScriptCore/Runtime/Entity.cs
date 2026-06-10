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

// runtime 操作用の Entity facade。ECSWorld* / native pointer は保持せず、
// world handle(index/generation) + entity index/generation の opaque handle だけを持つ。
// JSON へ直接保存しない（serialization 用 identity は EntityRef）。
public readonly struct Entity : IEquatable<Entity> {

    internal readonly NativeEntity native;

    public static Entity nullEntity => new(NativeEntity.Null);

    internal Entity(NativeEntity native) {
        this.native = native;
    }

    // 生ポインタ判定ではなく、world handleとentity indexの有効値で判定する。
    // default(Entity) は world handle が無効なので安全に invalid。
    public bool isValid => native.world.isValid && native.index != 0xffffffffu;
    // world generation / entity generation の検証は native 側 IsAlive が行う。
    // Stop 後の古い Entity や次 Play world の別 Entity を誤参照しない
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

    // wrapper を返す。欠落時は明確な例外（呼び出し側は Has/TryGet で確認するのが推奨）
    public T Get<T>() where T : struct, IComponentRef<T> {
        if (!Has<T>()) {
            throw new InvalidOperationException(
                $"Entity does not have component '{T.componentTypeName}'. Use Has<T>() or TryGet<T>() first.");
        }
        return T.FromEntity(this);
    }

    // component 追加。archetype 移動は WorldCommandBuffer 経由で安全地点まで遅延される（duplicate add は native 側で吸収）
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

    // Entity 破棄。callback 中の即時破棄は走査を壊すため WorldCommandBuffer 経由で遅延適用される。
    // flush 後に isAlive == false。invalid / 二重破棄は安全に扱われる
    public void Destroy() {
        if (isValid) {
            NativeApi.EnqueueDestroyEntity(native);
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
