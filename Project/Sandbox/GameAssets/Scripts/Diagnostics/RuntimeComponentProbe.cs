using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RuntimeComponentProbe class
//	キー操作で実行中のコンポーネント追加と削除を確認する
//============================================================================
[ScriptTypeID("572ec3fb-e7a9-4446-8521-3941f5d58533")]
public sealed class RuntimeComponentProbe : MonoBehaviour {

	private GameObject? target;
	private AudioSource? previousAudio;
	private RuntimeLifecycleProbe? previousScript;

	private void Start() {

		// 元のシーンに触れず確認用の対象を作る
		target = new GameObject("RuntimeComponentProbe.Target");
		Debug.Log("追加削除確認: F6=AudioSource追加 F7=削除 F8=Script追加 F9=削除 F10=状態表示");
	}

	private void Update() {

		if (target == null) {
			return;
		}
		if (Input.GetKeyDown(KeyCode.F6) && target.GetComponent<AudioSource>() == null) {

			// 追加直後の値が構造変更後も保持されるか確認する
			AudioSource audio = target.AddComponent<AudioSource>();
			audio.Volume = 0.25f;
			Debug.Log("AudioSourceを追加しました Volume=0.25");
		}
		if (Input.GetKeyDown(KeyCode.F7)) {
			previousAudio = target.GetComponent<AudioSource>();
			Destroy(previousAudio);
		}
		if (Input.GetKeyDown(KeyCode.F8) && target.GetComponent<RuntimeLifecycleProbe>() == null) {
			RuntimeLifecycleProbe script = target.AddComponent<RuntimeLifecycleProbe>();
			script.testValue = 17.0f;
			Debug.Log("Scriptを追加しました testValue=17");
		}
		if (Input.GetKeyDown(KeyCode.F9)) {
			previousScript = target.GetComponent<RuntimeLifecycleProbe>();
			Destroy(previousScript);
		}
		if (Input.GetKeyDown(KeyCode.F10)) {

			// 削除済みの参照と新しい個体の状態を照合する
			AudioSource? current = target.GetComponent<AudioSource>();
			Debug.Log($"AudioSource有効={current != null} Volume={current?.Volume} 旧参照有効={previousAudio != null}");
			Debug.Log($"Script有効={target.GetComponent<RuntimeLifecycleProbe>() != null} 旧参照有効={previousScript != null}");
		}
	}

	private void OnDestroy() {

		// 確認用オブジェクトを残さない
		if (target != null) {
			Destroy(target);
		}
	}
}
