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

    private GroupCollection? groups_;
    public GroupCollection Groups => groups_ ??= new GroupCollection(this);
    public int GroupCount => NativeApi.EffectGroupCountCall(entity.native);

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

    public sealed class GroupCollection {

        private readonly EffectEmitter emitter;

        internal GroupCollection(EffectEmitter emitter) {
            this.emitter = emitter;
        }

        public int Count => emitter.GroupCount;
        public GroupRef this[int index] => new(emitter, index);

        public GroupRef? Find(string name) {

            for (int i = 0; i < Count; ++i) {
                GroupRef group = this[i];
                if (group.Name == name) { return group; }
            }
            return null;
        }
    }

    public sealed class GroupRef {

        private readonly EffectEmitter emitter;
        private readonly int index;
        private StateCollection? states_;

        internal GroupRef(EffectEmitter emitter, int index) {
            this.emitter = emitter;
            this.index = index;
        }

        public string Name => NativeApi.EffectGroupNameCall(emitter.entity.native, index);
        public int StateCount => NativeApi.EffectStateCountCall(emitter.entity.native, index);
        public StateCollection States => states_ ??= new StateCollection(emitter, index);
    }

    public sealed class StateCollection {

        private readonly EffectEmitter emitter;
        private readonly int groupIndex;

        internal StateCollection(EffectEmitter emitter, int groupIndex) {
            this.emitter = emitter;
            this.groupIndex = groupIndex;
        }

        public int Count => NativeApi.EffectStateCountCall(emitter.entity.native, groupIndex);
        public StateRef this[int index] => new(emitter, groupIndex, index);

        public StateRef? Find(string name) {

            for (int i = 0; i < Count; ++i) {
                StateRef state = this[i];
                if (state.Name == name) { return state; }
            }
            return null;
        }
    }

    public sealed class StateRef {

        private const int EnabledProperty = 0;
        private const int EffectProperty = 1;
        private const int ModeProperty = 2;
        private const int DelayProperty = 3;
        private const int CountProperty = 4;
        private const int IntervalProperty = 5;
        private const int DurationProperty = 6;
        private const int EmitUntilStoppedProperty = 7;
        private const int LocalPositionProperty = 8;
        private const int LocalRotationProperty = 9;
        private const int LocalScaleProperty = 10;
        private const int UseEmitterParentProperty = 11;
        private const int ParentEntityProperty = 12;
        private const int IgnoreParentRotationProperty = 13;
        private const int IgnoreParentScaleProperty = 14;
        private const int KeepWorldOnDetachProperty = 15;

        private readonly EffectEmitter emitter;
        private readonly int groupIndex;
        private readonly int stateIndex;

        internal StateRef(EffectEmitter emitter, int groupIndex, int stateIndex) {
            this.emitter = emitter;
            this.groupIndex = groupIndex;
            this.stateIndex = stateIndex;
        }

        private T Get<T>(int property) where T : unmanaged =>
            NativeApi.EffectGetStatePropertyCall<T>(emitter.entity.native, groupIndex, stateIndex, property);

        private void Set<T>(int property, T value) where T : unmanaged =>
            NativeApi.EffectSetStatePropertyCall(emitter.entity.native, groupIndex, stateIndex, property, value);

        public string Name {
            get => NativeApi.EffectStateNameCall(emitter.entity.native, groupIndex, stateIndex);
            set => NativeApi.EffectSetStateNameCall(emitter.entity.native, groupIndex, stateIndex, value);
        }

        public bool Enabled {
            get => Get<int>(EnabledProperty) != 0;
            set => Set(EnabledProperty, value ? 1 : 0);
        }

        public ParticleEffect? Effect {
            get {
                ulong value = Get<ulong>(EffectProperty);
                return value != 0ul ? new ParticleEffect(new UUID(value)) : null;
            }
            set => Set(EffectProperty, value != null ? value.assetId.value : 0ul);
        }

        public EffectEmitterMode Mode {
            get => (EffectEmitterMode)Get<int>(ModeProperty);
            set => Set(ModeProperty, (int)value);
        }

        public float Delay {
            get => Get<float>(DelayProperty);
            set => Set(DelayProperty, value);
        }

        public int Count {
            get => Get<int>(CountProperty);
            set => Set(CountProperty, value);
        }

        public float Interval {
            get => Get<float>(IntervalProperty);
            set => Set(IntervalProperty, value);
        }

        public float Duration {
            get => Get<float>(DurationProperty);
            set => Set(DurationProperty, value);
        }

        public bool EmitUntilStopped {
            get => Get<int>(EmitUntilStoppedProperty) != 0;
            set => Set(EmitUntilStoppedProperty, value ? 1 : 0);
        }

        public Vector3 LocalPosition {
            get => Get<NativeVector3>(LocalPositionProperty).ToVector3();
            set => Set(LocalPositionProperty, NativeVector3.From(value));
        }

        public Quaternion LocalRotation {
            get => Get<NativeQuaternion>(LocalRotationProperty).ToQuaternion();
            set => Set(LocalRotationProperty, NativeQuaternion.From(value));
        }

        public Vector3 LocalScale {
            get => Get<NativeVector3>(LocalScaleProperty).ToVector3();
            set => Set(LocalScaleProperty, NativeVector3.From(value));
        }

        public bool UseEmitterAsParent {
            get => Get<int>(UseEmitterParentProperty) != 0;
            set => Set(UseEmitterParentProperty, value ? 1 : 0);
        }

        public Entity Parent {
            get => new(Get<NativeEntity>(ParentEntityProperty));
            set => Set(ParentEntityProperty, value.native);
        }

        public bool IgnoreParentRotation {
            get => Get<int>(IgnoreParentRotationProperty) != 0;
            set => Set(IgnoreParentRotationProperty, value ? 1 : 0);
        }

        public bool IgnoreParentScale {
            get => Get<int>(IgnoreParentScaleProperty) != 0;
            set => Set(IgnoreParentScaleProperty, value ? 1 : 0);
        }

        public bool KeepWorldOnDetach {
            get => Get<int>(KeepWorldOnDetachProperty) != 0;
            set => Set(KeepWorldOnDetachProperty, value ? 1 : 0);
        }

        public void SetEmitterParent() => UseEmitterAsParent = true;

        public void SetParent(Entity parent) => Parent = parent;

        public void ClearParent() {
            UseEmitterAsParent = false;
            Parent = Entity.nullEntity;
        }
    }
}
