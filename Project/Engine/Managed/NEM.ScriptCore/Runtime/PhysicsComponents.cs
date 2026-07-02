namespace NEMEngine;

// 力やトルクの加え方、Unityの ForceMode 相当
public enum ForceMode {

    // 質量を考慮した連続的な力、毎ステップ蓄積される
    Force = 0,
    // 質量を考慮した瞬間的な力、その場で速度へ加わる
    Impulse = 1,
    // 質量を無視した瞬間的な速度変化
    VelocityChange = 2,
    // 質量を無視した連続的な加速度
    Acceleration = 5,
}

// 生成された Rigidbody に物理操作の facade を足す
public sealed unsafe partial class Rigidbody {

    // 力を加える、mode で連続/瞬間と質量の扱いを切り替える
    public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force) {

        float m = Mass > 0.0f ? Mass : 1.0f;
        switch (mode) {
        case ForceMode.Force: AccumulatedForce += force; break;
        case ForceMode.Acceleration: AccumulatedForce += force * m; break;
        case ForceMode.Impulse: LinearVelocity += force / m; break;
        case ForceMode.VelocityChange: LinearVelocity += force; break;
        }
    }

    // トルクを加える、回転軸×大きさのベクトルで指定する
    public void AddTorque(Vector3 torque, ForceMode mode = ForceMode.Force) {

        float m = Mass > 0.0f ? Mass : 1.0f;
        switch (mode) {
        case ForceMode.Force: AccumulatedTorque += torque; break;
        case ForceMode.Acceleration: AccumulatedTorque += torque * m; break;
        case ForceMode.Impulse: AngularVelocity += torque / m; break;
        case ForceMode.VelocityChange: AngularVelocity += torque; break;
        }
    }
}

// 生成された Rigidbody2D に物理操作の facade を足す
public sealed unsafe partial class Rigidbody2D {

    // XY平面の力を加える、mode で連続/瞬間と質量の扱いを切り替える
    public void AddForce(Vector2 force, ForceMode mode = ForceMode.Force) {

        float m = Mass > 0.0f ? Mass : 1.0f;
        switch (mode) {
        case ForceMode.Force: AccumulatedForce += force; break;
        case ForceMode.Acceleration: AccumulatedForce += force * m; break;
        case ForceMode.Impulse: LinearVelocity += force / m; break;
        case ForceMode.VelocityChange: LinearVelocity += force; break;
        }
    }

    // Z軸まわりのトルクを加える、正で反時計回り
    public void AddTorque(float torque, ForceMode mode = ForceMode.Force) {

        float m = Mass > 0.0f ? Mass : 1.0f;
        switch (mode) {
        case ForceMode.Force: AccumulatedTorque += torque; break;
        case ForceMode.Acceleration: AccumulatedTorque += torque * m; break;
        case ForceMode.Impulse: AngularVelocity += torque / m; break;
        case ForceMode.VelocityChange: AngularVelocity += torque; break;
        }
    }
}
