namespace NEMEngine;

// 生成された Rigidbody に物理操作の facade を足す
public readonly unsafe partial struct Rigidbody {

    // 力を蓄積する、次の物理ステップで質量に応じて速度へ反映される
    public void AddForce(Vector3 force) {
        AccumulatedForce += force;
    }
}

// 生成された Rigidbody2D に物理操作の facade を足す
public readonly unsafe partial struct Rigidbody2D {

    // 力を蓄積する、次の物理ステップで質量に応じて速度へ反映される
    public void AddForce(Vector2 force) {
        AccumulatedForce += force;
    }
}
