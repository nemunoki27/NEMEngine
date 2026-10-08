namespace NEMEngine;

// 現在の状態名と再生時刻を保持する
public readonly struct AnimatorStateInfo {

    private readonly string stateName;
    public float normalizedTime { get; }
    public float length { get; }
    public bool loop { get; }
    public float speed { get; }

    internal AnimatorStateInfo(string name, float time, float duration, bool looping, float playbackSpeed) {

        stateName = name;
        normalizedTime = time;
        length = duration;
        loop = looping;
        speed = playbackSpeed;
    }

    public bool IsName(string name) {

        return string.Equals(stateName, name, StringComparison.Ordinal);
    }
}
