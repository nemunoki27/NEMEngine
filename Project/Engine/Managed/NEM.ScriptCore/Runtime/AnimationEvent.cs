namespace NEMEngine;

// アニメーションクリップ上の指定時刻で発火するイベント。OnAnimationEvent で受け取る。
public readonly struct AnimationEvent {

    // ハンドラ側の識別子
    public string Name { get; }
    // 任意パラメータ
    public float FloatParam { get; }
    public int IntParam { get; }
    public string StringParam { get; }

    public AnimationEvent(string name, float floatParam, int intParam, string stringParam) {
        Name = name;
        FloatParam = floatParam;
        IntParam = intParam;
        StringParam = stringParam;
    }
}
