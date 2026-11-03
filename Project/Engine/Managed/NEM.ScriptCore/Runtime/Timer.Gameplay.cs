namespace NEMEngine;

public sealed partial class Timer {

    // 開始時は現在値を0へ戻す
    public void Play() => ControlRequest = 1;
    public void Start() => Play();
    public void Pause() => ControlRequest = 2;
    public void Resume() => ControlRequest = 3;
    public void Stop() => ControlRequest = 4;
    public void Reset() => ControlRequest = 5;

    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 71);
        set => ComponentTimeSource.Apply(native, 71, value);
    }
}
