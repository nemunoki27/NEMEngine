using NEMEngine;

namespace NEMTests.Scripts;

// 実際のNative接続で再生要求と現在値を往復させる
[ScriptTypeID("4b5cb237-ff8c-4a55-a3a5-4d07c9f1f735")]
public sealed class TimelineAPIProbe : MonoBehaviour {
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000001")] private int command;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000002")] private float currentTime;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000003")] private bool paused;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000004")] private bool playing;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000005")] private bool window;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000006")] private int events;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000007")] private int enters;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000008")] private int exits;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000009")] private int samples;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000010")] private string targetName = string.Empty;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000011")] private string fieldTargetName = string.Empty;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000012")] private string label = string.Empty;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000013")] private string error = string.Empty;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000014")] private bool finished;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000015")] private int exitReason;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000016")] private bool scrubbing;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000017")] private float sampleDelta;
    [SerializeField, SerializedFieldID("48bbef01-32c4-42c4-a1c8-000000000018")] private float sampleTime;
    private TimelinePlayer? player;

    private void Awake() {
        // Nativeで追加したComponentを通常のGetComponentで取得する
        player = GetComponent<TimelinePlayer>();
    }

    private void Update() {
        try {
            if (player == null) throw new InvalidOperationException("TimelinePlayerを取得できません");
            int request = command;
            command = 0;
            switch (request) {
            case 1:
                // 条件と割り当てを設定してから再生する
                player.SetBinding("Target", GameObject.Find("TimelineTarget"));
                player.SetFloat("Power", 3);
                player.SetString("Label", "play");
                player.Play();
                break;
            case 2: player.Pause(); break;
            case 3: player.Resume(); break;
            case 4: player.Seek(0.35); break;
            case 5: player.Stop(); break;
            case 6: player.Reset(); break;
            case 7:
                // 再生中の子・独自クリップへ変更が届くことを確認する
                player.SetBinding("Target", GameObject.Find("TimelineReplacement"));
                player.SetFloat("Power", 4);
                player.SetString("Label", "updated");
                break;
            }
            TimelinePlayerState state = player.GetState();
            currentTime = (float)state.Time;
            paused = state.IsPaused;
            playing = state.IsPlaying;
            finished = state.IsFinished;
            window = player.IsWindowOpen("Combo");
            // 通知の消費による重複防止も実際の転送経路で確認する
            foreach (TimelineEvent signal in player.ConsumeEvents()) {
                if (signal.Name != "Strike") throw new InvalidOperationException("通知名が一致しません");
                ++events;
            }
        } catch (Exception exception) {
            error = exception.ToString();
        }
    }

    public void EnterClip() { ++enters; }
    public void ExitClip(TimelineExitReason reason) { ++exits; exitReason = (int)reason; }
    public void SampleClip(TimelineContext context, GameObject? fieldTarget) {
        // JSON化される監視用の値と、実際のComponent書込みを分ける
        ++samples;
        GameObject? target = context.GetBinding("Target");
        targetName = target != null ? target.name : string.Empty;
        fieldTargetName = fieldTarget != null ? fieldTarget.name : string.Empty;
        label = context.GetString("Label");
        scrubbing = context.IsScrubbing;
        sampleDelta = (float)context.DeltaTime;
        sampleTime = (float)context.LocalTime;
    }
}

[TimelineClipType("tests.timeline.integration", "実橋の補間", SupportsPreview = true)]
public sealed class TimelineIntegrationClip : TimelineClip {
    [SerializeField, SerializedFieldID("8123bab4-18bf-46ca-9e88-98599f472173")] private float amplitude = 1;
    [SerializeField, SerializedFieldID("daafbc49-1e41-4a8c-aefd-482f7af14036")] private GameObject? target;
    private TimelineAPIProbe? probe;
    private TimelineContext? initialContext;

    public override void OnEnter(TimelineContext context) {
        // Script型のGetComponentも実際のインスタンスを返す必要がある
        probe = context.Owner?.GetComponent<TimelineAPIProbe>();
        if (probe == null) throw new InvalidOperationException("監視用Scriptを取得できません");
        probe.EnterClip();
        initialContext = context;
    }

    public override void Evaluate(TimelineContext context) {
        if (!ReferenceEquals(initialContext, context)) throw new InvalidOperationException("継続中のContextが変更されました");
        // Nativeの時刻と型付きパラメーターで実際のTransformを更新する
        if (context.Owner != null)
            context.Owner.transform.localPosition = new(amplitude * context.GetFloat("Power") * (float)context.LocalTime, 0, 0);
        probe?.SampleClip(context, target);
    }

    public override void OnExit(TimelineContext context, TimelineExitReason reason) {
        probe?.ExitClip(reason);
        probe = null;
    }
}
