using NEMEngine;

public sealed class SubmitMenu : MonoBehaviour {

    [SerializeField] private GameObject? menu;
    private UIImageButton? imageButton;
    private UITextButton? textButton;

    void Start() {
        // 画像ボタン・文字ボタンのどちらでも同じ処理を使う
        imageButton = GetComponent<UIImageButton>();
        textButton = GetComponent<UITextButton>();
    }

    void Update() {
        // 決定したframeだけメニュー全体を閉じる
        bool submitted = imageButton != null && imageButton.WasClicked ||
            textButton != null && textButton.WasClicked;
        if (submitted && menu != null) menu.SetActive(false);
    }
}
