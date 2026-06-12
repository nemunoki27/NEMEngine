namespace NEMEngine;

// Prefab を実体化する。生成は WorldCommandBuffer 経由（callback 中の即時 ECS mutation はしない）。
// ルート Entity は即時予約して返し、PrefabSystem による実体化(component 付与)は次の flush で行われる。
// prefab-local EntityRef / ScriptRef の instance への再マップは PrefabSystem の sourceLocalToEntity / prefabInstanceID が担う。
// 返り値の Entity の可視性規則は World.CreateEntity と同じ（flush 前は予約済み、flush 後に通常 Entity）。
public static class Prefab {

    public static Entity Instantiate(AssetRef<PrefabAsset> prefab) {
        return NativeApi.SpawnPrefab(prefab.id.value, Vector3.zero, Quaternion.identity, false, Entity.nullEntity);
    }

    public static Entity Instantiate(AssetRef<PrefabAsset> prefab, Vector3 position, Quaternion rotation) {
        return NativeApi.SpawnPrefab(prefab.id.value, position, rotation, true, Entity.nullEntity);
    }

    public static Entity Instantiate(AssetRef<PrefabAsset> prefab, Vector3 position, Quaternion rotation, Entity parent) {
        return NativeApi.SpawnPrefab(prefab.id.value, position, rotation, true, parent);
    }
}
