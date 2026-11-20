using NEMEngine;

namespace NEMTests.Scripts;

[ScriptTypeID("08413d5b-d32d-4b30-acae-05a8dffb4438")]
public sealed class BeamAPIProbe : MonoBehaviour {

	[SerializeField, SerializedFieldID("06480fab-cb1d-4a63-9951-749fcd294058")] private bool passed;
	[SerializeField, SerializedFieldID("475d100d-755d-45b0-b4b0-b05421caf70f")] private string error = string.Empty;

	private void Start() {

		try {
			var beam = gameObject.AddComponent<BeamRenderer>();
			var target = GameObject.Find("RuntimeTarget") ?? throw new InvalidOperationException("追従先がありません");
			var wall = GameObject.Find("BeamWall") ?? throw new InvalidOperationException("壁がありません");

			var other = target.GetComponent<BeamRenderer>()!.MaterialInstance;
			var instance = beam.MaterialInstance;
			foreach (var (name, color) in new[] { ("startColor", Color4.red), ("targetColor", Color4.blue), ("emissiveColor", Color4.green) }) {
				if (!instance.SetColor(name, color) || !instance.TryGetColor(name, out var actual) || !actual.Equals(color) ||
					!other.TryGetColor(name, out var unchanged) || !unchanged.Equals(Color4.white) ||
					!instance.Clear(name) || !instance.TryGetColor(name, out actual) || !actual.Equals(Color4.white))
					throw new InvalidOperationException(name + "の変更または解除に失敗しました");
			}
			if (!instance.SetFloat("emissiveIntensity", 2.5f) || !instance.TryGetFloat("emissiveIntensity", out var strength) || strength != 2.5f ||
				!other.TryGetFloat("emissiveIntensity", out strength) || strength != 0 ||
				!instance.Clear("emissiveIntensity") || !instance.TryGetFloat("emissiveIntensity", out strength) || strength != 0)
				throw new InvalidOperationException("発光強度の変更または解除に失敗しました");
			if (!other.TryGetTexture("baseColorTexture", out var texture) || texture == null ||
				!instance.SetTexture("baseColorTexture", texture) || !instance.TryGetTexture("baseColorTexture", out var actualTexture) || actualTexture != texture ||
				!instance.Clear("baseColorTexture") || !instance.TryGetTexture("baseColorTexture", out actualTexture) || actualTexture != null ||
				!other.TryGetTexture("baseColorTexture", out actualTexture) || actualTexture != texture)
				throw new InvalidOperationException("テクスチャの変更または解除に失敗しました");

			// 保存用IDのないオブジェクトも端点と経由点に使う
			beam.Start = BeamEndpoint.Local(Vector3.zero);
			beam.End = BeamEndpoint.OnObject(target, new Vector3(8, 0, 0));
			beam.AddWaypoint(BeamEndpoint.World(new Vector3(2, 0, 0)));
			beam.AddWaypoint(beam.End);
			beam.MoveWaypoint(1, 0);
			beam.Width = 0.25f;
			if (beam.Width != 0.25f || beam.WaypointCount != 2 || beam.GetWaypoint(0).Object != target || beam.End.Object != target)
				throw new InvalidOperationException("設定変更で追従先が失われました");
			beam.RemoveWaypoint(0);
			beam.SetWaypoint(0, BeamEndpoint.World(new Vector3(1, 0, 0)));

			// 2番目の区間で球の手前の面に当たる
			beam.StopOnHit = true;
			beam.Play();
			if (!beam.IsPlaying || !beam.TryGetHit(out var hit, out int segment) || segment != 1 ||
				hit.gameObject != wall || hit.point != new Vector3(3.5f, 0, 0) || hit.normal != new Vector3(-1, 0, 0))
				throw new InvalidOperationException("ビームの命中位置、法線、区間が一致しません");
			beam.Stop();
			if (beam.IsPlaying || beam.TryGetHit(out _, out _)) throw new InvalidOperationException("停止後も命中情報が残っています");
			passed = true;
		} catch (Exception exception) { error = exception.ToString(); }
	}
}
