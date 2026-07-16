using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RendererColor
//============================================================================
public sealed class RendererColor : ScriptBehaviour {
	private SpriteRenderer? sprite;
	private TextRenderer? text;
	private float elapsedTime;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
		sprite = GetComponent<SpriteRenderer>();
		text = GetComponent<TextRenderer>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {
		elapsedTime += Time.DeltaTime;

		float red = Math.Sin(elapsedTime) * 0.5f + 0.5f;
		float green = Math.Sin(elapsedTime + Math.pi * 2.0f / 3.0f) * 0.5f + 0.5f;
		float blue = Math.Sin(elapsedTime + Math.pi * 4.0f / 3.0f) * 0.5f + 0.5f;
		float alpha = Math.Sin(elapsedTime * 0.5f) * 0.5f + 0.5f;
		Color4 color = new(red, green, blue, alpha);

		sprite?.SetColor(color);
		text?.SetColor(color);
	}
}
