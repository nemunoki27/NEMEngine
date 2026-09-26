using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace NEM.ScriptCodeGen
{
    // source generator(netstandard2.0)は System.Text.Json を手軽に同梱できないため、
    // .cs.meta を読むための依存無しの最小 JSON parser を用意する。
    // 返す値は Dictionary<string, object?> / List<object?> / string / double / bool / null。
    internal static class MetaJson
    {
        public static object? Parse(string text)
        {
            int pos = 0;
            object? value = ParseValue(text, ref pos, 0);
            SkipWhitespace(text, ref pos);
            if (pos != text.Length) { throw new FormatException("Unexpected JSON suffix."); }
            return value;
        }

        private static object? ParseValue(string s, ref int pos, int depth)
        {
            if (depth > 64) { throw new FormatException("Metadata is too deeply nested."); }
            SkipWhitespace(s, ref pos);
            if (pos >= s.Length) { throw new FormatException("Missing JSON value."); }
            char c = s[pos];
            switch (c)
            {
                case '{': return ParseObject(s, ref pos, depth + 1);
                case '[': return ParseArray(s, ref pos, depth + 1);
                case '"': return ParseString(s, ref pos);
                case 't': Consume(s, ref pos, "true"); return true;
                case 'f': Consume(s, ref pos, "false"); return false;
                case 'n': Consume(s, ref pos, "null"); return null;
                default: return ParseNumber(s, ref pos);
            }
        }

        private static Dictionary<string, object?> ParseObject(string s, ref int pos, int depth)
        {
            var result = new Dictionary<string, object?>(StringComparer.Ordinal);
            pos++; // '{'
            SkipWhitespace(s, ref pos);
            if (pos < s.Length && s[pos] == '}') { pos++; return result; }
            while (pos < s.Length)
            {
                SkipWhitespace(s, ref pos);
                string key = ParseString(s, ref pos);
                SkipWhitespace(s, ref pos);
                Consume(s, ref pos, ":");
                object? value = ParseValue(s, ref pos, depth);
                if (result.ContainsKey(key)) { throw new FormatException("Duplicate JSON key: " + key); }
                result.Add(key, value);
                SkipWhitespace(s, ref pos);
                if (pos < s.Length && s[pos] == ',') { pos++; continue; }
                if (pos < s.Length && s[pos] == '}') { pos++; return result; }
                throw new FormatException("Missing JSON object delimiter.");
            }
            throw new FormatException("Unclosed JSON object.");
        }

        private static List<object?> ParseArray(string s, ref int pos, int depth)
        {
            var result = new List<object?>();
            pos++; // '['
            SkipWhitespace(s, ref pos);
            if (pos < s.Length && s[pos] == ']') { pos++; return result; }
            while (pos < s.Length)
            {
                object? value = ParseValue(s, ref pos, depth);
                result.Add(value);
                SkipWhitespace(s, ref pos);
                if (pos < s.Length && s[pos] == ',') { pos++; continue; }
                if (pos < s.Length && s[pos] == ']') { pos++; return result; }
                throw new FormatException("Missing JSON array delimiter.");
            }
            throw new FormatException("Unclosed JSON array.");
        }

        private static string ParseString(string s, ref int pos)
        {
            var sb = new StringBuilder();
            Consume(s, ref pos, "\"");
            while (pos < s.Length)
            {
                char c = s[pos++];
                if (c == '"') { return sb.ToString(); }
                if (c < ' ') { throw new FormatException("Control character in JSON string."); }
                if (c == '\\' && pos < s.Length)
                {
                    char e = s[pos++];
                    switch (e)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'u':
                            if (pos + 4 <= s.Length &&
                                ushort.TryParse(s.Substring(pos, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out ushort code))
                            {
                                sb.Append((char)code);
                                pos += 4;
                            }
                            else { throw new FormatException("Invalid Unicode escape."); }
                            break;
                        default: throw new FormatException("Invalid JSON escape.");
                    }
                }
                else
                {
                    sb.Append(c);
                }
            }
            throw new FormatException("Unclosed JSON string.");
        }

        private static object ParseNumber(string s, ref int pos)
        {
            int start = pos;
            if (pos < s.Length && s[pos] == '-') { ++pos; }
            if (pos < s.Length && s[pos] == '0') { ++pos; }
            else { ReadDigits(s, ref pos); }
            if (pos < s.Length && s[pos] == '.') { ++pos; ReadDigits(s, ref pos); }
            if (pos < s.Length && (s[pos] == 'e' || s[pos] == 'E')) {
                ++pos;
                if (pos < s.Length && (s[pos] == '+' || s[pos] == '-')) { ++pos; }
                ReadDigits(s, ref pos);
            }
            string token = s.Substring(start, pos - start);
            if (!double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out double value) ||
                double.IsInfinity(value) || double.IsNaN(value)) { throw new FormatException("Invalid JSON number."); }
            return value;
        }

        private static void ReadDigits(string text, ref int pos) {
            int start = pos;
            while (pos < text.Length && text[pos] >= '0' && text[pos] <= '9') { ++pos; }
            if (start == pos) { throw new FormatException("Missing JSON digits."); }
        }

        private static void Consume(string text, ref int pos, string token) {
            if (pos + token.Length > text.Length || string.CompareOrdinal(text, pos, token, 0, token.Length) != 0) {
                throw new FormatException("Expected JSON token: " + token);
            }
            pos += token.Length;
        }

        private static void SkipWhitespace(string s, ref int pos)
        {
            while (pos < s.Length && (s[pos] == ' ' || s[pos] == '\r' || s[pos] == '\n' || s[pos] == '\t')) { pos++; }
        }
    }
}
