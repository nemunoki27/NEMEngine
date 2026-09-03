namespace NEMEngine;

// Prefabアセット参照。Instantiateは子階層とcomponentとscriptを実体化してからルートEntityを返す。
// prefab-localな参照のinstanceへの再マップはPrefabSystemのsourceLocalToEntity / prefabInstanceIDが担う。
[NativeAssetType("Prefab")]
public sealed class Prefab : Asset {

    internal Prefab(AssetGUID id) : base(id) { }

    public Entity Instantiate() {
        return NativeApi.SpawnPrefab(id, Vector3.zero, Quaternion.identity, false, Entity.nullEntity);
    }

    public Entity Instantiate(Vector3 position, Quaternion rotation) {
        return NativeApi.SpawnPrefab(id, position, rotation, true, Entity.nullEntity);
    }

    public Entity Instantiate(Vector3 position, Quaternion rotation, Entity parent) {
        return NativeApi.SpawnPrefab(id, position, rotation, true, parent);
    }
}
