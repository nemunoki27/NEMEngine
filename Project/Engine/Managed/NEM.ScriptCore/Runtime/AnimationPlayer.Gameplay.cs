namespace NEMEngine;

// 自動生成される AnimationPlayer wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// グループ名で再生し CrossFade でグループ単位にクロスフェードする。副作用は system が要求フィールドの立ち上がりで処理する。
public sealed partial class AnimationPlayer {

    // 指定グループへ即時に切り替えて、そのグループ内クリップを同時再生する。
    public void Play(string group) {
        PlayFade = 0.0f;
        PlayRequest = group;
    }

    // 指定グループへ時間をかけてグループ単位でクロスフェード再生する。
    public void CrossFade(string group, float fadeDuration) {
        PlayFade = fadeDuration;
        PlayRequest = group;
    }

    // 再生を止める（時間進行を停止し現在のポーズを保持する）。
    public void Stop() {
        StopRequest = true;
    }

    // 再生中か。
    public bool IsPlaying => IsPlayingValue;
}
