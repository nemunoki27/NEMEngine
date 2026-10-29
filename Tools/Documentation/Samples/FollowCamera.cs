using NEMEngine;

public sealed class FollowCamera : MonoBehaviour {

    [SerializeField] private GameObject? target;
    [SerializeField] private Vector3 offset = new Vector3(0.0f, 2.0f, -5.0f);

    void Start() {
        // 同じCameraControllerの追従と注視を有効にする
        var controller = GetComponent<CameraController>();
        if (controller == null || target == null) return;
        controller.Enabled = true;
        controller.Mode = CameraControlMode.FollowLookAt;

        // 対象の後方へ追従し、入力による回転を止める
        controller.FollowLookAt.Follow.Enabled = true;
        controller.FollowLookAt.Follow.Target = target;
        controller.FollowLookAt.Follow.Offset = offset;
        controller.FollowLookAt.Follow.PositionLerpSpeed = 5.0f;
        controller.FollowLookAt.Follow.EnableInputRotation = false;

        // 位置の追従先と同じ対象へ向きを合わせる
        controller.FollowLookAt.LookAt.Enabled = true;
        controller.FollowLookAt.LookAt.Target = target;
        controller.FollowLookAt.LookAt.LockRoll = true;
    }
}
