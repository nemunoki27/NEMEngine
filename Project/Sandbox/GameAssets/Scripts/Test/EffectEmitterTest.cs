using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	EffectEmitterTest
//============================================================================
public sealed class EffectEmitterTest : ScriptBehaviour {

	[Label("グループ名")]
	[SerializeField]
	private string groupName = "Default";

	[Label("座標指定オフセット")]
	[SerializeField]
	private Vector3 emitAtOffset = new(2.0f, 0.0f, 0.0f);

	private EffectEmitter? effectEmitter;
	private Transform? emitterTransform;
	private EffectPlaybackHandle lastPlayback;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
		effectEmitter = GetComponent<EffectEmitter>();
		emitterTransform = GetComponent<Transform>();

		if (effectEmitter == null) {
			Debug.LogError("[EffectEmitterTest] EffectEmitterがありません");
			return;
		}

		Debug.Log("[EffectEmitterTest] 1:発生 2:グループ発生 3:座標指定発生 4:最後を停止 5:最後を消去 6:グループ停止 7:グループ消去 8:全消去 9:状態表示");
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {
		if (effectEmitter == null) {
			return;
		}

		if (Input.GetKeyDown(KeyCode.Alpha1)) {
			lastPlayback = effectEmitter.Emit();
			Debug.Log($"[EffectEmitterTest] 発生: {lastPlayback.IsValid}");
		}
		if (Input.GetKeyDown(KeyCode.Alpha2)) {
			lastPlayback = effectEmitter.Emit(groupName);
			Debug.Log($"[EffectEmitterTest] グループ発生({groupName}): {lastPlayback.IsValid}");
		}
		if (Input.GetKeyDown(KeyCode.Alpha3)) {
			Vector3 position = emitterTransform != null ? emitterTransform.position + emitAtOffset : emitAtOffset;
			Quaternion rotation = emitterTransform != null ? emitterTransform.rotation : Quaternion.identity;
			lastPlayback = effectEmitter.EmitAt(groupName, position, rotation);
			Debug.Log($"[EffectEmitterTest] 座標指定発生({groupName}): {lastPlayback.IsValid}");
		}
		if (Input.GetKeyDown(KeyCode.Alpha4)) {
			effectEmitter.Stop(lastPlayback);
			Debug.Log("[EffectEmitterTest] 最後の再生を停止");
		}
		if (Input.GetKeyDown(KeyCode.Alpha5)) {
			effectEmitter.Clear(lastPlayback);
			Debug.Log("[EffectEmitterTest] 最後の再生を消去");
		}
		if (Input.GetKeyDown(KeyCode.Alpha6)) {
			effectEmitter.Stop(groupName);
			Debug.Log($"[EffectEmitterTest] グループ停止({groupName})");
		}
		if (Input.GetKeyDown(KeyCode.Alpha7)) {
			effectEmitter.Clear(groupName);
			Debug.Log($"[EffectEmitterTest] グループ消去({groupName})");
		}
		if (Input.GetKeyDown(KeyCode.Alpha8)) {
			effectEmitter.Clear();
			Debug.Log("[EffectEmitterTest] 全消去");
		}
		if (Input.GetKeyDown(KeyCode.Alpha9)) {
			Debug.Log($"[EffectEmitterTest] 最後={effectEmitter.IsPlaying(lastPlayback)} グループ={effectEmitter.IsPlaying(groupName)} 全体={effectEmitter.IsAnyPlaying}");
		}
	}
}
