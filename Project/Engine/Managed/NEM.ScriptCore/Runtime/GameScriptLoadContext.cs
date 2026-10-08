using System.Reflection;
using System.Runtime.Loader;
namespace NEMEngine;

// ゲームAssemblyと依存DLLの読込範囲
internal sealed class GameScriptLoadContext : AssemblyLoadContext {

    // ゲームDLLを回収可能な読込範囲へ配置する
    public GameScriptLoadContext(string mainAssemblyPath) : base("NEMEngine.GameScripts", isCollectible: true) {

        resolver = new AssemblyDependencyResolver(mainAssemblyPath);
    }

    // ScriptCoreを共有し他の依存Assemblyを解決する
    protected override Assembly? Load(AssemblyName assemblyName) {

        // NEM.ScriptCoreはゲームDLL側へ重複ロードしない
        if (assemblyName.Name == scriptCoreAssembly.GetName().Name) {
            return scriptCoreAssembly;
        }

        // GameScripts.dllの横にある依存DLLを解決する
        string? assemblyPath = resolver.ResolveAssemblyToPath(assemblyName);
        return assemblyPath == null ? null : LoadFromAssemblyPath(assemblyPath);
    }

    // 依存NativeDLLを解決して読み込む
    protected override IntPtr LoadUnmanagedDll(string unmanagedDllName) {

        string? unmanagedPath = resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
        if (unmanagedPath != null) {
            return LoadUnmanagedDllFromPath(unmanagedPath);
        }
        // 未解決のDLLはOSの検索へ委ねる
        return IntPtr.Zero;
    }

    // ゲームDLLの依存関係を解決する
    private readonly AssemblyDependencyResolver resolver;
    // ホスト側のScriptCoreを共有する
    private readonly Assembly scriptCoreAssembly = typeof(MonoBehaviour).Assembly;
}
