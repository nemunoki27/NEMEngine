using System.Globalization;
using System.Text;

namespace NEMEngine;

//============================================================================
//	CsvDocument class
//	見出しと行列でCSVの文字列データを保持する
//============================================================================
public sealed class CsvDocument {

	internal CsvDocument(List<string[]> rows, bool hasHeader) {
		headers = hasHeader && rows.Count > 0 ? Array.AsReadOnly(rows[0]) : Array.Empty<string>();
		if (hasHeader && rows.Count > 0) rows.RemoveAt(0);
		this.rows = rows.Select(row => (IReadOnlyList<string>)Array.AsReadOnly(row)).ToArray();
	}

	public IReadOnlyList<string> headers { get; }
	public IReadOnlyList<IReadOnlyList<string>> rows { get; }
	public string this[int row, int column] => rows[row][column];
	public string this[int row, string column] => rows[row][GetColumnIndex(column)];

	// 同名の見出しは先頭の列を使用する
	public int GetColumnIndex(string name) {
		ArgumentNullException.ThrowIfNull(name);
		for (int i = 0; i < headers.Count; ++i) if (headers[i] == name) return i;
		throw new KeyNotFoundException($"CSVの列がありません: {name}");
	}

	public bool TryGetInt32(int row, int column, out int value) =>
		int.TryParse(this[row, column], NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
	public bool TryGetSingle(int row, int column, out float value) =>
		float.TryParse(this[row, column], NumberStyles.Float, CultureInfo.InvariantCulture, out value);
}

//============================================================================
//	CsvUtility class
//	引用符とセル内改行を保持してCSVを読み書きする
//============================================================================
public static class CsvUtility {

	// 型変換せず空欄と空白をそのまま保持する
	public static CsvDocument Parse(string text, bool hasHeader = false) {
		ArgumentNullException.ThrowIfNull(text);
		var rows = new List<string[]>();
		var row = new List<string>();
		var field = new StringBuilder();
		bool quoted = false, closed = false, pending = false;
		int line = 1, column = 0;
		for (int i = text.Length > 0 && text[0] == '\uFEFF' ? 1 : 0; i < text.Length; ++i) {
			char c = text[i];
			++column;
			if (quoted) {
				if (c == '"') {
					if (i + 1 < text.Length && text[i + 1] == '"') { field.Append('"'); ++i; ++column; }
					else { quoted = false; closed = true; }
				} else {
					field.Append(c);
					if (c == '\n') { ++line; column = 0; }
				}
				continue;
			}
			if (closed && c != ',' && c != '\r' && c != '\n') throw Error(line, column);
			if (c == '"') {
				if (field.Length != 0) throw Error(line, column);
				quoted = true;
				pending = true;
			} else if (c == ',') {
				row.Add(field.ToString()); field.Clear(); closed = false; pending = true;
			} else if (c == '\r' || c == '\n') {
				if (c == '\r' && i + 1 < text.Length && text[i + 1] == '\n') ++i;
				row.Add(field.ToString()); rows.Add(row.ToArray()); row.Clear(); field.Clear();
				closed = false; pending = false; ++line; column = 0;
			} else { field.Append(c); pending = true; }
		}
		if (quoted) throw Error(line, column + 1);
		if (pending || row.Count != 0) { row.Add(field.ToString()); rows.Add(row.ToArray()); }
		return new CsvDocument(rows, hasHeader);
	}

	// 必要なセルだけ引用符で囲む
	public static string Write(IEnumerable<IEnumerable<string>> rows) {
		ArgumentNullException.ThrowIfNull(rows);
		var output = new StringBuilder();
		foreach (var row in rows) {
			bool first = true;
			foreach (string value in row) {
				if (!first) output.Append(',');
				first = false;
				string cell = value ?? string.Empty;
				if (cell.IndexOfAny([',', '"', '\r', '\n']) >= 0) output.Append('"').Append(cell.Replace("\"", "\"\"")).Append('"');
				else output.Append(cell);
			}
			output.Append("\r\n");
		}
		return output.ToString();
	}

	public static string Write(CsvDocument document) {
		ArgumentNullException.ThrowIfNull(document);
		IEnumerable<IEnumerable<string>> rows = document.rows;
		return Write(document.headers.Count > 0 ? new[] { document.headers }.Concat(rows) : rows);
	}

	// 不正な引用符の位置を通知する
	private static FormatException Error(int line, int column) => new($"CSVの形式が不正です: {line}行 {column}列");
}
