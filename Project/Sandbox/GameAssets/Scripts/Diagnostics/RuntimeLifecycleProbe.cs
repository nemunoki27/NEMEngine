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
	[SerializedFieldID("9b7d3d9c-34cd-4898-b540-5dfd6cc1e243")]
	public bool throwInAwake;
	[SerializedFieldID("20da6d5a-9d30-4fde-a6a9-b37f9cf3b4ba")]
	public bool throwInUpdate;
	[SerializedFieldID("f1880865-e87f-4b96-985a-6639a1158053")]
	public int callbackUpdates;

	private void Awake() {
		lifecycleMask |= 1;
		Debug.Log("RuntimeLifecycleProbe.Awake");
		if (throwInAwake) { throw new System.InvalidOperationException("Awake interruption probe"); }
	}

	private void OnEnable() {
		lifecycleMask |= 2;
		Debug.Log("RuntimeLifecycleProbe.OnEnable");
	}

	private void Start() {
		lifecycleMask |= 4;
		Debug.Log($"RuntimeLifecycleProbe.Start testValue={testValue}");
	}

	private void Update() {

		++callbackUpdates;
		if (throwInUpdate) { throw new System.InvalidOperationException("Update interruption probe"); }
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
