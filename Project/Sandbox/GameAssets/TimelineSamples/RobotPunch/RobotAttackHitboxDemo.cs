using NEMEngine;

namespace SandboxScripts;

public sealed class RobotAttackHitboxDemo : MonoBehaviour {

    private RobotAttackDemo? owner;

    private void OnCollisionEnter(Collision collision) {
        ReportHit(collision);
    }

    private void OnCollisionStay(Collision collision) {
        // 判定開始時にすでに重なっている場合も、親側で重複を除いて通知する
        ReportHit(collision);
    }

    private void ReportHit(Collision collision) {
        owner ??= gameObject.GetComponentInParent<RobotAttackDemo>();
        owner?.RegisterHit(collision);
    }
}
