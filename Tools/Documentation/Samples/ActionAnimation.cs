using NEMEngine;

public sealed class ActionAnimation : MonoBehaviour {

    [SerializeField] private string stateName = "Attack";
    private Animator? animator;

    void Start() {
        // AnimationPlayerのC#操作型を取得
        animator = GetComponent<Animator>();
    }

    void Update() {
        // 入力に応じて、Editorで設定したStateを再生
        if (animator != null && Input.GetKeyDown(KeyCode.E)) animator.Play(stateName);
    }
}
