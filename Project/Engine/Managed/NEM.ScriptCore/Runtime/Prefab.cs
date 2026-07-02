namespace NEMEngine;

// Prefabアセット参照。実体化はWorldCommandBuffer経由(callback中の即時ECS mutationはしない)。
// ルートEntityは即時予約して返し、PrefabSystemによる実体化(component付与)は次のflushで行われる。
// prefab-localな参照のinstanceへの再マップはPrefabSystemのsourceLocalToEntity / prefabInstanceIDが担う。
// 返り値のEntityの可視性規則はWorld.CreateEntityと同じ(flush前は予約済み、flush後に通常Entity)。
[NativeAssetType("Prefab")]
public sealed class Prefab : Asset {

    internal Prefab(UUID id) : base(id) { }

    public Entity Instantiate() {
        return NativeApi.SpawnPrefab(id.value, Vector3.zero, Quaternion.identity, false, Entity.nullEntity);
    }

    public Entity Instantiate(Vector3 position, Quaternion rotation) {
        return NativeApi.SpawnPrefab(id.value, position, rotation, true, Entity.nullEntity);
    }

    public Entity Instantiate(Vector3 position, Quaternion rotation, Entity parent) {
        return NativeApi.SpawnPrefab(id.value, position, rotation, true, parent);
    }
}
