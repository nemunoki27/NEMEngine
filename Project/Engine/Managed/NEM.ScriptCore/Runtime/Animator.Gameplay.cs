namespace NEMEngine;

// Controllerの状態再生と直接Clip再生を操作する
public sealed partial class Animator {

    public void Play(string stateName, int layer = -1, float normalizedTime = float.NegativeInfinity) {

        ValidateLayer(layer);
        ValidateTime(normalizedTime, true);
        SetPlaybackRequest(stateName, 0.0f, false, normalizedTime, true);
    }

    public void CrossFade(string stateName, float normalizedTransitionDuration, int layer = -1,
        float normalizedTimeOffset = float.NegativeInfinity) {

        ValidateLayer(layer);
        ValidateDuration(normalizedTransitionDuration);
        ValidateTime(normalizedTimeOffset, true);
        SetPlaybackRequest(stateName, normalizedTransitionDuration, true, normalizedTimeOffset, true);
    }

    public void CrossFadeInFixedTime(string stateName, float fixedTransitionDuration, int layer = -1,
        float fixedTimeOffset = 0.0f) {

        ValidateLayer(layer);
        ValidateDuration(fixedTransitionDuration);
        ValidateTime(fixedTimeOffset, false);
        SetPlaybackRequest(stateName, fixedTransitionDuration, false, fixedTimeOffset, false);
    }

    // Controllerの構成を変えずに単独Clipを再生する
    public void PlayClip(AnimationClip clip, float normalizedTime = 0.0f) {

        ArgumentNullException.ThrowIfNull(clip);
        ValidateTime(normalizedTime, false);
        NormalizedOffset = true;
        PlaybackTimeRequest = normalizedTime;
        DirectClipRequest = clip;
    }

    // 移動した区間のAnimation Eventは発火しない
    public void Seek(float normalizedTime) {

        ValidateTime(normalizedTime, false);
        NormalizedOffset = true;
        PlaybackTimeRequest = normalizedTime;
        SeekRequest = true;
    }

    public void Stop() {

        StopRequest = true;
    }

    public bool IsPlaying => IsPlayingValue;

    public AnimatorStateInfo GetCurrentAnimatorStateInfo(int layerIndex) {

        ValidateLayer(layerIndex);
        return new AnimatorStateInfo(CurrentGroup, NormalizedTimeValue, ClipDuration, Looping, speed);
    }

    private void SetPlaybackRequest(string stateName, float duration, bool normalizedDuration, float offset, bool normalizedOffset) {

        ArgumentException.ThrowIfNullOrEmpty(stateName);
        // 要求名を最後に設定し、時刻と遷移設定を一組で消費させる
        PlayFade = duration;
        NormalizedFade = normalizedDuration;
        NormalizedOffset = normalizedOffset;
        PlaybackTimeRequest = offset;
        PlayRequest = stateName;
    }

    private static void ValidateLayer(int layer) {

        // 現在のControllerは単一Layerを使う
        if (layer != -1 && layer != 0) throw new ArgumentOutOfRangeException(nameof(layer));
    }

    private static void ValidateTime(float value, bool allowCurrent) {

        if (allowCurrent && float.IsNegativeInfinity(value)) return;
        if (!float.IsFinite(value) || value < 0.0f) throw new ArgumentOutOfRangeException(nameof(value));
    }

    private static void ValidateDuration(float value) {

        if (!float.IsFinite(value) || value < 0.0f) throw new ArgumentOutOfRangeException(nameof(value));
    }
}
