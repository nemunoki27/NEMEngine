using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RenderFeatureControlExample
//============================================================================
public sealed class RenderFeatureControlExample : ScriptBehaviour {

	private const string ThresholdName = "Threshold";
	private static readonly MaterialParameterID ThresholdID =
		MaterialParameterID.FromHex("94d20ddddf0f9c93");

	[SerializeField]
	private string passName = "irisTransition";

	[SerializeField]
	private bool passEnabled = true;

	[Range(0.0f, 1.0f)]
	[SerializeField]
	private float threshold = 0.5f;

	[SerializeField]
	private string customComputePassName = "Gray";

	[SerializeField]
	private bool customComputeEnabled = true;

	[Range(0.0f, 1.0f)]
	[SerializeField]
	private float customComputeStrength = 1.0f;

	private RenderFeaturePass shaderGraphPass;
	private RenderFeaturePass customComputePass;
	private bool missingShaderGraphPassLogged = false;
	private bool missingCustomComputePassLogged = false;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		ResolvePass();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (!shaderGraphPass.isValid || !customComputePass.isValid) {
			ResolvePass();
		}
		if (shaderGraphPass.isValid) {
			shaderGraphPass.SetEnabled(passEnabled);
			shaderGraphPass.SetFloat(
				ThresholdID, ThresholdName, threshold);
		}

		// 自作.CS.hlslはcbufferの変数名をそのまま指定できる
		if (customComputePass.isValid) {
			customComputePass.SetEnabled(customComputeEnabled);
			customComputePass.SetFloat(
				"strength", customComputeStrength);
		}
	}

	//========================================================================
	//	無効化時処理
	//========================================================================
	public override void OnDisable() {

		if (shaderGraphPass.isValid) {
			shaderGraphPass.Reset();
		}
		if (customComputePass.isValid) {
			customComputePass.Reset();
		}
	}

	//========================================================================
	//	パス解決処理
	//========================================================================
	private void ResolvePass() {

		shaderGraphPass = RenderFeatures.FindPass(passName);
		if (shaderGraphPass.isValid) {
			missingShaderGraphPassLogged = false;
		} else if (!missingShaderGraphPassLogged) {
			Debug.LogWarning(
				$"レンダー機能パスが見つかりません name={passName}");
			missingShaderGraphPassLogged = true;
		}

		customComputePass = RenderFeatures.FindPass(customComputePassName);
		if (customComputePass.isValid) {
			missingCustomComputePassLogged = false;
		} else if (!missingCustomComputePassLogged) {
			Debug.LogWarning(
				$"レンダー機能パスが見つかりません name={customComputePassName}");
			missingCustomComputePassLogged = true;
		}
	}
}
