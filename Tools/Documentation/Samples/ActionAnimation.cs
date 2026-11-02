using NEMEngine;

public sealed class ActionAnimation : MonoBehaviour {

    private AnimationClipPlayer? player;

    void Start() {
        // 同じGameObjectのクリップ再生コンポーネントを取得
        player = GetComponent<AnimationClipPlayer>();
    }

    void Update() {
        // Eキーを押すとInspectorで設定したクリップを先頭から再生
        if (player != null && Input.GetKeyDown(KeyCode.E)) player.Play();
    }
}
