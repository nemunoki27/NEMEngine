using NEMEngine;

public sealed class FirstMover : MonoBehaviour {

    [SerializeField] private float speed = 2.0f;

    void Update() {
        // 左右キーから移動方向を作る
        float horizontal = 0.0f;
        if (Input.GetKey(KeyCode.LeftArrow)) horizontal -= 1.0f;
        if (Input.GetKey(KeyCode.RightArrow)) horizontal += 1.0f;
        // 経過秒を掛けてWorld座標で移動
        transform.Translate(new Vector3(horizontal, 0.0f, 0.0f) * speed * Time.deltaTime, Space.World);
    }
}
