namespace NEMEngine;

// Asset / Component の共通基底。破棄済み参照をnull同然に扱う等値比較を提供する。
// Unityと同じく「参照は残っているが参照先が消えた」オブジェクトは obj == null がtrueになる。
// ただし ?. / ?? はこの比較を通らないため、破棄済み判定には == null を使う。
public abstract class Object {

    // 参照先がまだ有効か。無効ならnull比較でtrue扱いになる
    internal abstract bool objectAlive { get; }

    // 参照identityの比較。同じ参照先を指すインスタンス同士を等値にする
    private protected virtual bool EqualsObject(Object other) => ReferenceEquals(this, other);

    public static bool operator ==(Object? lhs, Object? rhs) {

        // どちらかがnull相当(参照なし or 参照先が無効)なら、両方null相当のときだけ等値
        bool lhsNull = lhs is null || !lhs.objectAlive;
        bool rhsNull = rhs is null || !rhs.objectAlive;
        if (lhsNull || rhsNull) {
            return lhsNull == rhsNull;
        }
        return ReferenceEquals(lhs, rhs) || lhs!.EqualsObject(rhs!);
    }

    public static bool operator !=(Object? lhs, Object? rhs) => !(lhs == rhs);

    public override bool Equals(object? obj) => obj is Object other && this == other;

    public override int GetHashCode() => base.GetHashCode();
}
