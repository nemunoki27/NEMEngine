using NEMEngine;

public sealed class RockSurface : MonoBehaviour {

    [SerializeField] private int patternSeed = 27;

    void Start() {
        // 岩肌の向きと配置をオブジェクトごとに変える
        var tiling = GetComponent<HexTiling>();
        if (tiling == null) return;
        tiling.TileSize = 1.5f;
        tiling.RandomRotation = 55.0f;
        tiling.Seed = patternSeed;
    }
}
