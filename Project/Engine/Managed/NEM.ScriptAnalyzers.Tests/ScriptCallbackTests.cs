using System.Collections;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class ScriptCallbackTests {

    private class BaseScript : MonoBehaviour {
        internal int count;
        private void Awake() { count += 3; }
        protected virtual void Update() { count += 7; }
    }

    private sealed class DerivedScript : BaseScript {
        protected override void Update() { count += 11; }
        private void OnCollisionEnter(Collision collision) { count += 13; }
        private void LateUpdate() { throw new InvalidOperationException("callback failure"); }
        private IEnumerator Start() { count += 17; yield return null; count += 19; }
    }

    private sealed class InvalidScript : MonoBehaviour {
        private int Awake() => 1;
    }

    internal static void Run() {

        var script = new DerivedScript();
        var callbacks = new ScriptCallbacks(typeof(DerivedScript));
        callbacks.Awake!(script);
        callbacks.Update!(script);
        callbacks.OnCollisionEnter!(script, default);
        Check(script.count == 27 && callbacks.FixedUpdate is null);

        // Startの最初のyieldまでは呼出し中に進む
        callbacks.Start!(script);
        Check(script.count == 44);
        Coroutines.StopAllForOwner(script);
        Check(script.count == 44);

        // 反射の包み例外を挟まず元の例外を受け取る
        bool raised = false;
        try {
            callbacks.LateUpdate!(script);
        } catch (InvalidOperationException ex) when (ex.Message == "callback failure") {
            raised = true;
        }
        Check(raised);

        // 解決後の通常callbackは反射検索や配列確保を行わない
        for (int i = 0; i < 100; ++i) { callbacks.Update!(script); }
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1000; ++i) { callbacks.Update!(script); }
        Check(GC.GetAllocatedBytesForCurrentThread() == before);
        bool rejected = false;
        try {
            _ = new ScriptCallbacks(typeof(InvalidScript));
        } catch (InvalidOperationException) {
            rejected = true;
        }
        Check(rejected);
        CheckServiceInterruption();
        CheckDiagnosticWorld();
        Console.WriteLine("[PASS] named callbacks, service interruption and callback resume.");
    }

    // 構造化診断にWorldの世代を残す
    private static void CheckDiagnosticWorld() {

        var previousReport = NativeAPI.ReportScriptException;
        NativeAPI.ReportScriptException = &CaptureDiagnostic;
        try {
            var script = new DerivedScript();
            var owner = new NativeEntity { world = new ManagedWorldHandle { index = 7, generation = 11 }, index = 3, generation = 0 };
            ScriptInvocationDiagnostics.ReportScriptExceptionDTO(new ScriptTypeRegistry(), script, typeof(DerivedScript),
                nameof(DerivedScript), "Update", new InvalidOperationException("所有者"), owner, "診断対象");
            JsonObject dto = JsonNode.Parse(diagnosticJSON)!.AsObject();
            Check(dto["worldIndex"]!.GetValue<uint>() == 7 && dto["worldGeneration"]!.GetValue<uint>() == 11 &&
                dto["entityIndex"]!.GetValue<uint>() == 3 && dto["entityGeneration"]!.GetValue<uint>() == 0);
        }
        finally {
            NativeAPI.ReportScriptException = previousReport;
            diagnosticJSON = string.Empty;
        }
    }

    private static string diagnosticJSON = string.Empty;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void CaptureDiagnostic(byte* jsonUTF8) {
        diagnosticJSON = Marshal.PtrToStringUTF8((nint)jsonUTF8) ?? string.Empty;
    }

    private static bool interruption;
    private static bool pauseOnError;

    // Event、Timer、Coroutineの停止境界を確認する
    private static void CheckServiceInterruption() {
        var previousInterruption = NativeAPI.IsUpdateInterrupted;
        var previousReport = NativeAPI.ReportScriptException;
        var previousDelta = NativeAPI.GetDeltaTime;
        NativeAPI.IsUpdateInterrupted = &ReadInterruption;
        NativeAPI.ReportScriptException = &ReportFailure;
        NativeAPI.GetDeltaTime = &ReadDelta;
        pauseOnError = true;
        interruption = false;
        ScriptServiceLifetime.ResetForReload();
        try {
            int failed = 0, handlerFollower = 0, unrelated = 0;
            Action handlers = () => { ++failed; throw new InvalidOperationException("event probe"); };
            handlers += () => ++handlerFollower;
            EventDispatch.Enqueue(() => EventDispatch.Raise(handlers, "fixture"));
            EventDispatch.Enqueue(() => ++unrelated);
            bool raised = false;
            try { EventDispatch.FlushDeferred(); }
            catch (InvalidOperationException) { raised = true; }
            Check(raised && failed == 1 && handlerFollower == 0 && unrelated == 0);
            // 同じEventの残りは再配信せず、別のEventを次回へ残す
            EventDispatch.FlushDeferred();
            Check(unrelated == 1 && failed == 1 && handlerFollower == 0);
            try { EventDispatch.Raise(handlers, "fixture"); }
            catch (InvalidOperationException) { }
            Check(failed == 2 && handlerFollower == 0);

            // callback中に追加したEventは同じflushで実行しない
            int nested = 0;
            EventDispatch.Enqueue(() => EventDispatch.Enqueue(() => ++nested));
            EventDispatch.FlushDeferred();
            Check(nested == 0);
            EventDispatch.FlushDeferred();
            Check(nested == 1);

            int repeatingCalls = 0, timerFollower = 0;
            TimerHandle repeating = Timers.ScheduleRepeating(0.0f, () => {
                ++repeatingCalls;
                throw new InvalidOperationException("timer probe");
            });
            Timers.Schedule(0.0f, () => ++timerFollower);
            delegate* unmanaged[Cdecl]<int, int> tick = &HostBridge.TickFrame;
            Check(tick(0) == (int)ManagedStatus.ScriptException && interruption && repeating.IsValid &&
                repeatingCalls == 1 && timerFollower == 0);
            interruption = false;
            Check(tick(0) == (int)ManagedStatus.ScriptException && repeatingCalls == 2 && timerFollower == 0);
            Timers.Cancel(repeating);
            interruption = false;
            Check(tick(0) == (int)ManagedStatus.Ok && timerFollower == 1);

            int coroutineFollower = 0;
            CoroutineHandle failing = Coroutines.Start(null!, FailingRoutine());
            CoroutineHandle follower = Coroutines.Start(null!, FollowingRoutine(() => ++coroutineFollower));
            Check(tick(0) == (int)ManagedStatus.ScriptException && !failing.IsValid && follower.IsValid &&
                coroutineFollower == 0);
            interruption = false;
            Check(tick(0) == (int)ManagedStatus.Ok && coroutineFollower == 1 && !follower.IsValid);

            Timers.ResetForReload();
            // LogErrorはcallbackを途中で巻き戻さず、次の予約を止める
            int callbackBoundary = 0, nestedHandler = 0, nestedGeneric = 0;
            timerFollower = 0;
            Timers.Schedule(0.0f, () => {
                ++callbackBoundary;
                NEMEngine.Debug.LogError("interruption probe");
                EventDispatch.Raise(() => ++nestedHandler, "nested fixture");
                EventDispatch.Raise<int>(_ => ++nestedGeneric, 1, "nested fixture");
                ++callbackBoundary;
            });
            Timers.Schedule(0.0f, () => ++timerFollower);
            Check(tick(0) == (int)ManagedStatus.Ok && interruption && callbackBoundary == 2 && timerFollower == 0 &&
                nestedHandler == 0 && nestedGeneric == 0);
            interruption = false;
            Check(tick(0) == (int)ManagedStatus.Ok && timerFollower == 1);

            EventDispatch.Raise(() => ++nestedHandler, "resumed fixture");
            EventDispatch.Raise<int>(_ => ++nestedGeneric, 1, "resumed fixture");
            Check(nestedHandler == 1 && nestedGeneric == 1);

            // Error Pauseが無効なら後続の予約も進める
            pauseOnError = false;
            timerFollower = 0;
            Timers.Schedule(0.0f, () => NEMEngine.Debug.LogError("pause disabled"));
            Timers.Schedule(0.0f, () => ++timerFollower);
            Check(tick(0) == (int)ManagedStatus.Ok && timerFollower == 1);
        }
        finally {
            ScriptServiceLifetime.ResetForReload();
            NativeAPI.IsUpdateInterrupted = previousInterruption;
            NativeAPI.ReportScriptException = previousReport;
            NativeAPI.GetDeltaTime = previousDelta;
        }
    }

    private static IEnumerator FailingRoutine() {
        yield return null;
        throw new InvalidOperationException("coroutine probe");
    }

    private static IEnumerator FollowingRoutine(Action action) {
        yield return null;
        action();
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int ReadInterruption() => pauseOnError && interruption ? 1 : 0;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static float ReadDelta() => 0.01f;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void ReportFailure(byte* _) { interruption = true; }

    private static void Check(bool result) {
        if (!result) { throw new InvalidOperationException("Script callback contract failed."); }
    }
}
