using System.Reflection;


using System.Runtime.Loader;






namespace NEMEngine;

// ゲームAssemblyと依存DLLの読込範囲
internal sealed class GameScriptLoadContext : AssemblyLoadContext {

    // ゲームDLLの依存関係解決に使うResolver
    private readonly AssemblyDependencyResolver resolver;
    // ScriptCore本体はホスト側で読み込まれているものを共有する
    private readonly Assembly scriptCoreAssembly = typeof(MonoBehaviour).Assembly;

    public GameScriptLoadContext(string mainAssemblyPath) : base("NEMEngine.GameScripts", isCollectible: true) {
        resolver = new AssemblyDependencyResolver(mainAssemblyPath);
    }

    protected override Assembly? Load(AssemblyName assemblyName) {

        // NEM.ScriptCoreはゲームDLL側へ重複ロードしない
        if (assemblyName.Name == scriptCoreAssembly.GetName().Name) {
            return scriptCoreAssembly;
        }

        // GameScripts.dllの横にある依存DLLを解決する
        string? assemblyPath = resolver.ResolveAssemblyToPath(assemblyName);
        return assemblyPath == null ? null : LoadFromAssemblyPath(assemblyPath);
    }

    // native 依存 DLL（P/Invoke 先）を deps.json 経由で解決する。
    // managed の Load() と同じく resolver を使い、解決できた場合だけ明示ロードする。
    protected override IntPtr LoadUnmanagedDll(string unmanagedDllName) {

        string? unmanagedPath = resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
        if (unmanagedPath != null) {
            return LoadUnmanagedDllFromPath(unmanagedPath);
        }
        // 解決できなければ既定動作（OS既定検索）へ委ねる
        return IntPtr.Zero;
    }
}
