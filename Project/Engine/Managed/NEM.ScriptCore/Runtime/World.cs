namespace NEMEngine;

// gameplay code から新しい Entity を生成する。生成は WorldCommandBuffer 経由（structural mutation は遅延）。
public static class World {

    // 空 Entity を即時予約して返す。Transform/SceneObject/Name の付与と親付けは次の flush で適用される。
    // 可視性規則:
    //   flush 前: 返り値の Entity は isAlive=true（予約済み）。ただし component はまだ無いため
    //             Has<T>()=false、Get<T>() は例外、Transform の get は既定値、set は staging される。
    //             Name は flush まで空。
    //   flush 後: 通常 Entity と同じ。lifecycle(script)は次の SynchronizeLifecycle から開始する。
    public static Entity CreateEntity(string? name = null) {
        return NativeApi.SpawnEntity(name, Entity.nullEntity);
    }

    public static Entity CreateEntity(string? name, Entity parent) {
        return NativeApi.SpawnEntity(name, parent);
    }

    //========================================================================
    //	検索（アクティブワールド全体のO(n)走査。頻繁に呼ぶ場合は結果をキャッシュ推奨）
    //========================================================================

    // 名前が一致する最初のEntityを返す、未発見はnull Entity
    public static Entity Find(string name) => NativeApi.FindByName(name);

    // タグが一致する最初のEntityを返す、未発見はnull Entity
    public static Entity FindWithTag(string tag) => NativeApi.FindByTag(tag);

    // タグが一致する全Entityを返す
    public static Entity[] FindEntitiesWithTag(string tag) => NativeApi.FindManyByTag(tag);

    // 指定型のスクリプトを1つ返す、未発見はnull。Unityの FindObjectOfType 相当
    public static T? FindObjectOfType<T>() where T : ScriptBehaviour => HostBridge.FindScriptOfType<T>();

    // 指定型のスクリプトを全て返す
    public static T[] FindObjectsOfType<T>() where T : ScriptBehaviour => HostBridge.FindScriptsOfType<T>();

    // 指定componentを持つ最初のEntityを返す、未発見はnull Entity
    public static Entity FindEntityWithComponent<T>() where T : struct, IComponentRef<T>
        => NativeApi.FindByComponent(ComponentType<T>.Id);

    // 指定componentを持つ全Entityを返す
    public static Entity[] FindEntitiesWithComponent<T>() where T : struct, IComponentRef<T>
        => NativeApi.FindManyByComponent(ComponentType<T>.Id);
}
