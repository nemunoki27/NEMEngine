using NEMEngine;

namespace NEMTests.Scripts;

[ScriptTypeID("ca8b8586-623a-40b1-9e72-444019040172")]
public sealed class TextCharAnimationAPIProbe : MonoBehaviour {

	[SerializeField, SerializedFieldID("39beccc7-e4c3-448d-bf96-b345a3ac0d45")] private bool passed;
	[SerializeField, SerializedFieldID("dc999f78-45e2-41e5-b518-bc370a493cda")] private string error = string.Empty;

	private void Start() {

		try {
			var source = GameObject.Find("AnimationSource") ?? throw new InvalidOperationException("参照元がありません");
			var animation = gameObject.AddComponent<TextCharAnimation>();
			var scalar = GameObject.Find("ScalarSource") ?? throw new InvalidOperationException("スカラー参照がありません");
			var assets = source.GetComponent<TextCharAnimation>()!;
			if (assets.ScaleSource is not Prefab prefab || assets.ColorSource is not AnimationClip clip)
				throw new InvalidOperationException("アセット参照の取得に失敗しました");
			animation.PlayOnStart = false;
			animation.Order = TextCharAnimationOrder.CenterToEnds;
			animation.Lines = TextCharAnimationLines.PerLine;
			animation.Timing = TextCharAnimationTiming.TotalDuration;
			animation.Repeat = TextCharAnimationRepeat.LoopCharacters;
			animation.Interval = 0.15f;
			animation.TotalDuration = 2.5f;
			animation.Easing = EasingType.EaseOutQuad;
			animation.Seed = 456;
			animation.TranslationSource = source;
			animation.ScaleSource = prefab;
			animation.ColorSource = clip;
			animation.RotationSource = scalar;
			animation.TimeSource = new TimeSourceSettings { Mode = TimeSourceMode.Unscaled };
			if (animation.TranslationSource != source || animation.RotationSource != scalar ||
				animation.ColorSource != clip || animation.ScaleSource != prefab ||
				animation.TimeSource.Mode != TimeSourceMode.Unscaled || animation.Seed != 456 ||
				animation.Interval != 0.15f || animation.TotalDuration != 2.5f ||
				animation.Easing != EasingType.EaseOutQuad || animation.Order != TextCharAnimationOrder.CenterToEnds ||
				animation.Repeat != TextCharAnimationRepeat.LoopCharacters || animation.Timing != TextCharAnimationTiming.TotalDuration ||
				animation.Lines != TextCharAnimationLines.PerLine || animation.PlayOnStart)
				throw new InvalidOperationException("設定の読み戻しに失敗しました");
			animation.ColorSource = scalar;
			animation.ScaleSource = source;
			animation.TranslationSource = prefab;
			if (animation.ColorSource != scalar || animation.TranslationSource != prefab || animation.ScaleSource != source)
				throw new InvalidOperationException("参照種別の変更に失敗しました");
			try {
				animation.TranslationSource = clip;
				throw new ApplicationException("移動トラックのないクリップを受け付けました");
			} catch (InvalidOperationException) { }
			if (animation.TranslationSource != prefab) throw new InvalidOperationException("無効な参照で上書きされました");
			animation.Play();
			animation.Pause();
			animation.Resume();
			animation.Seek(1.25f);
			animation.Stop();
			animation.RotationSource = null;
			animation.TranslationSource = null;
			if (animation.RotationSource != null || animation.TranslationSource != null || animation.ColorSource != scalar)
				throw new InvalidOperationException("参照の解除に失敗しました");
			passed = true;
		} catch (Exception exception) { error = exception.ToString(); }
	}
}
