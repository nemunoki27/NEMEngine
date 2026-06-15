using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	ToGameEvent
//============================================================================
public sealed class ToGameEvent : ScriptBehaviour
{
    [SerializeField]
    private AssetRef<SceneAsset> gameScene;
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
        if (gameScene.IsNull || requested)
        {
            return;
        }
        SceneManager.LoadScene(gameScene, LoadSceneMode.Single);
        // シーンリクエスト済み
        requested = true;
    }
}
