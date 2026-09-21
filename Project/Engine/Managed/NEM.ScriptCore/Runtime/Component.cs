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
// ScriptBehaviour派生か組込みcomponentかを型ごとに一度だけ判定する。
internal static class ComponentKind<T> where T : Component {

    internal static readonly bool isScript = typeof(ScriptBehaviour).IsAssignableFrom(typeof(T));
    internal static readonly int typeID = isScript ? -1 : GeneratedComponentTypeMap.GetTypeID<T>();

    // owner Entityからwrapperを生成する。FromEntityを持たない型はnull
    internal static T? CreateWrapper(Entity entity) =>
        isScript ? null : GeneratedComponentTypeMap.Create<T>(entity);
}
