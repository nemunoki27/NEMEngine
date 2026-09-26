namespace NEMEngine;

// gameplay code から新しい GameObject を生成する。生成は WorldCommandBuffer 経由（structural mutation は遅延）。
internal static class World {

    // 空 GameObject を即時予約して返す。Transform/SceneObject/Name の付与と親付けは次の flush で適用される。
    // 可視性規則:
    //   flush 前: 返り値の GameObject は isAlive=true（予約済み）。ただし component はまだ無いため
    //             Has<T>()=false、Get<T>() は例外、Transform の get は既定値、set は staging される。
    //             Name は flush まで空。
    //   flush 後: 通常 GameObject と同じ。lifecycle(script)は次の SynchronizeLifecycle から開始する。
    public static GameObject? CreateEntity(string? name = null) {
        return NativeEntityAPI.SpawnEntity(name, null);
    }

    public static GameObject? CreateEntity(string? name, GameObject? parent) {
        return NativeEntityAPI.SpawnEntity(name, parent);
    }

    //========================================================================
    //	検索（アクティブワールド全体のO(n)走査。頻繁に呼ぶ場合は結果をキャッシュ推奨）
    //========================================================================

    // 名前が一致する最初のGameObjectを返す、未発見はnull GameObject
    public static GameObject? Find(string name) => NativeEntityAPI.FindByName(name);

    // タグが一致する最初のGameObjectを返す、未発見はnull GameObject
    public static GameObject? FindWithTag(string tag) => NativeEntityAPI.FindByTag(tag);

    // タグが一致する全GameObjectを返す
    public static GameObject[] FindEntitiesWithTag(string tag) => NativeEntityAPI.FindManyByTag(tag);

    // 指定型のスクリプトを1つ返す、未発見はnull。Unityの FindObjectOfType 相当
    public static T? FindObjectOfType<T>() where T : MonoBehaviour => HostBridge.FindScriptOfType<T>();

    // 指定型のスクリプトを全て返す
    public static T[] FindObjectsOfType<T>() where T : MonoBehaviour => HostBridge.FindScriptsOfType<T>();

    // 指定componentを持つ最初のGameObjectを返す、未発見はnull GameObject。script型はinstance走査で解決する
    public static GameObject? FindEntityWithComponent<T>() where T : Component {
        if (ComponentKind<T>.isScript) {
            MonoBehaviour? script = HostBridge.FindScriptOfTypeByType(typeof(T));
            return script != null ? script.gameObject : null;
        }
        return ComponentKind<T>.typeID >= 0 ? NativeEntityAPI.FindByComponent(ComponentKind<T>.typeID) : null;
    }

    // 指定componentを持つ全GameObjectを返す
    public static GameObject[] FindEntitiesWithComponent<T>() where T : Component {
        if (ComponentKind<T>.isScript) {
            List<MonoBehaviour> scripts = HostBridge.FindScriptsOfTypeByType(typeof(T));
            var entities = new GameObject[scripts.Count];
            for (int i = 0; i < scripts.Count; ++i) {
                entities[i] = scripts[i].gameObject;
            }
            return entities;
        }
        return ComponentKind<T>.typeID >= 0 ? NativeEntityAPI.FindManyByComponent(ComponentKind<T>.typeID) : Array.Empty<GameObject>();
    }
}
