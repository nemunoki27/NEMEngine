namespace NEMEngine;

// EffectEmitterの再生1回を識別するハンドル
public readonly struct EffectPlaybackHandle : System.IEquatable<EffectPlaybackHandle> {

    internal readonly ulong value;

    internal EffectPlaybackHandle(ulong value) {
        this.value = value;
    }

    public bool IsValid => value != 0ul;

    public bool Equals(EffectPlaybackHandle other) => value == other.value;
    public override bool Equals(object? obj) => obj is EffectPlaybackHandle other && Equals(other);
    public override int GetHashCode() => value.GetHashCode();
    public static bool operator ==(EffectPlaybackHandle left, EffectPlaybackHandle right) => left.Equals(right);
    public static bool operator !=(EffectPlaybackHandle left, EffectPlaybackHandle right) => !left.Equals(right);
}

// 自動生成されるEffectEmitter wrapperのgameplay method拡張
public sealed partial class EffectEmitter {

    public EffectPlaybackHandle Emit() => new(NativeApi.EffectEmitCall(entity.native, string.Empty, default, default, false));

    public EffectPlaybackHandle Emit(string group) => new(NativeApi.EffectEmitCall(entity.native, group, default, default, false));

    public EffectPlaybackHandle EmitAt(Vector3 position, Quaternion rotation) =>
        new(NativeApi.EffectEmitCall(entity.native, string.Empty, position, rotation, true));

    public EffectPlaybackHandle EmitAt(string group, Vector3 position, Quaternion rotation) =>
        new(NativeApi.EffectEmitCall(entity.native, group, position, rotation, true));

    public void Stop(EffectPlaybackHandle handle) => NativeApi.EffectStopHandleCall(entity.native, handle.value);
    public void Stop(string group) => NativeApi.EffectStopGroupCall(entity.native, group);
    public void Stop() => NativeApi.EffectStopAllCall(entity.native);

    public void Clear(EffectPlaybackHandle handle) => NativeApi.EffectClearHandleCall(entity.native, handle.value);
    public void Clear(string group) => NativeApi.EffectClearGroupCall(entity.native, group);
    public void Clear() => NativeApi.EffectClearAllCall(entity.native);

    public bool IsPlaying(EffectPlaybackHandle handle) => NativeApi.EffectIsPlayingHandleCall(entity.native, handle.value);
    public bool IsPlaying(string group) => NativeApi.EffectIsPlayingGroupCall(entity.native, group);
    public bool IsAnyPlaying => NativeApi.EffectIsPlayingAllCall(entity.native);
}
