using NEMEngine;

namespace NeecSample;

public sealed class BoardExample : MonoBehaviour {
    private readonly NeecBoard board = new();
    private bool wasNear;
    private string lastError = "";

    private void Start() => board.Connect();

    private void Update() {
        if (lastError != board.Error) {
            lastError = board.Error;
            Debug.Log(lastError);
        }
        if (!board.IsConnected) return;
        if (board.Switch.WasPressedThisFrame) Debug.Log("スイッチが押されました");
        bool near = board.TryGetDistanceMm(out int mm) && mm <= 120;
        if (near == wasNear) return;
        wasNear = near;
        board.SetLedColor(near ? (byte)0 : (byte)255, 255, 0);
        board.SetServoAngle(near ? 90 : 10);
    }

    private void OnDestroy() => board.Dispose();
}

