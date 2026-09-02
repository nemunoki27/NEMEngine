using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	ControlIrisTransition
//============================================================================
public sealed class ControlIrisTransition : ScriptBehaviour {

	private const string PassName = "irisTransition";
	private const string ThresholdName = "Threshold";
	private static readonly MaterialParameterID ThresholdID =
		MaterialParameterID.FromHex("94d20ddddf0f9c93");

	[SerializeField]
	private bool effectEnabled = true;

	[Range(0.0f, 1.0f)]
	[SerializeField]
	private float threshold = 0.5f;

	private RenderFeaturePass pass;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		pass = RenderFeatures.FindPass(PassName);
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (!pass.isValid) {
			pass = RenderFeatures.FindPass(PassName);
		}
		if (!pass.isValid) {
			return;
		}
		pass.SetEnabled(effectEnabled);
		pass.SetFloat(ThresholdID, ThresholdName, threshold);
	}

	//========================================================================
	//	無効化時処理
	//========================================================================
	public override void OnDisable() {

		if (pass.isValid) {
			pass.Reset();
		}
	}
}