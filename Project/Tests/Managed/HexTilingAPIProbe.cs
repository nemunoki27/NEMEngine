using NEMEngine;

namespace NEMTests.Scripts;

[ScriptTypeID("a1f589cb-c9be-4122-8c2b-2acc3dab4eab")]
public sealed class HexTilingAPIProbe : MonoBehaviour {

	[SerializeField, SerializedFieldID("5193fde1-8360-4618-a568-fbaa273448b0")] private bool passed;
	[SerializeField, SerializedFieldID("6a55be91-44d3-4b5a-a14d-365d0aea59f6")] private string error = string.Empty;

	private void Start() {

		try {
			var target = GameObject.Find("HexMesh") ?? throw new InvalidOperationException("検証用モデルがありません");
			var tiling = target.AddComponent<HexTiling>();
			tiling.Enabled = false;
			tiling.TileSize = 2.5f;
			tiling.RandomRotation = 90;
			tiling.Seed = -15;
			tiling.SetSubMeshEnabled(1, false);
			if (tiling.Enabled || tiling.TileSize != 2.5f || tiling.RandomRotation != 90 || tiling.Seed != -15 ||
				!tiling.IsSubMeshEnabled(0) || tiling.IsSubMeshEnabled(1)) throw new InvalidOperationException("設定を反映できません");
			tiling.SetSubMeshEnabled(1, true);
			tiling.Enabled = true;
			passed = tiling.IsSubMeshEnabled(1) && tiling.Enabled;
		} catch (Exception ex) {
			error = ex.ToString();
		}
	}
}
