using System.Text;

namespace NEMEngine;

//============================================================================
//	TextAsset class
//	CSVとJSONの内容をAsset参照から読み込む
//============================================================================
[NativeAssetType("Text")]
public sealed class TextAsset : Asset {

	private byte[]? cachedBytes;
	private string? cachedText;
	internal TextAsset(AssetGUID id) : base(id) { }
	public string text => cachedText ??= Decode(ReadBytes());
	public byte[] bytes => (byte[])ReadBytes().Clone();
	public int dataSize => ReadBytes().Length;

	// 読み込み済みの内容を次のアクセス時に更新する
	public void Reload() { cachedBytes = null; cachedText = null; }

	// Assetの元ファイルは読み取り専用で扱う
	private unsafe byte[] ReadBytes() {
		if (cachedBytes != null) return cachedBytes;
		if (NativeAPI.CopyTextAssetBytes == null) throw new NotSupportedException("TextAsset APIが接続されていません");
		for (int attempt = 0; attempt < 3; ++attempt) {
			int size = NativeAPI.CopyTextAssetBytes(id, null, 0);
			if (size < 0 || size > 64 * 1024 * 1024) throw new IOException("テキストAssetを読み込めません");
			byte[] bytes = new byte[size];
			fixed (byte* data = bytes) {
				int written = NativeAPI.CopyTextAssetBytes(id, data, bytes.Length);
				if (written < 0) throw new IOException("テキストAssetを読み込めません");
				if (written > size) continue;
				if (written < size) Array.Resize(ref bytes, written);
				return cachedBytes = bytes;
			}
		}
		throw new IOException("読み込み中にテキストAssetが変更されました");
	}

	// UTF-8のBOMは文字列へ含めない
	private static string Decode(byte[] bytes) {
		int offset = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
		return new UTF8Encoding(false, true).GetString(bytes, offset, bytes.Length - offset);
	}
}
