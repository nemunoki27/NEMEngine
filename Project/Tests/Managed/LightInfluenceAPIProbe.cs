using NEMEngine;

namespace NEMTests.Scripts;

[ScriptTypeID("2b940d5c-54f4-4dd0-b979-1784c44278e1")]
public sealed class LightInfluenceAPIProbe : MonoBehaviour {

	[SerializeField, SerializedFieldID("64c04024-a95f-46a5-b3ec-880a669f1a80")]
	private bool passed;
	[SerializeField, SerializedFieldID("82c446a6-392c-449a-a8a3-c69e0bf587b4")]
	private string error = string.Empty;

	private void Start() {

		try {
			var target = GameObject.Find("Emitter") ?? throw new InvalidOperationException("発光元がありません");
			var directional = gameObject.AddComponent<DirectionalLight>();
			Check(target, value => directional.RenderingLayerMask = value, () => directional.RenderingLayerMask,
				value => directional.ShadowExcludedLayerMask = value, () => directional.ShadowExcludedLayerMask,
				directional.SetShadowExclusion, directional.RemoveShadowExclusion, directional.ClearShadowExclusions,
				directional.GetShadowExclusions);
			var point = gameObject.AddComponent<PointLight>();
			Check(target, value => point.RenderingLayerMask = value, () => point.RenderingLayerMask,
				value => point.ShadowExcludedLayerMask = value, () => point.ShadowExcludedLayerMask,
				point.SetShadowExclusion, point.RemoveShadowExclusion, point.ClearShadowExclusions, point.GetShadowExclusions);
			var spot = gameObject.AddComponent<SpotLight>();
			Check(target, value => spot.RenderingLayerMask = value, () => spot.RenderingLayerMask,
				value => spot.ShadowExcludedLayerMask = value, () => spot.ShadowExcludedLayerMask,
				spot.SetShadowExclusion, spot.RemoveShadowExclusion, spot.ClearShadowExclusions, spot.GetShadowExclusions);
			var rect = gameObject.AddComponent<RectLight>();
			Check(target, value => rect.RenderingLayerMask = value, () => rect.RenderingLayerMask,
				value => rect.ShadowExcludedLayerMask = value, () => rect.ShadowExcludedLayerMask,
				rect.SetShadowExclusion, rect.RemoveShadowExclusion, rect.ClearShadowExclusions, rect.GetShadowExclusions);
			passed = true;
		} catch (Exception exception) { error = exception.ToString(); }
	}

	// 生成Bindingと複合設定の実橋を同じScriptから操作する
	private static void Check(GameObject target, Action<uint> setLayers, Func<uint> getLayers,
		Action<uint> setExcludedLayers, Func<uint> getExcludedLayers, Action<GameObject, bool> setExclusion,
		Action<GameObject> removeExclusion, Action clear, Func<IReadOnlyList<LightShadowExclusion>> read) {

		setLayers(3u);
		setExcludedLayers(2u);
		if (getLayers() != 3u || getExcludedLayers() != 2u) throw new InvalidOperationException("レイヤー変更が反映されません");
		setExclusion(target, false);
		setExclusion(target, true);
		var entries = read();
		if (entries.Count != 1 || entries[0].Target != target || !entries[0].IncludeChildren)
			throw new InvalidOperationException("除外対象の追加または更新が反映されません");
		removeExclusion(target);
		if (read().Count != 0) throw new InvalidOperationException("除外対象の削除が反映されません");
		setExclusion(target, false);
		clear();
		if (read().Count != 0 || getExcludedLayers() != 2u) throw new InvalidOperationException("除外対象のクリアが反映されません");
	}
}
