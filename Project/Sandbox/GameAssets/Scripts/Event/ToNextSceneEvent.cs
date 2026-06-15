using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	ToNextSceneEvent
//============================================================================
public sealed class ToNextSceneEvent : ScriptBehaviour
{
    [SerializeField]
    private AssetRef<SceneAsset> nextScene;
    // シーンリクエストフラグ
    private bool requested;

    //========================================================================
    //	毎フレーム更新処理
    //========================================================================
    public override void Update()
    {
        // 1キー入力でGameScene遷移
        if (Input.GetKeyDown(KeyCode.Alpha1))
        {
            LoadGameScene();
        }
    }

    //========================================================================
    //	GameSceneを読み込む
    //========================================================================
    private void LoadGameScene()
    {

        // 遷移不可の時は処理しない
        if (nextScene.IsNull || requested)
        {
            return;
        }
        SceneManager.LoadScene(nextScene, LoadSceneMode.Single);
        // シーンリクエスト済み
        requested = true;
    }
}