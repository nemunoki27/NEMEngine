using System.Diagnostics;
using System.Reflection;





using System.Text.Json.Nodes;



namespace NEMEngine;

// 境界の例外とスクリプト診断を転送する
internal static unsafe class ScriptInvocationDiagnostics {

    private const int MaxExceptionFrames = 24;

    internal static ManagedStatus Guard(string apiName, Func<ManagedStatus> body) {

        try {
            return body();
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(2, $"[NativeExport:{apiName}] unhandled managed exception\n{ex}");
            return ManagedStatus.InternalError;
        }
    }

    internal static void LogScriptException(ScriptTypeRegistry registry, ScriptBehaviour script, string callbackName, Exception ex) {

        Type type = script.GetType();
        string typeName = type.FullName ?? type.Name;

        // owner entity handle。解決できればentity名も付ける
        Entity owner = script.entity;
        string entityHandle = $"{owner.native.index}:{owner.native.generation}";
        string entityName = owner.isValid ? owner.name : string.Empty;

        NativeApplicationAPI.WriteLog(2,
            $"[ScriptException] callback={callbackName} type={typeName} entity={entityHandle} name=\"{entityName}\"\n{ex}");

        // Console ログとは別に、構造化 DTO を native の exception store へ 1 件報告する。
        // UI はこの store を source of truth にする（ログ文字列の再解析はしない）。
        ReportScriptExceptionDto(registry, script, type, typeName, callbackName, ex, owner, entityName);
    }

    internal static void ReportScriptExceptionDto(ScriptTypeRegistry registry, ScriptBehaviour script, Type type, string typeName,
        string callbackName, Exception ex, Entity owner, string entityName) {

        // canonical identity は .cs.meta 由来の scriptTypeID。登録 entry から引く（表示名には使わない）。
        string scriptTypeID = registry.typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) ? entry!.scriptTypeID : string.Empty;

        var dto = new JsonObject {
            ["callback"] = callbackName,
            ["slotId"] = script.scriptSlotID,
            ["scriptTypeId"] = scriptTypeID,
            ["typeName"] = typeName,
            ["exceptionType"] = ex.GetType().FullName ?? ex.GetType().Name,
            ["message"] = ex.Message ?? string.Empty,
            ["entityIndex"] = owner.native.index,
            ["entityGeneration"] = owner.native.generation,
            ["entityName"] = entityName,
        };

        // stack frame は file/line 付きで取得し、上限件数だけ載せる（深い stack で肥大させない）。
        var frames = new JsonArray();
        var trace = new StackTrace(ex, true);
        int frameCount = trace.FrameCount;
        for (int i = 0; i < frameCount && frames.Count < MaxExceptionFrames; ++i) {

            StackFrame? frame = trace.GetFrame(i);
            MethodBase? method = frame?.GetMethod();
            if (method == null) {
                continue;
            }
            string memberName = method.DeclaringType != null
                ? $"{method.DeclaringType.FullName}.{method.Name}"
                : method.Name;
            frames.Add(new JsonObject {
                ["method"] = memberName,
                ["file"] = frame?.GetFileName() ?? string.Empty,
                ["line"] = frame?.GetFileLineNumber() ?? 0,
                ["column"] = frame?.GetFileColumnNumber() ?? 0,
            });
        }
        dto["frames"] = frames;

        NativeApplicationAPI.ReportScriptExceptionJson(dto.ToJsonString());
    }
}
