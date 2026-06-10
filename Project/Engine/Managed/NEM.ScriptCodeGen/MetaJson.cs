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
            try
            {
                object? value = ParseValue(text, ref pos);
                return value;
            }
            catch
            {
                return null;
            }
        }

        private static object? ParseValue(string s, ref int pos)
        {
            SkipWhitespace(s, ref pos);
            if (pos >= s.Length) { return null; }
            char c = s[pos];
            switch (c)
            {
                case '{': return ParseObject(s, ref pos);
                case '[': return ParseArray(s, ref pos);
                case '"': return ParseString(s, ref pos);
                case 't': pos += 4; return true;
                case 'f': pos += 5; return false;
                case 'n': pos += 4; return null;
                default: return ParseNumber(s, ref pos);
            }
        }

        private static Dictionary<string, object?> ParseObject(string s, ref int pos)
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
                if (pos < s.Length && s[pos] == ':') { pos++; }
                object? value = ParseValue(s, ref pos);
                result[key] = value;
                SkipWhitespace(s, ref pos);
                if (pos < s.Length && s[pos] == ',') { pos++; continue; }
                if (pos < s.Length && s[pos] == '}') { pos++; break; }
                break;
            }
            return result;
        }

        private static List<object?> ParseArray(string s, ref int pos)
        {
            var result = new List<object?>();
            pos++; // '['
            SkipWhitespace(s, ref pos);
            if (pos < s.Length && s[pos] == ']') { pos++; return result; }
            while (pos < s.Length)
            {
                object? value = ParseValue(s, ref pos);
                result.Add(value);
                SkipWhitespace(s, ref pos);
                if (pos < s.Length && s[pos] == ',') { pos++; continue; }
                if (pos < s.Length && s[pos] == ']') { pos++; break; }
                break;
            }
            return result;
        }

        private static string ParseString(string s, ref int pos)
        {
            var sb = new StringBuilder();
            pos++; // opening quote
            while (pos < s.Length)
            {
                char c = s[pos++];
                if (c == '"') { break; }
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
                            break;
                        default: sb.Append(e); break;
                    }
                }
                else
                {
                    sb.Append(c);
                }
            }
            return sb.ToString();
        }

        private static object ParseNumber(string s, ref int pos)
        {
            int start = pos;
            while (pos < s.Length && (char.IsDigit(s[pos]) || s[pos] == '-' || s[pos] == '+' || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E'))
            {
                pos++;
            }
            string token = s.Substring(start, pos - start);
            return double.TryParse(token, NumberStyles.Any, CultureInfo.InvariantCulture, out double value) ? value : 0.0;
        }

        private static void SkipWhitespace(string s, ref int pos)
        {
            while (pos < s.Length && char.IsWhiteSpace(s[pos])) { pos++; }
        }
    }
}
