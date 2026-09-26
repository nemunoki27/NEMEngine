namespace NEMEngine;

// Prefabアセット参照。Instantiateは子階層とcomponentとscriptを実体化してからルートGameObjectを返す。
// prefab-localな参照のinstanceへの再マップはPrefabSystemのsourceLocalToEntity / prefabInstanceIDが担う。
[NativeAssetType("Prefab")]
public sealed class Prefab : Asset {

    internal Prefab(AssetGUID id) : base(id) { }

    public GameObject? Instantiate() {
        return NativeEntityAPI.SpawnPrefab(id, Vector3.zero, Quaternion.identity, false, null);
    }

    public GameObject? Instantiate(Vector3 position, Quaternion rotation) {
        return NativeEntityAPI.SpawnPrefab(id, position, rotation, true, null);
    }

    public GameObject? Instantiate(Vector3 position, Quaternion rotation, GameObject? parent) {
        return NativeEntityAPI.SpawnPrefab(id, position, rotation, true, parent);
    }
}
