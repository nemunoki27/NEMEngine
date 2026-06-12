using System.Globalization;

namespace NEMEngine;

// C++側 Engine::UUID（64bit）と対応する軽量 ID。
// scene/prefab の localFileID、asset ID、script slot ID など 64bit UUID の保存に使う。
// 文字列表現は engine と同じ 16 桁小文字 hex。0 は無効値。
public readonly struct UUID : IEquatable<UUID> {

    public readonly ulong value;

    public UUID(ulong value) {
        this.value = value;
    }

    public bool isValid => value != 0ul;

    public static UUID None => new(0ul);

    // 16 桁 hex（engine の UUID::ToString と一致）
    public override string ToString() => value.ToString("x16", CultureInfo.InvariantCulture);

    // 16 桁 hex 文字列を解釈する。空・不正は None。
    public static UUID Parse(string? text) {

        if (string.IsNullOrWhiteSpace(text)) {
            return None;
        }
        return ulong.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out ulong parsed)
            ? new UUID(parsed) : None;
    }

    public bool Equals(UUID other) => value == other.value;
    public override bool Equals(object? obj) => obj is UUID other && Equals(other);
    public override int GetHashCode() => value.GetHashCode();
    public static bool operator ==(UUID a, UUID b) => a.value == b.value;
    public static bool operator !=(UUID a, UUID b) => a.value != b.value;
}
