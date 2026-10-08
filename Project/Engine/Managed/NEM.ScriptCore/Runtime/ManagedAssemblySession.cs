using System.Runtime.CompilerServices;
namespace NEMEngine;

// ゲームAssemblyの読込状態と解放順を所有する
internal sealed unsafe class ManagedAssemblySession {

    internal readonly ScriptTypeRegistry registry = new();
    internal readonly ScriptFieldCodec codec;

    // ゲーム側DLLをアンロード可能にする専用LoadContext
    internal GameScriptLoadContext? gameLoadContext;
    // Assembly解放の診断連番
    internal int reloadCounter = 0;
    // 直近のALC回収結果
    internal int lastALCUnloadStatus = 0;

    // 世代履歴をAssembly間で保持する
    private readonly ScriptInstanceStore instances;

    // 型登録と個体管理を接続する
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
            // 新しいAssemblyの予約処理と購読を開始する
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

    // 所有処理と個体と型を解放してALCを回収する
    internal void ReleaseGameAssembly(bool collect) {

        // 型を解放する前に予約処理と購読を終了する
        ScriptRuntimeLifetime.EndAssemblyLifetime();

        // 個体を解放して参照の世代を進める
        instances.ReleaseAllSlots();

        // 旧Assemblyの型と保存値の参照を解除する
        registry.scriptTypeEntries.Clear();
        registry.guidToEntry.Clear();
        registry.typeToEntry.Clear();
        registry.defaultInstanceCache.Clear();
        registry.gameAssembly = null;
        codec.ResetAssemblyState();

        GameScriptLoadContext? loadContext = gameLoadContext;
        gameLoadContext = null;
        if (loadContext == null) {
            return;
        }

        // ALCの解放後に強参照を解除する
        int reloadID = ++reloadCounter;
        string contextName = loadContext.Name ?? "GameScripts";
        WeakReference weakContext = UnloadContextForCollection(loadContext);
        loadContext = null;

        if (!collect) {
            return;
        }

        // 回数を制限してALCの回収を確認する
        const int maxAttempts = 10;
        int attempts = 0;
        for (; attempts < maxAttempts && weakContext.IsAlive; ++attempts) {

            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }

        if (weakContext.IsAlive) {

            // 残留参照をEditorへ通知する
            lastALCUnloadStatus = 2; // 回収未完了
            NativeApplicationAPI.WriteLog(1,
                $"[ALC leak] GameScripts load context was not collected. reloadId={reloadID} " +
                $"context=\"{contextName}\" attempts={attempts}. " +
                "A static field, running Task/Timer, or unmanaged callback may still reference the old assembly. " +
                "Register disposables / unsubscribes via ScriptRuntimeLifetime so they are released on reload.");
        } else {

            lastALCUnloadStatus = 1; // 回収完了
            NativeApplicationAPI.WriteLog(0, $"GameScripts load context unloaded. reloadId={reloadID} attempts={attempts}");
        }
    }

    // 回収確認の呼出元へALCの強参照を残さない
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference UnloadContextForCollection(GameScriptLoadContext context) {

        context.Unload();
        return new WeakReference(context, trackResurrection: false);
    }
}
