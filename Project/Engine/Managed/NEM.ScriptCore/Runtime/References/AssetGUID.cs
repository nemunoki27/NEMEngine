using System.Globalization;
using System.Runtime.InteropServices;

namespace NEMEngine;

// C++側 Engine::AssetGUIDと対応する128bitアセットID
[StructLayout(LayoutKind.Sequential)]
public readonly struct AssetGUID : IEquatable<AssetGUID> {

    public readonly ulong high;
    public readonly ulong low;

    public AssetGUID(ulong high, ulong low) {
        this.high = high;
        this.low = low;
    }

    public bool isValid => high != 0ul || low != 0ul;

    public static AssetGUID None => new(0ul, 0ul);

    public override string ToString() =>
        high.ToString("x16", CultureInfo.InvariantCulture) +
        low.ToString("x16", CultureInfo.InvariantCulture);

    public static AssetGUID Parse(string? text) {

        if (string.IsNullOrWhiteSpace(text) || text.Length != 32 ||
            !ulong.TryParse(text.AsSpan(0, 16), NumberStyles.AllowHexSpecifier,
                CultureInfo.InvariantCulture, out ulong high) ||
            !ulong.TryParse(text.AsSpan(16, 16), NumberStyles.AllowHexSpecifier,
                CultureInfo.InvariantCulture, out ulong low)) {
            return None;
        }
        return new AssetGUID(high, low);
    }

    public bool Equals(AssetGUID other) => high == other.high && low == other.low;
    public override bool Equals(object? obj) => obj is AssetGUID other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(high, low);
    public static bool operator ==(AssetGUID a, AssetGUID b) => a.Equals(b);
    public static bool operator !=(AssetGUID a, AssetGUID b) => !a.Equals(b);
}
