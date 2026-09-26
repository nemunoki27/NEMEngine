using System.Diagnostics.CodeAnalysis;

namespace NEMEngine;

// Native個体の生存と参照の等値を扱う共通基底
public abstract class Object {

    internal abstract bool objectAlive { get; }

    // 同じ個体を指す参照だけを等値にする
    private protected virtual bool EqualsObject(Object other) => ReferenceEquals(this, other);

    public static bool operator ==(Object? lhs, Object? rhs) {

        // 実際のnullとの比較だけでNative側の生存を調べる
        if (lhs is null) {
            return rhs is null || !rhs.objectAlive;
        }
        if (rhs is null) {
            return !lhs.objectAlive;
        }
        return ReferenceEquals(lhs, rhs) || lhs.EqualsObject(rhs);
    }

    public static bool operator !=(Object? lhs, Object? rhs) => !(lhs == rhs);
    public static implicit operator bool([NotNullWhen(true)] Object? value) => value != null;

    public override bool Equals(object? obj) => obj is null ? this == null : obj is Object other && this == other;
    public override int GetHashCode() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);
}
