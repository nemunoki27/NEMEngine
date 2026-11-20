using NEMEngine;

namespace NEMTests.Scripts;

[ScriptTypeID("a05a06b8-d792-4eb1-b786-0933380787b8")]
public sealed class ShineHighlightAPIProbe : MonoBehaviour {

	[SerializeField, SerializedFieldID("d6705f15-b1bf-4781-8959-c91772804054")] private bool passed;
	[SerializeField, SerializedFieldID("70895dae-b297-4cd5-aba6-9454b9a86185")] private string error = string.Empty;

	private void Start() {

		try {
			var shine = gameObject.AddComponent<ShineHighlight>();
			shine.Enabled = false;
			shine.Position = 0.375f;
			shine.Angle = -45.0f;
			shine.Color = new Color4(0.25f, 0.5f, 0.125f, 1.0f);
			shine.Intensity = 2.5f;
			shine.Width = 0.25f;
			shine.Softness = 0.75f;
			var ramp = GameObject.Find("RampSource")?.GetComponent<ShineHighlight>()?.RampTexture
				?? throw new InvalidOperationException("ランプテクスチャを取得できません");
			shine.RampTexture = ramp;
			var actual = gameObject.GetComponent<ShineHighlight>()!;
			if (actual.Enabled || actual.Position != 0.375f || actual.Angle != -45.0f ||
				!actual.Color.Equals(new Color4(0.25f, 0.5f, 0.125f, 1.0f)) || actual.RampTexture != ramp || actual.Intensity != 2.5f ||
				actual.Width != 0.25f || actual.Softness != 0.75f) throw new InvalidOperationException("設定が一致しません");
			shine.Enabled = true;
			shine.RampTexture = null;
			passed = actual.Enabled && actual.RampTexture == null;
		} catch (Exception ex) { error = ex.ToString(); }
	}
}
