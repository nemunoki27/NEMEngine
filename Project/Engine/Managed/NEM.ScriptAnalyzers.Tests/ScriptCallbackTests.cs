using System.Collections;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static class ScriptCallbackTests {

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
        Console.WriteLine("[PASS] named script callbacks, inheritance and allocation-free invocation.");
    }

    private static void Check(bool result) {
        if (!result) { throw new InvalidOperationException("Script callback contract failed."); }
    }
}
