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
}
