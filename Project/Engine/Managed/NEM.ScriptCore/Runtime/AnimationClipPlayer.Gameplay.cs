namespace NEMEngine;

public sealed partial class AnimationClipPlayer {

    public void Play() => ControlRequest = 1;
    public void Pause() => ControlRequest = 2;
    public void Resume() => ControlRequest = 3;
    public void Stop() => ControlRequest = 4;
    public void Reset() => ControlRequest = 5;

    public void Seek(float seconds) {
        if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        SeekTimeRequest = seconds;
        ControlRequest = 6;
    }

    public void Play(AnimationClip clip) {
        ArgumentNullException.ThrowIfNull(clip);
        Clip = clip;
        Play();
    }
}
