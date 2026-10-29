using NEMEngine;

public sealed class RuntimeGraphics : MonoBehaviour {

    void Start() {
        // 現在の設定を取得して必要な項目だけ変更
        var settings = GraphicsSettings.GetSettings();
        settings.rayTracingShadows = true;
        settings.doubleBuffering = true;
        settings.clearColor = new Color4(0.05f, 0.08f, 0.12f, 1.0f);
        // Snapshotを実行設定へ戻し、FPS上限を指定
        GraphicsSettings.ApplySettings(settings);
        Application.targetFrameRate = 60;
    }
}
