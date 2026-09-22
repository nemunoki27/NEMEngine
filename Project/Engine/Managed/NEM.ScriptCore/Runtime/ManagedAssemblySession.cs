using System.Runtime.CompilerServices;








namespace NEMEngine;

// ゲームAssemblyの読込状態と解放順を所有する
internal sealed unsafe class ManagedAssemblySession {

    internal readonly ScriptTypeRegistry registry = new();
    internal readonly ScriptFieldCodec codec;
    private readonly ScriptInstanceStore instances;

    // ゲーム側DLLをアンロード可能にする専用LoadContext
    internal GameScriptLoadContext? gameLoadContext;
    // reload 診断用の連番（ALC unload ログに使う）
    internal int reloadCounter = 0;
    // 直近の collectible ALC unload の typed status（0=Unknown, 1=UnloadSucceeded, 2=LeakSuspected）。
    // Editor は log scraping ではなくこの typed status を参照する。
    internal int lastAlcUnloadStatus = 0;

    internal ManagedAssemblySession(ScriptInstanceStore instances) {
        this.instances = instances;
        codec = new ScriptFieldCodec(registry);
    }

    // 旧Assemblyを解放して新しい型を登録する
    internal ManagedStatus Load(string path) {

        try {
            ManagedDebuggerWait.WaitForManagedDebuggerIfRequested();

            // 既存DLLを解放してから新しいDLLを読み込む
            ReleaseGameAssembly(collect: true);
            gameLoadContext = new GameScriptLoadContext(path);
            registry.gameAssembly = gameLoadContext.LoadFromAssemblyPath(path);
            registry.RebuildScriptTypes(codec);
            // 新しい assembly の寿命を開始する（unload 前に停止/解放するための起点）
            ScriptRuntimeLifetime.BeginAssemblyLifetime();
            NativeApplicationAPI.WriteLog(0, $"Loaded GameScripts: {path}, scriptTypes={registry.scriptTypeEntries.Count}");
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"Failed to load GameScripts: {path}\n{ex}");
            ReleaseGameAssembly(collect: true);
            return ManagedStatus.InternalError;
        }
    }

    internal void ReleaseGameAssembly(bool collect) {

        // user code が登録した IDisposable / 購読解除を unload 前に実行し、reload token を cancel する。
        // 古い assembly を参照し続ける task / timer / event を止めて ALC 回収を妨げないようにする。
        ScriptRuntimeLifetime.EndAssemblyLifetime();

        // ロード済みインスタンスや型情報をすべて破棄する。
        // slot配列はclearせず全slotをreleaseしてgenerationを進める。
        // generation履歴を保つことで、reload前のhandleがreload後の別instanceへ届かない（reload epoch相当）。
        instances.ReleaseAllSlots();
        registry.scriptTypeEntries.Clear();
        registry.guidToEntry.Clear();
        registry.typeToEntry.Clear();
        registry.defaultInstanceCache.Clear();
        registry.gameAssembly = null;

        GameScriptLoadContext? loadContext = gameLoadContext;
        gameLoadContext = null;
        if (loadContext == null) {
            return;
        }

        // ALC への strong reference を scope 外へ追い出してから unload する（回収可能にするため）。
        int reloadID = ++reloadCounter;
        string contextName = loadContext.Name ?? "GameScripts";
        WeakReference weakContext = UnloadContextForCollection(loadContext);
        loadContext = null;

        if (!collect) {
            return;
        }

        // 限定回数だけ GC を回して回収を促す（無制限ループはしない）。Edit reload 時のみのコスト。
        const int maxAttempts = 10;
        int attempts = 0;
        for (; attempts < maxAttempts && weakContext.IsAlive; ++attempts) {

            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }

        if (weakContext.IsAlive) {

            // 回収できなかった = どこかに古い assembly への strong reference が残っている
            lastAlcUnloadStatus = 2; // LeakSuspected
            NativeApplicationAPI.WriteLog(1,
                $"[ALC leak] GameScripts load context was not collected. reloadId={reloadID} " +
                $"context=\"{contextName}\" attempts={attempts}. " +
                "A static field, running Task/Timer, or unmanaged callback may still reference the old assembly. " +
                "Register disposables / unsubscribes via ScriptRuntimeLifetime so they are released on reload.");
        } else {

            lastAlcUnloadStatus = 1; // UnloadSucceeded
            NativeApplicationAPI.WriteLog(0, $"GameScripts load context unloaded. reloadId={reloadID} attempts={attempts}");
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference UnloadContextForCollection(GameScriptLoadContext context) {

        context.Unload();
        return new WeakReference(context, trackResurrection: false);
    }
}
