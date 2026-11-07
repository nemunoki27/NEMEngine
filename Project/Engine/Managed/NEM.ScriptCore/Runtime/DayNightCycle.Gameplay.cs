namespace NEMEngine;

public sealed partial class DayNightCycle {

    // 再生開始時は設定された開始時刻へ戻す
    public void Play() => ControlRequest = 1;
    public void Pause() => ControlRequest = 2;
    public void Resume() => ControlRequest = 3;
    public void Stop() => ControlRequest = 4;
    public void Reset() => ControlRequest = 5;

    public void SetTime(float hour) {
        if (!float.IsFinite(hour)) throw new ArgumentOutOfRangeException(nameof(hour));
        if (FixedTime) StartHour = (hour % 24 + 24) % 24;
        SeekHour = hour;
        ControlRequest = 6;
    }

    public string CurrentTime {
        get {
            int seconds = (int)(CurrentHour * 3600) % 86400;
            return $"{seconds / 3600:00}:{seconds / 60 % 60:00}:{seconds % 60:00}";
        }
    }

    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 75);
        set => ComponentTimeSource.Apply(native, 75, value);
    }
}
