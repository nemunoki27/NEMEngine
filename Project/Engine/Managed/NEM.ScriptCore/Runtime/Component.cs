namespace NEMEngine;

// GameObjectとComponentの個体番号を保持する共通基底
public abstract class Component : Object {

    private GameObject? owner;
    private int nativeTypeID = -1;
    private ulong nativeInstanceID;

    public GameObject gameObject {
        get {
            if (!objectAlive) {
                throw new MissingReferenceException($"{GetType().Name}の参照先は破棄されています");
            }
            return owner!;
        }
        internal set {
            owner = value;
            nativeTypeID = GeneratedComponentTypeMap.GetTypeID(GetType());
            nativeInstanceID = NativeEntityAPI.ReadComponentInstanceID(GameObject.RawNative(owner), nativeTypeID);
        }
    }

    public Transform transform => gameObject.transform;
    internal GameObject? ownerReference => owner;

    internal override bool objectAlive => nativeInstanceID != 0 &&
        NativeEntityAPI.ReadComponentInstanceID(GameObject.RawNative(owner), nativeTypeID) == nativeInstanceID;
    private protected bool ownerAlive => owner != null;

    // 個体を確認してからNative操作へ渡す
    internal NativeEntity native {
        get {
            if (!objectAlive) {
                throw new MissingReferenceException($"{GetType().Name}の参照先は破棄されています");
            }
            return GameObject.RawNative(owner);
        }
    }

    // 同じWorldとGameObjectに属する同じComponent個体を比較する
    private protected override bool EqualsObject(Object other) =>
        nativeInstanceID != 0 && other is Component component && component.nativeTypeID == nativeTypeID &&
        component.nativeInstanceID == nativeInstanceID && component.owner == owner;

    public override int GetHashCode() => HashCode.Combine(nativeTypeID, nativeInstanceID, owner);

    //========================================================================
    //	component access(owner GameObjectへの委譲)
    //========================================================================
    public T? GetComponent<T>() where T : class => gameObject.GetComponent<T>();
    public bool TryGetComponent<T>(out T component) where T : class => gameObject.TryGetComponent(out component);
    public bool HasComponent<T>() where T : class => gameObject.HasComponent<T>();
    public T AddComponent<T>() where T : Component => gameObject.AddComponent<T>();
    public void RemoveComponent<T>() where T : Component => gameObject.RemoveComponent<T>();
    public T[] GetComponents<T>() where T : class => gameObject.GetComponents<T>();
    public void GetComponents<T>(List<T> results) where T : class => gameObject.GetComponents(results);
    public T? GetComponentInChildren<T>(bool includeInactive = false) where T : class => gameObject.GetComponentInChildren<T>(includeInactive);
    public T? GetComponentInParent<T>(bool includeInactive = false) where T : class => gameObject.GetComponentInParent<T>(includeInactive);
    public T[] GetComponentsInChildren<T>(bool includeInactive = false) where T : class => gameObject.GetComponentsInChildren<T>(includeInactive);
    public void GetComponentsInChildren<T>(List<T> results) where T : class => gameObject.GetComponentsInChildren(results);
    public void GetComponentsInChildren<T>(bool includeInactive, List<T> results) where T : class => gameObject.GetComponentsInChildren(includeInactive, results);
    public T[] GetComponentsInParent<T>(bool includeInactive = false) where T : class => gameObject.GetComponentsInParent<T>(includeInactive);
    public void GetComponentsInParent<T>(bool includeInactive, List<T> results) where T : class => gameObject.GetComponentsInParent(includeInactive, results);
}

// GetComponent<T>統一APIのための型カテゴリキャッシュ。
// MonoBehaviour派生か組込みcomponentかを型ごとに一度だけ判定する。
internal static class ComponentKind<T> where T : class {

    internal static readonly bool isScript = typeof(MonoBehaviour).IsAssignableFrom(typeof(T));
    internal static readonly int typeID = isScript ? -1 : GeneratedComponentTypeMap.GetTypeID<T>();

    // owner GameObjectからwrapperを生成する。FromEntityを持たない型はnull
    internal static T? CreateWrapper(GameObject gameObject) =>
        isScript ? null : GeneratedComponentTypeMap.Create<T>(gameObject);
}
