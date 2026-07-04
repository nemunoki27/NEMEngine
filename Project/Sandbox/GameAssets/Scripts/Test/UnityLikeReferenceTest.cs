namespace GameScripts;

// Unityライクな参照型システムの動作確認用スクリプト
//
// 使い方:
//   1. MeshRendererを持つEntityへこのスクリプトをattachする(子Entityが1つ以上あるとInChildren系も確認できる)
//   2. 同じEntityか別EntityへReferenceTestTargetをattachし、otherTargetフィールドへ割り当てる
//   3. 各シリアライズフィールドをインスペクターから割り当てる(未割り当てはSKIP扱いで進む)
//   4. Playすると各項目が[PASS]/[FAIL]/[SKIP]でConsoleへ出力され、最後に集計が出る
//
// 確認できる内容:
//   - 型宣言だけのシリアライズフィールド(アセット/コンポーネント/スクリプト/Entity/コレクション)
//   - シーンロード時の参照解決(スクリプト参照はAwake前に解決される)
//   - fake-null(未割り当て・破棄済み参照の == null 判定)
//   - GetComponent系の統一API(組込みcomponentとスクリプトの両対応)
//   - アセットの実型プロパティ(MeshRenderer.Mesh等)のget/set
//   - AddComponent/RemoveComponent/Instantiate/Destroyの遅延適用
public class UnityLikeReferenceTest : ScriptBehaviour {

    //========================================================================
    //	シリアライズフィールド(インスペクター表示と保存の確認対象)
    //========================================================================

    [SeparatorText("アセット参照")]
    [SerializeField] private Mesh? meshAsset;
    [SerializeField] private Material? materialAsset;
    [SerializeField] private Texture? textureAsset;
    [SerializeField] private AudioClip? audioAsset;
    [SerializeField] private Font? fontAsset;
    [SerializeField] private AnimationClip? animationAsset;
    [SerializeField] private Prefab? spawnPrefab;
    [SerializeField] private SceneAsset? sceneAsset;

    [SeparatorText("コンポーネント/スクリプト/Entity参照")]
    // 別Entityの組込みcomponentへの参照
    [SerializeField] private MeshRenderer? otherRenderer;
    [SerializeField] private Transform? otherTransform;
    // 別Entityのスクリプトへの参照
    [SerializeField] private ReferenceTestTarget? otherTarget;
    [SerializeField] private Entity targetEntity;

    [SeparatorText("コレクション")]
    [SerializeField] private List<Material?>? materialList;
    [SerializeField] private Prefab?[]? prefabArray;

    [SeparatorText("スカラー(既存機能の互換確認)")]
    [Range(0.0f, 10.0f)]
    [SerializeField] private float moveSpeed = 1.0f;
    [SerializeField] private BlendMode blendMode = BlendMode.Normal;
    [SerializeField] private Vector3 offset = Vector3.zero;
    [SerializeField] private Color4 tintColor = Color4.white;
    [SerializeField] private int? optionalCount;

    //========================================================================
    //	テスト状態
    //========================================================================

    // 集計
    private int passCount;
    private int failCount;
    private int skipCount;

    // Awake時点でスクリプト参照が解決済みだったか(参照解決はlifecycle前に行われる)
    private bool otherTargetResolvedAtAwake;

    // 遅延適用の確認用
    private Entity spawnedEntity;
    private Transform? spawnedTransform;
    private bool addComponentRequested;
    private int frameCount;
    private bool finished;

    public override void Awake() {

		// シーンロード時の参照解決はAwakeより前に完了している(遅延解決キューのflush確認)
		otherTargetResolvedAtAwake = otherTarget != null;
    }

    public override void Start() {

        Debug.Log("===== UnityLikeReferenceTest 開始 =====");

        //--------------------------------------------------------------------
        // 1. シリアライズフィールドの割り当て状態
        //--------------------------------------------------------------------
        Debug.Log("--- 1. シリアライズフィールドの解決状態 ---");
        LogAsset("meshAsset", meshAsset);
        LogAsset("materialAsset", materialAsset);
        LogAsset("textureAsset", textureAsset);
        LogAsset("audioAsset", audioAsset);
        LogAsset("fontAsset", fontAsset);
        LogAsset("animationAsset", animationAsset);
        LogAsset("spawnPrefab", spawnPrefab);
        LogAsset("sceneAsset", sceneAsset);

		Debug.Log($"otherRenderer: {(otherRenderer != null ? otherRenderer.entity.name : "null")}");
        Debug.Log($"otherTransform: {(otherTransform != null ? otherTransform.entity.name : "null")}");
        Debug.Log($"otherTarget: {(otherTarget != null ? $"{otherTarget.entity.name} (label={otherTarget.label})" : "null")}");
        Debug.Log($"targetEntity: {(targetEntity.isAlive ? targetEntity.name : "null")}");
        Debug.Log($"materialList: {(materialList != null ? $"{materialList.Count}件" : "null")}");
        Debug.Log($"prefabArray: {(prefabArray != null ? $"{prefabArray.Length}件" : "null")}");
        Debug.Log($"scalars: moveSpeed={moveSpeed} blendMode={blendMode} offset=({offset.x},{offset.y},{offset.z}) optionalCount={(optionalCount.HasValue ? optionalCount.Value.ToString() : "null")}");

        //--------------------------------------------------------------------
        // 2. スクリプト参照の解決タイミングと参照経由の呼び出し
        //--------------------------------------------------------------------
        Debug.Log("--- 2. スクリプト参照 ---");
        if (otherTarget != null) {
            Check("スクリプト参照はAwake前に解決済み", otherTargetResolvedAtAwake);
            Check("参照経由のメソッド呼び出し", otherTarget.Ping() == 1);
            Check("参照先のGetComponent<Transform>", otherTarget.GetComponent<Transform>() != null);
        } else {
            Skip("スクリプト参照(otherTarget未割り当て)");
        }

        //--------------------------------------------------------------------
        // 3. fake-null(未割り当てはnull扱い)
        //--------------------------------------------------------------------
        Debug.Log("--- 3. fake-null ---");
        UnityLikeReferenceTest? nullScript = null;
        Check("null参照 == null", nullScript == null);
        // 無効Entityを指すwrapperはnull扱いになる
        Transform deadTransform = Entity.nullEntity.transform;
        Check("無効Entityのwrapper == null", deadTransform == null);
        Check("無効Entityのwrapper != null は false", !(deadTransform != null));

        //--------------------------------------------------------------------
        // 4. GetComponent統一API(組込みcomponentとスクリプト)
        //--------------------------------------------------------------------
        Debug.Log("--- 4. GetComponent統一API ---");
        Check("GetComponent<Transform>", GetComponent<Transform>() != null);
        Check("HasComponent<Transform>", HasComponent<Transform>());
        Check("TryGetComponent<Transform>", TryGetComponent(out Transform _));
        Check("GetComponent<自スクリプト型>", GetComponent<UnityLikeReferenceTest>() == this);

        MeshRenderer? selfRenderer = GetComponent<MeshRenderer>();
        if (selfRenderer != null) {
            Check("GetComponent<MeshRenderer>", true);
            // 同一Entity・同型のwrapperは等値になる
            Check("wrapperの等値比較", selfRenderer == GetComponent<MeshRenderer>());
        } else {
            Skip("GetComponent<MeshRenderer>(自EntityにMeshRendererなし)");
        }

        //--------------------------------------------------------------------
        // 5. 階層を辿るGetComponent
        //--------------------------------------------------------------------
        Debug.Log("--- 5. 階層走査 ---");
        Check("GetComponentInParent<Transform>", GetComponentInParent<Transform>() != null);
        Check("GetComponentInChildren<Transform>", GetComponentInChildren<Transform>() != null);
        List<Transform> childTransforms = GetComponentsInChildren<Transform>();
        Debug.Log($"GetComponentsInChildren<Transform>: {childTransforms.Count}件(自身+子孫)");
        Check("GetComponentsInChildrenが自身を含む", childTransforms.Count >= 1);
        List<UnityLikeReferenceTest> childScripts = GetComponentsInChildren<UnityLikeReferenceTest>();
        Check("GetComponentsInChildren<スクリプト型>", childScripts.Count >= 1);

        //--------------------------------------------------------------------
        // 6. アセット参照のプロパティと等値
        //--------------------------------------------------------------------
        Debug.Log("--- 6. アセット参照 ---");
        if (meshAsset != null) {
            Check("Asset.exists", meshAsset.exists);
            Check("Asset.name", !string.IsNullOrEmpty(meshAsset.name));
        } else {
            Skip("Asset.exists/name(meshAsset未割り当て)");
        }
        if (selfRenderer != null && meshAsset != null) {
            // 実型プロパティのset/getラウンドトリップ、別インスタンスでもUUIDが同じなら等値
            selfRenderer.Mesh = meshAsset;
            Check("MeshRenderer.Meshのset/get等値", selfRenderer.Mesh == meshAsset);
        } else {
            Skip("MeshRenderer.Meshラウンドトリップ");
        }
        if (selfRenderer != null && materialAsset != null) {
            selfRenderer.Material = materialAsset;
            Check("MeshRenderer.Materialのset/get等値", selfRenderer.Material == materialAsset);
        } else {
            Skip("MeshRenderer.Materialラウンドトリップ");
        }

        //--------------------------------------------------------------------
        // 7. World検索(組込みcomponentとスクリプトの両方)
        //--------------------------------------------------------------------
        Debug.Log("--- 7. World検索 ---");
        Check("FindEntityWithComponent<Transform>", World.FindEntityWithComponent<Transform>().isAlive);
        Check("FindEntityWithComponent<スクリプト型>", World.FindEntityWithComponent<UnityLikeReferenceTest>() == entity);
        Check("FindObjectOfType<スクリプト型>", World.FindObjectOfType<UnityLikeReferenceTest>() == this);

        //--------------------------------------------------------------------
        // 8. 遅延適用系の開始(結果は後続フレームのUpdateで確認)
        //--------------------------------------------------------------------
        Debug.Log("--- 8. 遅延適用系(数フレーム後に判定) ---");
        if (!HasComponent<LineRenderer>()) {
            LineRenderer? added = AddComponent<LineRenderer>();
            addComponentRequested = added != null;
            Check("AddComponentがwrapperを返す", addComponentRequested);
            Check("AddComponent直後はflush前で未反映", !HasComponent<LineRenderer>());
        } else {
            Skip("AddComponent(既にLineRendererあり)");
        }
        if (spawnPrefab != null) {
            spawnedEntity = Instantiate(spawnPrefab, transform.position + offset, Quaternion.identity);
            Check("Instantiateが予約Entityを返す", spawnedEntity.isAlive);
        } else {
            Skip("Instantiate(spawnPrefab未割り当て)");
        }
    }

    public override void Update() {

        if (finished) {
            return;
        }
        ++frameCount;

        // flushを跨いだ後に遅延適用の結果を確認する
        if (frameCount == 3) {

            Debug.Log("--- 8a. 遅延適用の反映確認(3フレーム目) ---");
            if (addComponentRequested) {
                Check("AddComponentがflush後に反映", HasComponent<LineRenderer>());
                RemoveComponent<LineRenderer>();
            }
            if (spawnedEntity.isAlive) {
                Check("Instantiateしたprefabが実体化", true);
                // 破棄後のfake-null確認用に、生成EntityのTransform wrapperを保持してから破棄する
                spawnedTransform = spawnedEntity.transform;
                Check("生成Entityのwrapper取得", spawnedTransform != null);
                Destroy(spawnedEntity);
            } else if (spawnPrefab != null) {
                Check("Instantiateしたprefabが実体化", false);
            }
        }

        if (frameCount == 6) {

            Debug.Log("--- 8b. 破棄と削除の反映確認(6フレーム目) ---");
            if (addComponentRequested) {
                Check("RemoveComponentがflush後に反映", !HasComponent<LineRenderer>());
            }
            if (spawnedTransform != null || spawnPrefab != null) {
                Check("破棄済みEntityのwrapperがfake-nullになる", spawnedTransform == null);
                Check("破棄済みEntityのisAliveがfalse", !spawnedEntity.isAlive);
            }

            //----------------------------------------------------------------
            // 集計
            //----------------------------------------------------------------
            finished = true;
            string result = failCount == 0 ? "全PASS" : "FAILあり";
            Debug.Log($"===== UnityLikeReferenceTest 完了: {result} (PASS={passCount} FAIL={failCount} SKIP={skipCount}) =====");
            Debug.Log("Play中インスペクターで各参照フィールドの現在値(参照先名)が表示されることも確認してください");
        }
    }

    //========================================================================
    //	helpers
    //========================================================================

    // 判定結果をログへ出す
    private void Check(string label, bool condition) {

        if (condition) {
            ++passCount;
            Debug.Log($"[PASS] {label}");
        } else {
            ++failCount;
            Debug.LogError($"[FAIL] {label}");
        }
    }

    // 未割り当て等で確認できない項目
    private void Skip(string label) {

        ++skipCount;
        Debug.LogWarning($"[SKIP] {label}");
    }

    // アセットフィールドの状態を表示名付きでログへ出す
    private void LogAsset(string fieldName, Asset? asset) {

        Debug.Log($"{fieldName}: {(asset != null ? $"{asset.name} (exists={asset.exists})" : "null")}");
    }
}
