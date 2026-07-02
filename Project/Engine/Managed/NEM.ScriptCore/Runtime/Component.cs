using System.Reflection;

namespace NEMEngine;

// Entity上のcomponentとScriptBehaviourの共通基底。owner Entityのopaque handleだけを保持し、
// native component pointer / ECS chunk pointerは一切保持しない。
public abstract class Component : Object {

    private Entity owner;
    private Transform? cachedTransform;

    public Entity entity {
        get => owner;
        internal set {
            owner = value;
            cachedTransform = null;
        }
    }

    public Transform transform => cachedTransform ??= new Transform(owner);

    internal override bool objectAlive => owner.isAlive;

    // 同一Entity上の同型componentは同じ参照先として扱う(ScriptBehaviourは参照同一性で上書きする)
    private protected override bool EqualsObject(Object other) =>
        other is Component component && component.GetType() == GetType() && component.owner == owner;

    public override int GetHashCode() => HashCode.Combine(GetType(), owner);

    //========================================================================
    //	component access(owner Entityへの委譲)
    //========================================================================
    public T? GetComponent<T>() where T : Component => owner.GetComponent<T>();
    public bool TryGetComponent<T>(out T component) where T : Component => owner.TryGetComponent(out component);
    public bool HasComponent<T>() where T : Component => owner.HasComponent<T>();
    public T? AddComponent<T>() where T : Component => owner.AddComponent<T>();
    public void RemoveComponent<T>() where T : Component => owner.RemoveComponent<T>();
    public T? GetComponentInChildren<T>() where T : Component => owner.GetComponentInChildren<T>();
    public T? GetComponentInParent<T>() where T : Component => owner.GetComponentInParent<T>();
    public List<T> GetComponentsInChildren<T>() where T : Component => owner.GetComponentsInChildren<T>();
}

// GetComponent<T>統一APIのための型カテゴリキャッシュ。
// ScriptBehaviour派生か組込みcomponentかを型ごとに一度だけ判定し、
// 組込み側はIComponentRef<T>のstatic実装を初回のみreflectionで引いてdelegateへ固定する(hot pathではreflectionしない)。
internal static class ComponentKind<T> where T : Component {

    internal static readonly bool isScript = typeof(ScriptBehaviour).IsAssignableFrom(typeof(T));

    // -2 = 未解決, -1 = 未登録(script型含む)
    private static int cachedTypeId = -2;
    private static Func<Entity, T>? fromEntity;
    private static bool factoryResolved;

    internal static int typeId {
        get {
            if (cachedTypeId == -2) {
                cachedTypeId = ResolveTypeId();
            }
            return cachedTypeId;
        }
    }

    // owner Entityからwrapperを生成する。FromEntityを持たない型はnull
    internal static T? CreateWrapper(Entity entity) {
        if (!factoryResolved) {
            fromEntity = ResolveFactory();
            factoryResolved = true;
        }
        return fromEntity?.Invoke(entity);
    }

    private static int ResolveTypeId() {
        if (isScript) {
            return -1;
        }
        PropertyInfo? nameProperty = typeof(T).GetProperty("componentTypeName", BindingFlags.Public | BindingFlags.Static);
        return nameProperty?.GetValue(null) is string componentTypeName
            ? NativeApi.ResolveComponentTypeId(componentTypeName)
            : -1;
    }

    private static Func<Entity, T>? ResolveFactory() {
        if (isScript) {
            return null;
        }
        MethodInfo? method = typeof(T).GetMethod("FromEntity",
            BindingFlags.Public | BindingFlags.Static, new[] { typeof(Entity) });
        return method != null && method.ReturnType == typeof(T)
            ? (Func<Entity, T>)Delegate.CreateDelegate(typeof(Func<Entity, T>), method)
            : null;
    }
}
