using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace NEMEngine;

// JSONの数値を精度を失わず変換する
internal static class ScriptNumericConversion {

    // 数学型の内部Fieldも同じ変換規則で読む
    internal static void Configure(JsonSerializerOptions options) {
        options.Converters.Add(new NumericConverter<float>());
        options.Converters.Add(new NumericConverter<double>());
    }

    private sealed class NumericConverter<T> : JsonConverter<T> where T : struct {
        public override T Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {
            using JsonDocument document = JsonDocument.ParseValue(ref reader);
            TryRead(typeof(T), JsonNode.Parse(document.RootElement.GetRawText())!, out object? result);
            return (T)result!;
        }

        public override void Write(Utf8JsonWriter writer, T value, JsonSerializerOptions options) {
            JsonSerializer.Serialize(writer, value);
        }
    }

    internal static bool TryRead(Type type, JsonNode value, out object? result) {
        result = null;
        Type target = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        bool integer = target == typeof(byte) || target == typeof(sbyte) || target == typeof(short) ||
            target == typeof(ushort) || target == typeof(int) || target == typeof(uint) || target == typeof(long) || target == typeof(ulong);
        if (!integer && target != typeof(float) && target != typeof(double)) { return false; }
        string text = value.ToJsonString();
        using JsonDocument document = JsonDocument.Parse(text);
        if (document.RootElement.ValueKind != JsonValueKind.Number) { throw new JsonException("Expected a numeric field value."); }
        (string digits, int exponent) = Normalize(text);
        if (integer) {
            if (exponent < 0 || exponent > 20 || digits.Length > 21) { throw new JsonException("Integer conversion loses precision."); }
            BigInteger number = BigInteger.Parse(digits, CultureInfo.InvariantCulture) * BigInteger.Pow(10, exponent);
            try {
                if (target == typeof(byte)) { result = (byte)number; }
                else if (target == typeof(sbyte)) { result = (sbyte)number; }
                else if (target == typeof(short)) { result = (short)number; }
                else if (target == typeof(ushort)) { result = (ushort)number; }
                else if (target == typeof(int)) { result = (int)number; }
                else if (target == typeof(uint)) { result = (uint)number; }
                else if (target == typeof(long)) { result = (long)number; }
                else { result = (ulong)number; }
            }
            catch (OverflowException ex) { throw new JsonException("Integer value is out of range.", ex); }
            if (type.IsEnum) { result = Enum.ToObject(type, result!); }
            return true;
        }

        // 保存時の最短表記とNativeからのfloat昇格表記を受け入れる
        double parsed = document.RootElement.GetDouble();
        if (target == typeof(float)) {
            float converted = (float)parsed;
            if (!float.IsFinite(converted) ||
                (Normalize(converted.ToString("R", CultureInfo.InvariantCulture)) != (digits, exponent) &&
                    ((double)converted != parsed || Normalize(parsed.ToString("R", CultureInfo.InvariantCulture)) != (digits, exponent)))) {
                throw new JsonException("Single precision conversion loses precision.");
            }
            result = converted;
        } else {
            if (!double.IsFinite(parsed) || Normalize(parsed.ToString("R", CultureInfo.InvariantCulture)) != (digits, exponent)) {
                throw new JsonException("Double precision conversion loses precision.");
            }
            result = parsed;
        }
        return true;
    }

    // 仮数と指数を正規化して桁落ちを比較する
    private static (string digits, int exponent) Normalize(string text) {
        int separator = text.IndexOfAny(['e', 'E']);
        int exponent = 0;
        if (separator >= 0) {
            if (!int.TryParse(text.AsSpan(separator + 1), NumberStyles.Integer, CultureInfo.InvariantCulture, out exponent)) {
                throw new JsonException("Numeric exponent is out of range.");
            }
            text = text[..separator];
        }
        bool negative = text.StartsWith('-');
        if (negative) { text = text[1..]; }
        int point = text.IndexOf('.');
        if (point >= 0) {
            exponent = checked(exponent - (text.Length - point - 1));
            text = text.Remove(point, 1);
        }
        text = text.TrimStart('0');
        if (text.Length == 0) { return ("0", 0); }
        int length = text.TrimEnd('0').Length;
        exponent = checked(exponent + text.Length - length);
        text = text[..length];
        return (negative ? "-" + text : text, exponent);
    }
}
