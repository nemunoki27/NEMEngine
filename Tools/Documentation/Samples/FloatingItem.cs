using NEMEngine;

public sealed class FloatingItem : MonoBehaviour {

    private SimpleAnimation? animation;

    void Start() {
        // 初期位置を中心に、2秒周期で上下へ揺らす
        animation = GetComponent<SimpleAnimation>();
        if (animation == null) return;
        animation.AutoApply = false;
        animation.SetSettings(new SimpleAnimationSettings<Vector3> {
            Mode = SimpleAnimationMode.Sin,
            Repeat = SimpleAnimationRepeat.Loop,
            Center = transform.localPosition,
            Amplitude = new Vector3(0, 0.3f, 0),
            Period = 2
        });
        animation.Play();
    }

    void LateUpdate() {
        // SimpleAnimationが更新した当frameの結果を受け取る
        if (animation != null && animation.TryGetValue<Vector3>(out var position))
            transform.localPosition = position;
    }
}
