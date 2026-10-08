using System.Diagnostics.CodeAnalysis;

namespace NEMEngine;

// Native個体の生存と参照の等値を扱う共通基底
public abstract class Object {

    // Prefab Assetから実行用GameObjectを生成する
    public static GameObject Instantiate(Prefab original) => Instantiate(original, null);
    public static GameObject Instantiate(Prefab original, GameObject? parent) {
        ArgumentNullException.ThrowIfNull(original);
        return original.Instantiate(parent) ??
            throw new InvalidOperationException("Prefabを生成できません");
    }
    public static GameObject Instantiate(Prefab original, Vector3 position, Quaternion rotation) =>
        Instantiate(original, position, rotation, null);
    public static GameObject Instantiate(Prefab original, Vector3 position, Quaternion rotation, GameObject? parent) {
        ArgumentNullException.ThrowIfNull(original);
        return original.Instantiate(position, rotation, parent) ??
            throw new InvalidOperationException("Prefabを生成できません");
    }

    // GameObjectまたはComponentの所有階層を複製する
    public static T Instantiate<T>(T original) where T : Object => InstantiateObject(original, false, default, default, null);
    public static T Instantiate<T>(T original, GameObject? parent) where T : Object =>
        InstantiateObject(original, false, default, default, parent);
    public static T Instantiate<T>(T original, Vector3 position, Quaternion rotation) where T : Object =>
        InstantiateObject(original, true, position, rotation, null);
    public static T Instantiate<T>(T original, Vector3 position, Quaternion rotation, GameObject? parent) where T : Object =>
        InstantiateObject(original, true, position, rotation, parent);

    private static T InstantiateObject<T>(T original, bool useTransform, Vector3 position,
        Quaternion rotation, GameObject? parent) where T : Object {

        if (ReferenceEquals(original, null) || original == null) {
            throw new ArgumentException("複製元が無効です", nameof(original));
        }
        GameObject source = original switch {
            GameObject gameObject => gameObject,
            Component component => component.gameObject,
            _ => throw new ArgumentException("GameObjectまたはComponentを指定してください", nameof(original))
        };
        GameObject clone = NativeEntityAPI.CloneEntity(source, position, rotation, useTransform, parent) ??
            throw new InvalidOperationException("GameObjectを複製できません");
        if (original is GameObject) {
            return (T)(Object)clone;
        }
        if (original is MonoBehaviour script) {
            string? typeID = HostBridge.GetScriptTypeGUID(script.GetType());
            return typeID != null && HostBridge.FindScriptByGUID(clone.native, typeID, script.scriptSlotID) is T clonedScript ?
                clonedScript : throw new InvalidOperationException("複製先のScriptを解決できません");
        }
        return clone.GetComponent<T>() ?? throw new InvalidOperationException("複製先のComponentを解決できません");
    }

    // 参照先の個体だけを安全地点で破棄する
    public static void Destroy(Object? target) {

        if (target == null) {
            return;
        }
        switch (target) {
            case GameObject gameObject:
                gameObject.Destroy();
                break;
            case Component component:
                component.EnqueueDestroy();
                break;
            default:
                throw new ArgumentException("実行中のGameObjectかComponentを指定してください", nameof(target));
        }
    }

    internal abstract bool objectAlive { get; }

    // 同じ個体を指す参照だけを等値にする
    private protected virtual bool EqualsObject(Object other) => ReferenceEquals(this, other);

    public static bool operator ==(Object? lhs, Object? rhs) {

        // 実際のnullとの比較だけでNative側の生存を調べる
        if (lhs is null) {
            return rhs is null || !rhs.objectAlive;
        }
        if (rhs is null) {
            return !lhs.objectAlive;
        }
        return ReferenceEquals(lhs, rhs) || lhs.EqualsObject(rhs);
    }

    public static bool operator !=(Object? lhs, Object? rhs) => !(lhs == rhs);
    public static implicit operator bool([NotNullWhen(true)] Object? value) => value != null;

    public override bool Equals(object? obj) => obj is null ? this == null : obj is Object other && this == other;
    public override int GetHashCode() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);
}
