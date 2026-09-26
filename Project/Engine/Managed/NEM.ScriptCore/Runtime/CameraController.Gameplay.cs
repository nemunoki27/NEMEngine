namespace NEMEngine;

// 自動生成されるCameraController wrapperのゲームプレイ向けアクセサ
public sealed partial class CameraController {

    // 設定グループごとのアクセサ
    private FollowAccessor? follow_;
    private LookAtAccessor? lookAt_;
    private FollowLookAtAccessor? followLookAt_;

    public FollowAccessor Follow => follow_ ??= new FollowAccessor(this, false);
    public LookAtAccessor LookAt => lookAt_ ??= new LookAtAccessor(this, false);
    public FollowLookAtAccessor FollowLookAt => followLookAt_ ??= new FollowLookAtAccessor(this);

    // 追従設定
    public sealed class FollowAccessor {

        private readonly CameraController controller;
        private readonly bool combined;

        internal FollowAccessor(CameraController controller, bool combined) {
            this.controller = controller;
            this.combined = combined;
        }

        public bool Enabled {
            get => combined ? controller.FollowLookAtFollowEnabled : controller.FollowEnabled;
            set { if (combined) controller.FollowLookAtFollowEnabled = value; else controller.FollowEnabled = value; }
        }
        public GameObject? Target {
            get => combined ? controller.FollowLookAtFollowTarget : controller.FollowTarget;
            set { if (combined) controller.FollowLookAtFollowTarget = value; else controller.FollowTarget = value; }
        }
        public Vector3 Offset {
            get => combined ? controller.FollowLookAtFollowOffset : controller.FollowOffset;
            set { if (combined) controller.FollowLookAtFollowOffset = value; else controller.FollowOffset = value; }
        }
        public Vector3 AxisMask {
            get => combined ? controller.FollowLookAtFollowAxisMask : controller.FollowAxisMask;
            set { if (combined) controller.FollowLookAtFollowAxisMask = value; else controller.FollowAxisMask = value; }
        }
        public float PositionLerpSpeed {
            get => combined ? controller.FollowLookAtFollowPositionLerpSpeed : controller.FollowPositionLerpSpeed;
            set { if (combined) controller.FollowLookAtFollowPositionLerpSpeed = value; else controller.FollowPositionLerpSpeed = value; }
        }
        public bool EnableInputRotation {
            get => combined ? controller.FollowLookAtFollowEnableInputRotation : controller.FollowEnableInputRotation;
            set { if (combined) controller.FollowLookAtFollowEnableInputRotation = value; else controller.FollowEnableInputRotation = value; }
        }
        public float InputLerpRate {
            get => combined ? controller.FollowLookAtFollowInputLerpRate : controller.FollowInputLerpRate;
            set { if (combined) controller.FollowLookAtFollowInputLerpRate = value; else controller.FollowInputLerpRate = value; }
        }
        public Vector2 PadSensitivity {
            get => combined ? controller.FollowLookAtFollowPadSensitivity : controller.FollowPadSensitivity;
            set { if (combined) controller.FollowLookAtFollowPadSensitivity = value; else controller.FollowPadSensitivity = value; }
        }
        public Vector2 MouseSensitivity {
            get => combined ? controller.FollowLookAtFollowMouseSensitivity : controller.FollowMouseSensitivity;
            set { if (combined) controller.FollowLookAtFollowMouseSensitivity = value; else controller.FollowMouseSensitivity = value; }
        }
        public bool PadEnabled {
            get => combined ? controller.FollowLookAtFollowPadEnabled : controller.FollowPadEnabled;
            set { if (combined) controller.FollowLookAtFollowPadEnabled = value; else controller.FollowPadEnabled = value; }
        }
        public bool MouseEnabled {
            get => combined ? controller.FollowLookAtFollowMouseEnabled : controller.FollowMouseEnabled;
            set { if (combined) controller.FollowLookAtFollowMouseEnabled = value; else controller.FollowMouseEnabled = value; }
        }
        public bool AutoInputDevice {
            get => combined ? controller.FollowLookAtFollowAutoInputDevice : controller.FollowAutoInputDevice;
            set { if (combined) controller.FollowLookAtFollowAutoInputDevice = value; else controller.FollowAutoInputDevice = value; }
        }
        public bool InvertPitch {
            get => combined ? controller.FollowLookAtFollowInvertPitch : controller.FollowInvertPitch;
            set { if (combined) controller.FollowLookAtFollowInvertPitch = value; else controller.FollowInvertPitch = value; }
        }
    }

    // 注視設定
    public sealed class LookAtAccessor {

        private readonly CameraController controller;
        private readonly bool combined;

        internal LookAtAccessor(CameraController controller, bool combined) {
            this.controller = controller;
            this.combined = combined;
        }

        public bool Enabled {
            get => combined ? controller.FollowLookAtLookAtEnabled : controller.LookAtEnabled;
            set { if (combined) controller.FollowLookAtLookAtEnabled = value; else controller.LookAtEnabled = value; }
        }
        public GameObject? Target {
            get => combined ? controller.FollowLookAtLookAtTarget : controller.LookAtTarget;
            set { if (combined) controller.FollowLookAtLookAtTarget = value; else controller.LookAtTarget = value; }
        }
        public Vector3 Offset {
            get => combined ? controller.FollowLookAtLookAtOffset : controller.LookAtOffset;
            set { if (combined) controller.FollowLookAtLookAtOffset = value; else controller.LookAtOffset = value; }
        }
        public float RotationLerpSpeed {
            get => combined ? controller.FollowLookAtLookAtRotationLerpSpeed : controller.LookAtRotationLerpSpeed;
            set { if (combined) controller.FollowLookAtLookAtRotationLerpSpeed = value; else controller.LookAtRotationLerpSpeed = value; }
        }
        public bool LockRoll {
            get => combined ? controller.FollowLookAtLookAtLockRoll : controller.LookAtLockRoll;
            set { if (combined) controller.FollowLookAtLookAtLockRoll = value; else controller.LookAtLockRoll = value; }
        }
    }

    // 追従と注視の同時設定
    public sealed class FollowLookAtAccessor {

        private readonly FollowAccessor follow;
        private readonly LookAtAccessor lookAt;

        internal FollowLookAtAccessor(CameraController controller) {
            follow = new FollowAccessor(controller, true);
            lookAt = new LookAtAccessor(controller, true);
        }

        public FollowAccessor Follow => follow;
        public LookAtAccessor LookAt => lookAt;
    }
}
