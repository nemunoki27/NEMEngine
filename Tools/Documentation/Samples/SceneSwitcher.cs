using NEMEngine;

public sealed class SceneSwitcher : MonoBehaviour {

    [SerializeField] private SceneAsset? nextScene;

    void Update() {
        // Enterを押したframeだけ指定Sceneへ切り替える
        if (!Input.GetKeyDown(KeyCode.Return) || nextScene == null) return;
        SceneManager.LoadScene(nextScene);
    }
}
