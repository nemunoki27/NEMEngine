using NEMEngine;

public sealed class ScoreText : MonoBehaviour {

    private TextRenderer? label;
    private int score;

    void Start() {
        // 表示先を取得して初期値を描画
        label = GetComponent<TextRenderer>();
        Refresh();
    }

    public void AddScore(int amount) {
        // アイテム取得や敵の撃破時に得点を加える
        score += amount;
        Refresh();
    }

    void Refresh() {
        // 値が変わったときだけ文字列を更新
        if (label != null) label.Text = $"SCORE: {score}";
    }
}
