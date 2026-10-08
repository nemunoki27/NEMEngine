using System.Reflection;
using System.Text.Json.Nodes;

namespace NEMEngine;

// 読み込んだ保存値と変換できなかったFieldを保持する
internal sealed class ScriptSerializedValues {

    internal JsonObject source = new();
    internal readonly HashSet<FieldInfo> rejectedFields = new();
    internal ScriptReferenceGraph? graph;
}
