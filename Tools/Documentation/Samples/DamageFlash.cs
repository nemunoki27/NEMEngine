using System.Collections;
using NEMEngine;

public sealed class DamageFlash : MonoBehaviour {

    [SerializeField, Min(0.01f)] private float duration = 0.15f;
    private MeshRenderer? renderer;
    private bool flashing;

    void Start() {
        // 被弾処理から使うRendererを保持
        renderer = GetComponent<MeshRenderer>();
    }

    public void Flash() {
        // 進行中の点滅を重ねて元の色を失わないようにする
        if (!flashing && renderer != null) StartCoroutine(FlashRoutine());
    }

    IEnumerator FlashRoutine() {
        if (renderer == null) yield break;
        flashing = true;

        // 部位ごとの元の色を保持して赤くする
        var colors = new Color4[renderer.GetSubMeshes().Count];
        for (int i = 0; i < colors.Length; ++i) {
            colors[i] = renderer.GetColor(i);
            renderer.SetColor(i, new Color4(1.0f, 0.1f, 0.1f, 1.0f));
        }
        yield return new WaitForSeconds(duration);

        // モデルやRendererが残っている範囲だけ元へ戻す
        if (renderer != null) {
            int count = Math.Min(colors.Length, renderer.GetSubMeshes().Count);
            for (int i = 0; i < count; ++i) renderer.SetColor(i, colors[i]);
        }
        flashing = false;
    }
}
