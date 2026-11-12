using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class TimelineTests {
    private static readonly List<float> samples = [];
    private static int enters, exits, commandCount, noPreviewInstances;
    private static TimelineExitReason lastExit;
    private static NativeEntity lastBinding;
    private static string lastText = string.Empty;
    private static bool lastScrubbing;
    private static double lastDeltaTime;

    [TimelineClipType("tests.timeline.sample", "テスト補間", SupportsPreview = true)]
    private sealed class SampleClip : TimelineClip {
        [SerializeField, FormerlySerializedAs("oldAmplitude")] private float amplitude = 1;
        [SerializeField] private GameObject? target = null;
        private TimelineContext? initialContext;
        public override void OnEnter(TimelineContext context) { ++enters; initialContext = context; }
        public override void Evaluate(TimelineContext context) {
            Check(ReferenceEquals(initialContext, context), "設定更新でContextの参照を変えない");
            // 保存設定とインスタンス固有の時刻を組み合わせる
            samples.Add(amplitude * context.GetFloat("rate") * (float)context.LocalTime);
            lastBinding = GameObject.RawNative(target);
            lastText = context.GetString("label");
            lastScrubbing = context.IsScrubbing;
            lastDeltaTime = context.DeltaTime;
        }
        public override void OnExit(TimelineContext context, TimelineExitReason reason) { ++exits; lastExit = reason; }
    }

    [TimelineClipType("tests.timeline.no-preview", "プレビュー禁止")]
    private sealed class NoPreviewClip : TimelineClip {
        public NoPreviewClip() { ++noPreviewInstances; }
        public override void Evaluate(TimelineContext context) { samples.Add(999); }
    }

    [TimelineClipType("tests.timeline.throw", "例外の分離")]
    private sealed class ThrowingClip : TimelineClip {
        public override void Evaluate(TimelineContext context) { throw new InvalidOperationException("expected timeline fixture"); }
    }

    internal static void Run() {
        Check(Marshal.SizeOf<NativeTimelineState>() == 40 && Marshal.SizeOf<NativeTimelineValue>() == 24 &&
            Marshal.SizeOf<NativeTimelineFrame>() == 56 && Marshal.SizeOf<NativeTimelineBindingUpdate>() == 24 &&
            Marshal.SizeOf<NativeTimelineParameterUpdate>() == 40, "ABIのサイズ");
        Check(Marshal.OffsetOf<NativeTimelineState>("preparationProgress").ToInt32() == 28 &&
            Marshal.OffsetOf<NativeTimelineState>("preparing").ToInt32() == 32, "準備状態の配置");
        Check(Marshal.OffsetOf<NativeTimelineFrame>("isScrubbing").ToInt32() == 48, "スクラブ状態の配置");
        Check(TimelinePlayer.componentTypeID == 76 && (uint)TimeChannel.All == 511, "公開IDと時間チャンネル");
        var alive = NativeAPI.IsAlive;
        NativeAPI.IsAlive = &Alive;
        try { TestCustomClips(); TestFastAPI(); }
        finally { NativeAPI.IsAlive = alive; }
    }

    private static void TestCustomClips() {
        enters = exits = 0;
        samples.Clear();
        var registry = new TimelineClipRegistry();
        registry.Load(typeof(TimelineTests).Assembly);
        JsonArray schema = JsonNode.Parse(Encoding.UTF8.GetString(registry.Schema))!.AsArray();
        JsonObject description = schema.OfType<JsonObject>().Single(item => item["id"]!.GetValue<string>() == "tests.timeline.sample");
        Check(description["fields"]!.AsArray().OfType<JsonObject>().Any(field => field["kind"]!.GetValue<string>() == "GameObject"),
            "独自クリップの型付き設定");
        const string first = """
            {"fields":{"oldAmplitude":3,"target":{"binding":"Target"}},"usePlayerBindings":false,
             "bindings":{"Target":{"name":"Target","id":"0000000000000001","worldIndex":22,"worldGeneration":1,"index":4,"generation":1}},
             "parameters":{"rate":{"name":"rate","id":"0000000000000002","type":"Float","value":2},
              "label":{"name":"label","id":"0000000000000003","type":"String","value":"before"}}}
            """;
        Check(registry.Create(NativeEntity.Null, "tests.timeline.sample", first, out ulong firstHandle) == ManagedStatus.Ok,
            "独自クリップの生成");
        const string second = """{"fields":{"amplitude":5},"parameters":{"rate":{"type":"Float","value":2}}}""";
        Check(registry.Create(NativeEntity.Null, typeof(SampleClip).FullName!, second, out ulong secondHandle) == ManagedStatus.Ok,
            "型名による生成");
        NativeTimelineFrame frame = new() { time = 0.5, localTime = 0.5, duration = 1, deltaTime = 0.1, weight = 1, phase = 1 };
        Check(registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && samples[^1] == 3 && lastBinding.index == 4,
            "旧Field名と対象の割り当て");
        Check(registry.Evaluate(secondHandle, frame) == ManagedStatus.Ok && samples[^1] == 5 && enters == 2,
            "複数再生の状態分離");
        NativeTimelineBindingUpdate[] bindings = [new() { id = 1, worldIndex = 22, worldGeneration = 1, index = 5, generation = 1 }];
        byte[] text = Encoding.UTF8.GetBytes("更新後\0");
        fixed (byte* pointer = text) {
            NativeTimelineParameterUpdate[] parameters = [new() { id = 2, value = new() { kind = 2, scalar = 4 } },
                new() { id = 3, value = new() { kind = 4 }, stringValue = pointer }];
            Check(registry.UpdateContext(firstHandle, bindings, parameters) == ManagedStatus.Ok &&
                registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && samples[^1] == 6 && lastBinding.index == 5 && lastText == "更新後",
                "子Timelineの対象・パラメーター・保存参照Fieldを現在値へ更新する");
            samples.EnsureCapacity(samples.Count + 1100);
            for (int i = 0; i < 32; ++i) { registry.UpdateContext(firstHandle, bindings, parameters); registry.Evaluate(firstHandle, frame); }
            long allocated = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 1000; ++i) { registry.UpdateContext(firstHandle, bindings, parameters); registry.Evaluate(firstHandle, frame); }
            Check(GC.GetAllocatedBytesForCurrentThread() == allocated, "独自クリップの現在値同期・評価で毎フレーム割り当てをしない");
            JsonObject edited = JsonNode.Parse(first)!.AsObject();
            edited["fields"]!["amplitude"] = 7;
            edited["parameters"]!["rate"]!["value"] = 4;
            edited["bindings"]!["Target"]!["index"] = 5;
            Check(registry.Reconfigure(firstHandle, "tests.timeline.sample", edited.ToJsonString()) == ManagedStatus.Ok &&
                registry.UpdateContext(firstHandle, bindings, parameters) == ManagedStatus.Ok &&
                registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && samples[^1] == 14 && enters == 2 && exits == 0,
                "保存Fieldの編集で同じ個体を更新し開始と終了を再実行しない");
            edited["fields"]!["amplitude"] = "invalid";
            Check(registry.Reconfigure(firstHandle, "tests.timeline.sample", edited.ToJsonString()) == ManagedStatus.InvalidArgument &&
                registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && samples[^1] == 14 && enters == 2,
                "無効な編集は稼働中のFieldと個体へ反映しない");
            Check(registry.Reconfigure(firstHandle, "tests.timeline.no-preview", "{}") == ManagedStatus.InvalidArgument && enters == 2,
                "稼働中に独自型を変更しても別個体を開始しない");
            frame.isScrubbing = 1;
            frame.deltaTime = 0;
            frame.time = frame.localTime = 0.2;
            Check(registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && lastScrubbing && lastDeltaTime == 0 &&
                Math.Abs(samples[^1] - 5.6f) < 0.0001f && enters == 2 && exits == 0,
                "逆スクラブで個体を保ち絶対時刻と時間差ゼロとIsScrubbingを渡す");
            frame.localTime = frame.time = 0.8;
            Check(registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && enters == 2 && exits == 0,
                "連続スクラブで開始と終了を再実行しない");
            frame.isScrubbing = 2;
            Check(registry.Evaluate(firstHandle, frame) == ManagedStatus.InvalidArgument, "無効なスクラブフラグを拒否する");
            frame.isScrubbing = 0;
            frame.deltaTime = 0.1;
            Check(registry.Evaluate(firstHandle, frame) == ManagedStatus.Ok && !lastScrubbing, "通常再生でスクラブ状態を解除する");
        }
        Check(registry.Release(firstHandle, TimelineExitReason.Stopped) == ManagedStatus.Ok && exits == 1 &&
            lastExit == TimelineExitReason.Stopped, "停止時の後始末");
        Check(registry.Release(firstHandle, TimelineExitReason.Stopped) == ManagedStatus.InvalidInstanceHandle && exits == 1,
            "終了処理の二重呼び出し防止");
        int count = samples.Count;
        int created = noPreviewInstances;
        Check(registry.Create(NativeEntity.Null, "tests.timeline.no-preview", "{\"evaluationMode\":2}", out ulong suppressed) == ManagedStatus.Ok &&
            noPreviewInstances == created, "非対応プレビューは実体のコンストラクターを実行しない");
        Check(registry.Release(suppressed, TimelineExitReason.Stopped) == ManagedStatus.Ok, "非対応プレビューの解放");
        Check(registry.Create(NativeEntity.Null, "tests.timeline.no-preview", "{}", out ulong muted) == ManagedStatus.Ok,
            "プレビュー禁止クリップの生成");
        frame.evaluationMode = (int)TimelineEvaluationMode.Preview;
        Check(registry.Evaluate(muted, frame) == ManagedStatus.Ok && samples.Count == count, "プレビューの明示対応");
        Check(registry.Evaluate(secondHandle, frame) == ManagedStatus.Ok && samples.Count == count + 1,
            "対応クリップのプレビュー");
        Check(registry.Create(NativeEntity.Null, "tests.timeline.throw", "{}", out ulong faulty) == ManagedStatus.Ok,
            "例外用クリップの生成");
        frame.evaluationMode = 0;
        Check(registry.Evaluate(faulty, frame) == ManagedStatus.ScriptException &&
            registry.Evaluate(secondHandle, frame) == ManagedStatus.Ok, "例外を起こしたクリップの分離");
        registry.Clear();
        Check(exits == 2 && lastExit == TimelineExitReason.Reload &&
            registry.Evaluate(secondHandle, frame) == ManagedStatus.InvalidInstanceHandle, "再読み込み時の個体破棄");
    }

    private static void TestFastAPI() {
        var previousState = NativeAPI.GetTimelineState;
        var previousCommand = NativeAPI.TimelineCommand;
        var previousValue = NativeAPI.GetTimelineParameter;
        var previousWindow = NativeAPI.IsTimelineWindowOpen;
        var previousInstance = NativeAPI.GetComponentInstanceID;
        NativeAPI.GetTimelineState = &ReadState;
        NativeAPI.TimelineCommand = &Command;
        NativeAPI.GetTimelineParameter = &ReadValue;
        NativeAPI.IsTimelineWindowOpen = &Window;
        NativeAPI.GetComponentInstanceID = &Instance;
        try {
            var owner = GameObject.FromNative(new() { world = new() { index = 22, generation = 1 }, index = 4, generation = 1 })!;
            var player = TimelinePlayer.FromEntity(owner);
            player.Play(); player.Pause(); player.Resume(); player.Stop(); player.Reset(); player.Seek(0.25);
            Check(commandCount == 6 && player.GetFloat("rate") == 2.5f && player.IsWindowOpen("Combo"), "即時の再生と条件API");
            Check(player.GetState() is { IsPlaying: true, IsPreparing: true, PreparationProgress: 0.5f }, "準備状態の取得");
            for (int i = 0; i < 32; ++i) { _ = player.GetState(); _ = player.GetFloat("rate"); _ = player.IsWindowOpen("Combo"); _ = player.ConsumeEvents(); }
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 1000; ++i) { _ = player.GetState(); _ = player.GetFloat("rate"); _ = player.IsWindowOpen("Combo"); _ = player.ConsumeEvents(); }
            Check(GC.GetAllocatedBytesForCurrentThread() == before, "頻繁な状態参照でJSON転送と割り当てをしない");
        } finally {
            NativeAPI.GetTimelineState = previousState;
            NativeAPI.TimelineCommand = previousCommand;
            NativeAPI.GetTimelineParameter = previousValue;
            NativeAPI.IsTimelineWindowOpen = previousWindow;
            NativeAPI.GetComponentInstanceID = previousInstance;
        }
    }

    private static void Check(bool condition, string name) {
        if (!condition) throw new InvalidOperationException($"Timelineの検証に失敗しました: {name}");
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Alive(NativeEntity entity) => entity.world.index == 22 && entity.index is 4 or 5 ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static ulong Instance(NativeEntity entity, int type) => type == 76 ? 1UL : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Command(NativeEntity entity, int action, double time) { ++commandCount; return 1; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ReadState(NativeEntity entity, NativeTimelineState* output) {
        *output = new NativeTimelineState { time = 0.25, duration = 1, playing = 1, preparationProgress = 0.5f, preparing = 1 };
        return 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ReadValue(NativeEntity entity, byte* name, NativeTimelineValue* output) {
        *output = new NativeTimelineValue { kind = 2, scalar = 2.5f };
        return 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Window(NativeEntity entity, byte* name) => 1;
}
