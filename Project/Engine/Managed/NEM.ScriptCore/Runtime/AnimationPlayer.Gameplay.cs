namespace NEMEngine;

// 自動生成される AnimationPlayer wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// state名で再生し CrossFade でクロスフェードする。副作用は system が要求フィールドの立ち上がりで処理する。
public sealed partial class AnimationPlayer {

    // 指定 state へ即時に切り替えて再生する。
    public void Play(string state) {
        PlayFade = 0.0f;
        PlayRequest = state;
    }

    // 指定 state へ時間をかけてクロスフェード再生する。
    public void CrossFade(string state, float fadeDuration) {
        PlayFade = fadeDuration;
        PlayRequest = state;
    }

    // 再生を止める（時間進行を停止し現在のポーズを保持する）。
    public void Stop() {
        StopRequest = true;
    }

    // 再生中か。
    public bool IsPlaying => IsPlayingValue;
}
