using System.Diagnostics;
using System.Reflection;
using System.Text.Json.Nodes;

namespace NEMEngine;

// 境界の例外とスクリプト診断を転送する
internal static unsafe class ScriptInvocationDiagnostics {

    // Native呼出しの例外境界を保護
    internal static ManagedStatus Guard(string apiName, Func<ManagedStatus> body) {

        try {
            return body();
        }
        catch (Exception ex) {
            // 例外の文字列化に失敗してもNativeへ戻す
            try {
                NativeApplicationAPI.WriteLog(2, $"[NativeExport:{apiName}] unhandled managed exception\n{ex}");
            }
            catch {
            }
            return ManagedStatus.InternalError;
        }
    }

    // callbackの例外を診断へ転送
    internal static void LogScriptException(ScriptTypeRegistry registry, MonoBehaviour script, string callbackName, Exception ex) {

        try {
            WriteScriptException(registry, script, callbackName, ex);
        }
        catch {
            // 診断の失敗でNativeの例外境界を越えない
            try {
                NativeApplicationAPI.WriteLog(2, "[ScriptException] Failed to format script exception diagnostics.");
            }
            catch {
            }
        }
    }

    // Timer／Coroutine／遅延Eventの例外をPause判定へ渡す
    internal static void ReportRuntimeServiceException(string apiName, Exception ex) {

        try {
            NativeApplicationAPI.WriteLog(2, $"[RuntimeServiceException] api={apiName}\n{ex}");
            var dto = new JsonObject {
                ["callback"] = apiName,
                ["exceptionType"] = ex.GetType().FullName ?? ex.GetType().Name,
                ["message"] = ex.Message ?? string.Empty,
                ["frames"] = new JsonArray(),
            };
            NativeApplicationAPI.ReportScriptExceptionJson(dto.ToJsonString());
        }
        catch {
            try {
                NativeApplicationAPI.WriteLog(2, "[RuntimeServiceException] Failed to report diagnostics.");
            }
            catch {
            }
        }
    }

    // Debug.LogErrorをEditorのError Pauseへ通知する
    internal static void ReportLogError(string message) {

        var dto = new JsonObject {
            ["callback"] = "Debug.LogError",
            ["exceptionType"] = "LogError",
            ["message"] = message,
            ["frames"] = new JsonArray(),
        };
        NativeApplicationAPI.ReportScriptExceptionJson(dto.ToJsonString());
    }

    // 構造化した例外情報をNativeへ転送
    internal static void ReportScriptExceptionDTO(ScriptTypeRegistry registry, MonoBehaviour script, Type type, string typeName,
        string callbackName, Exception ex, NativeEntity owner, string entityName) {

        // 型の登録情報から保存IDを取得
        string scriptTypeID = registry.typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry!.scriptTypeID : string.Empty;

        var dto = new JsonObject {
            ["callback"] = callbackName,
            ["slotId"] = script.scriptSlotID,
            ["scriptTypeId"] = scriptTypeID,
            ["typeName"] = typeName,
            ["exceptionType"] = ex.GetType().FullName ?? ex.GetType().Name,
            ["message"] = ex.Message ?? string.Empty,
            ["worldIndex"] = owner.world.index,
            ["worldGeneration"] = owner.world.generation,
            ["entityIndex"] = owner.index,
            ["entityGeneration"] = owner.generation,
            ["entityName"] = entityName,
        };

        // 呼出し位置を上限件数まで取得
        var frames = new JsonArray();
        var trace = new StackTrace(ex, true);
        int frameCount = trace.FrameCount;
        for (int i = 0; i < frameCount && frames.Count < MaxExceptionFrames; ++i) {

            if (trace.GetFrame(i) is not StackFrame frame || frame.GetMethod() is not MethodBase method) {
                continue;
            }

            string memberName = method.DeclaringType != null
                ? $"{method.DeclaringType.FullName}.{method.Name}"
                : method.Name;
            frames.Add(new JsonObject {
                ["method"] = memberName,
                ["file"] = frame.GetFileName() ?? string.Empty,
                ["line"] = frame.GetFileLineNumber(),
                ["column"] = frame.GetFileColumnNumber(),
            });
        }
        dto["frames"] = frames;

        NativeApplicationAPI.ReportScriptExceptionJson(dto.ToJsonString());
    }

    private const int MaxExceptionFrames = 24;

    // 所有情報と例外をログへ出力
    private static void WriteScriptException(ScriptTypeRegistry registry, MonoBehaviour script, string callbackName, Exception ex) {

        Type type = script.GetType();
        string typeName = type.FullName ?? type.Name;

        // 所有GameObjectの参照と表示名を取得
        GameObject? owner = script.ownerReference;
        NativeEntity handle = GameObject.RawNative(owner);
        string entityHandle = $"{handle.index}:{handle.generation}";
        string entityName = owner != null ? owner.name : string.Empty;

        NativeApplicationAPI.WriteLog(2,
            $"[ScriptException] callback={callbackName} type={typeName} gameObject={entityHandle} name=\"{entityName}\"\n{ex}");

        // 一覧用の構造化診断をNativeへ送る
        ReportScriptExceptionDTO(registry, script, type, typeName, callbackName, ex, handle, entityName);
    }
}
