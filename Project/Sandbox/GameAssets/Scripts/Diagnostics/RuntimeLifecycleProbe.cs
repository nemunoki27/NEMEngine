using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RuntimeLifecycleProbe class
//	追加から削除までの呼出順をログに残す
//============================================================================
[ScriptTypeID("eed5af54-963a-4b78-81f0-b6106e6737e1")]
public sealed class RuntimeLifecycleProbe : MonoBehaviour {

	[SerializedFieldID("bda98a95-8333-4c86-b4d1-0320f6e3032a")]
	public float testValue = 3.0f;
	[SerializedFieldID("6ae09a0e-2e17-4405-ae7c-2f39a2d7d39b")]
	public int lifecycleMask;

	private void Awake() {
		lifecycleMask |= 1;
		Debug.Log("RuntimeLifecycleProbe.Awake");
	}

	private void OnEnable() {
		lifecycleMask |= 2;
		Debug.Log("RuntimeLifecycleProbe.OnEnable");
	}

	private void Start() {
		lifecycleMask |= 4;
		Debug.Log($"RuntimeLifecycleProbe.Start testValue={testValue}");
	}

	private void OnDisable() {
		lifecycleMask |= 8;
		Debug.Log("RuntimeLifecycleProbe.OnDisable");
	}

	private void OnDestroy() {
		lifecycleMask |= 16;
		Debug.Log("RuntimeLifecycleProbe.OnDestroy");
	}
}
