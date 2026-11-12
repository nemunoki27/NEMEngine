using NEMEngine;

namespace SandboxScripts;

public sealed class RobotAttackDemo : MonoBehaviour {

    [SerializeField]
    private bool frameCameraOnPlay = true;

    private TimelinePlayer? player;
    private CollisionComponent? hitbox;
    private ParticleSystem? sparks;
    private GameObject? target;
    private GameObject? demoCamera;
    private Vector3 originPosition;
    private Quaternion originRotation;
    private Vector3 cameraPosition;
    private Quaternion cameraRotation;
    private bool cameraCaptured;
    private bool wasActive;
    private bool hitRegistered;

    private void Awake() {
        // サンプル内の参照を一度だけ取得し、再実行用の開始姿勢を保持する
        player = GetComponent<TimelinePlayer>();
        hitbox = gameObject.GetComponentInChildren<CollisionComponent>(true);
        sparks = gameObject.GetComponentInChildren<ParticleSystem>(true);
        target = GameObject.Find("RobotAttackTarget");
        originPosition = transform.localPosition;
        originRotation = transform.localRotation;

        // 保存されたカメラを変更せず、実行用Worldだけで全身と的を収める
        if (frameCameraOnPlay) {
            demoCamera = GameObject.Find("Camera3D");
            if (demoCamera != null) {
                cameraPosition = demoCamera.transform.position;
                cameraRotation = demoCamera.transform.rotation;
                cameraCaptured = true;
                Vector3 origin = transform.position;
                Vector3 position = origin + new Vector3(11.0f, 7.0f, -11.0f);
                demoCamera.transform.position = position;
                demoCamera.transform.rotation = Quaternion.LookRotation(origin + new Vector3(0.0f, 3.0f, 2.0f) - position);
            }
        }
        Debug.Log("RobotPunch: 初回は自動再生します。GameViewをクリックし、Eキーで再実行できます");
    }

    private void Update() {
        if (player == null) return;
        TimelinePlayerState state = player.GetState();
        bool active = state.IsPlaying || state.IsPreparing;

        // 途中停止でも攻撃判定と残った粒子を片付ける
        if (wasActive && !active) ClearTransientObjects();
        wasActive = active;

        // 攻撃中の連打は無視し、完了後の入力だけで先頭から再実行する
        if (!active && Input.GetKeyDown(KeyCode.E)) {
            ClearTransientObjects();
            transform.localPosition = originPosition;
            transform.localRotation = originRotation;
            hitRegistered = false;
            player.Play();
            wasActive = true;
        }

        // 時刻の通知と、実際の衝突による命中通知は別々に扱う
        foreach (TimelineEvent signal in player.ConsumeEvents()) {
            Debug.Log($"RobotPunch / {signal.StringParam} / {signal.Time:F3}秒");
        }
    }

    public void RegisterHit(Collision collision) {
        // 的以外の接触と、同じ攻撃の重複接触を除く
        if (hitRegistered || target == null || collision.gameObject != target) return;
        hitRegistered = true;
        Debug.Log("RobotPunch / 命中: RobotAttackTarget / この攻撃では1回だけ通知します");
    }

    private void OnDisable() {
        // サンプルを無効にしたときも判定とカメラを元に戻す
        if (player != null) player.Stop();
        ClearTransientObjects();
        if (cameraCaptured && demoCamera != null) {
            demoCamera.transform.position = cameraPosition;
            demoCamera.transform.rotation = cameraRotation;
        }
        cameraCaptured = false;
    }

    private void ClearTransientObjects() {
        if (hitbox != null) hitbox.Enabled = false;
        if (sparks != null) sparks.Stop(false, ParticleSystemStopBehavior.StopEmittingAndClear);
    }
}
