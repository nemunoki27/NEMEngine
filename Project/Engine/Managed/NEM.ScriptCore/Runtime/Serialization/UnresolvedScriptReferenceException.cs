using System.Text.Json;

namespace NEMEngine;

// 未解決の保存参照をnullへの書換えと区別する
internal sealed class UnresolvedScriptReferenceException : JsonException {

    internal UnresolvedScriptReferenceException(string message) : base(message) { }
}
