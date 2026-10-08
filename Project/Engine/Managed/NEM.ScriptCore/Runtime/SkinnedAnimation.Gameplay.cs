namespace NEMEngine;

// 自動生成されるSkinnedAnimationへ再生操作とRuntime状態を追加する
public sealed partial class SkinnedAnimation {

    // 指定クリップへ遷移再生する
    public void Play(string stateName) {
        NativePlaybackAPI.PlaySkinnedAnimationClip(native, stateName);
    }

    // クリップ状態を保持したまま時間更新を止める
    public void Stop() {
        Enabled = false;
    }

    // 現在の再生クリップ
    public string CurrentState =>
        NativePlaybackAPI.ReadSkinnedAnimationCurrentClip(native);

    // 現在の再生時間
    public float CurrentTime =>
        NativePlaybackAPI.ReadSkinnedAnimationRuntimeState(native).currentTime;

    // 非ループ再生が終端へ到達したか
    public bool Finished =>
        NativePlaybackAPI.ReadSkinnedAnimationRuntimeState(native).finished != 0;

    // クリップをブレンド中か
    public bool InTransition =>
        NativePlaybackAPI.ReadSkinnedAnimationRuntimeState(native).inTransition != 0;

    // ループした回数
    public int RepeatCount =>
        NativePlaybackAPI.ReadSkinnedAnimationRuntimeState(native).repeatCount;

    // 有効かつ自然終了していないか
    public bool IsPlaying => Enabled && !Finished;

    // 指定クリップの再生時間を返す
    public float GetDuration(string clipName) => NativePlaybackAPI.ReadSkinnedAnimationDuration(native, clipName);
}
