namespace NEMEngine;

// Nativeの接続表を所有する
internal static unsafe class NativeAPI {

    // 実行中の更新停止要求を返す
    internal static delegate* unmanaged[Cdecl]<int> IsUpdateInterrupted;
    // 倍率を適用した差分時刻を返す
    internal static delegate* unmanaged[Cdecl]<float> GetDeltaTime;
    // 固定更新の差分時刻を返す
    internal static delegate* unmanaged[Cdecl]<float> GetFixedDeltaTime;
    // C#のログをNativeへ渡す
    internal static delegate* unmanaged[Cdecl]<int, byte*, void> Log;
    // キーの押下状態を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetKey;
    // キーの押下開始を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetKeyDown;
    // キーの解放を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetKeyUp;
    // マウスボタンの押下状態を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButton;
    // マウスボタンの押下開始を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButtonDown;
    // マウスボタンの解放を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButtonUp;
    // マウスの位置を返す
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetMousePosition;
    // マウスの移動量を返す
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetMouseDelta;
    // マウスのホイール量を返す
    internal static delegate* unmanaged[Cdecl]<float> GetMouseWheel;
    // ゲームパッドのボタン状態を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetGamepadButton;
    // ゲームパッドの押下開始を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetGamepadButtonDown;
    // ゲームパッドの接続状態を返す
    internal static delegate* unmanaged[Cdecl]<int> IsGamepadConnected;
    // 左スティックの入力を返す
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetLeftStick;
    // 右スティックの入力を返す
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetRightStick;
    // 左トリガーの入力を返す
    internal static delegate* unmanaged[Cdecl]<float> GetLeftTrigger;
    // 右トリガーの入力を返す
    internal static delegate* unmanaged[Cdecl]<float> GetRightTrigger;
    // Entityの生存状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> IsAlive;
    // Entity名を指定バッファへコピーする
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopyName;
    // Entity名を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> SetName;
    // Entity自身の有効状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetActiveSelf;
    // Entity自身の有効状態を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetActiveSelf;
    // 親階層を含む有効状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetActiveInHierarchy;
    // 親Entityを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetParent;
    // 最初の子Entityを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetFirstChild;
    // 次の兄弟Entityを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetNextSibling;
    // 親変更をCommandへ登録する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, void> SetParent;
    // ワールド位置を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetPosition;
    // ワールド位置を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetPosition;
    // ローカル位置を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLocalPosition;
    // ローカル位置を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetLocalPosition;
    // ローカル拡縮率を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLocalScale;
    // ローカル拡縮率を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetLocalScale;
    // ローカル回転を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> GetLocalRotation;
    // ローカル回転を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> SetLocalRotation;
    // ワールド回転を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> GetRotation;
    // ワールド回転を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> SetRotation;
    // ワールド拡縮率を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLossyScale;
    // Componentの有無を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int> HasComponent;
    // Componentの実行IDを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, ulong> GetComponentInstanceID;
    // Component追加を予約する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> AddComponent;
    // Component削除を予約する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> RemoveComponent;
    // Entityの破棄を予約する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> DestroyEntity;
    // Scriptの有効状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, int> GetScriptEnabled;
    // Scriptの有効状態を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, int, void> SetScriptEnabled;
    // Entity上のScriptを型IDで検索する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, NativeScriptInstanceHandle> GetScriptInstance;
    // EntityへScriptを追加する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, NativeScriptInstanceHandle> AttachScript;
    // EntityからScriptを削除する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, void> RemoveScript;
    // Componentの値を取得する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> GetComponentProperty;
    // Componentの値を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> SetComponentProperty;
    // Componentの文字列を取得する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, byte*, int, int*,
        int> GetComponentStringProperty;
    // Componentの文字列を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, byte*, int, int> SetComponentStringProperty;
    // 倍率適用前の差分時刻を返す
    internal static delegate* unmanaged[Cdecl]<float> GetUnscaledDeltaTime;
    // 倍率適用前の固定差分時刻を返す
    internal static delegate* unmanaged[Cdecl]<float> GetUnscaledFixedDeltaTime;
    // 倍率適用後の経過時間を返す
    internal static delegate* unmanaged[Cdecl]<double> GetTimeSinceStartup;
    // 倍率適用前の経過時間を返す
    internal static delegate* unmanaged[Cdecl]<double> GetUnscaledTime;
    // 時間倍率を返す
    internal static delegate* unmanaged[Cdecl]<float> GetTimeScale;
    // 時間倍率を設定する
    internal static delegate* unmanaged[Cdecl]<float, void> SetTimeScale;
    // 実行中のフレーム数を返す
    internal static delegate* unmanaged[Cdecl]<ulong> GetFrameCount;
    // Assetの存在を確認する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, int> AssetExists;
    // Assetの表示名をコピーする
    internal static delegate* unmanaged[Cdecl]<AssetGUID, byte*, int, int> CopyAssetDisplayName;
    // Entityの生成を予約する
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity, NativeEntity> CreateEntity;
    // Prefabの実体生成を予約する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, NativeVector3, NativeQuaternion, int, NativeEntity,
        NativeEntity> InstantiatePrefab;
    // Entity階層の複製を予約する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, NativeQuaternion, int, NativeEntity,
        NativeEntity> InstantiateEntity;
    // Sceneの追加読込を要求する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong> LoadSceneAdditive;
    // Sceneの単独読込を要求する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong> LoadSceneSingle;
    // Sceneの事前読込を要求する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong> PreloadScene;
    // Active Sceneの再読込を要求する
    internal static delegate* unmanaged[Cdecl]<ulong> ReloadActiveScene;
    // Sceneの解放を要求する
    internal static delegate* unmanaged[Cdecl]<ulong, void> UnloadScene;
    // Scene実体の生存状態を返す
    internal static delegate* unmanaged[Cdecl]<ulong, int> IsSceneInstanceAlive;
    // 座標保持を指定して親変更を予約する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, int, void> SetParentKeepWorld;
    // 指定パッドのボタン状態を返す
    internal static delegate* unmanaged[Cdecl]<int, int, int> GetGamepadButtonIndexed;
    // 指定パッドの押下開始を返す
    internal static delegate* unmanaged[Cdecl]<int, int, int> GetGamepadButtonDownIndexed;
    // 指定パッドの解放を返す
    internal static delegate* unmanaged[Cdecl]<int, int, int> GetGamepadButtonUpIndexed;
    // 指定パッドの軸値を返す
    internal static delegate* unmanaged[Cdecl]<int, int, float> GetGamepadAxisIndexed;
    // 指定パッドの接続状態を返す
    internal static delegate* unmanaged[Cdecl]<int, int> IsGamepadConnectedIndexed;
    // 接続中のパッド数を返す
    internal static delegate* unmanaged[Cdecl]<int> GetConnectedGamepadCount;
    // アプリのフォーカス状態を返す
    internal static delegate* unmanaged[Cdecl]<int> GetHasFocus;
    // 入力された文字列をコピーする
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> CopyTextInput;
    // Projectのルートをコピーする
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> CopyProjectRoot;
    // ユーザー設定のルートをコピーする
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> CopyUserSettingsRoot;
    // Audioの再生を開始する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioPlay;
    // Audioの再生を一時停止する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioPause;
    // Audioの再生を終了する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioStop;
    // Audioの再生状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> AudioIsPlaying;
    // Audioを一度だけ重ねて再生する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, AssetGUID, float, void> AudioPlayOneShot;
    // Audioの一時停止を解除する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioUnPause;
    // Script例外をNativeへ報告する
    internal static delegate* unmanaged[Cdecl]<byte*, void> ReportScriptException;
    // 保存参照を対象Entityへ解決する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong, NativeEntity, NativeEntity> ResolveEntityRef;
    // Lineの点列を置き換える
    internal static delegate* unmanaged[Cdecl]<NativeEntity, LinePoint*, int, int, void> LineSetPoints;
    // 点列から即時Lineを描く
    internal static delegate* unmanaged[Cdecl]<LinePoint*, int, int, int, AssetGUID, void> LineDrawImmediate;
    // 球形の即時Lineを描く
    internal static delegate* unmanaged[Cdecl]<NativeVector3, float, NativeColor4, int, float, AssetGUID,
        void> LineDrawSphereImmediate;
    // Lineへ点を追加する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, LinePoint, int> LineAddPoint;
    // Lineの指定点を更新する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, LinePoint, void> LineUpdatePoint;
    // EntityのTagをコピーする
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopyTag;
    // EntityのTagを設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> SetTag;
    // 描画対象のLayerを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetVisibilityLayerMask;
    // 描画対象のLayerを設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetVisibilityLayerMask;
    // CollisionのLayerを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetCollisionTypeMask;
    // Collisionの接触状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetCollisionRuntimeState;
    // CollisionのLayerを設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetCollisionTypeMask;
    // 名前でEntityを検索する
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity> FindEntityByName;
    // TagでEntityを検索する
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity> FindEntityByTag;
    // TagでEntity一覧を取得する
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity*, int, int> FindEntitiesByTag;
    // ComponentでEntityを検索する
    internal static delegate* unmanaged[Cdecl]<int, NativeEntity> FindEntityByComponent;
    // ComponentでEntity一覧を取得する
    internal static delegate* unmanaged[Cdecl]<int, NativeEntity*, int, int> FindEntitiesByComponent;
    // 指定形状の即時Lineを描く
    internal static delegate* unmanaged[Cdecl]<NativeLineShape*, void> LineDrawShape;
    // 親回転の継承除外状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetIgnoreParentRotation;
    // 親回転の継承除外を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetIgnoreParentRotation;
    // 親拡縮率の継承除外状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetIgnoreParentScale;
    // 親拡縮率の継承除外を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetIgnoreParentScale;

    // 現在の入力種別を返す
    internal static delegate* unmanaged[Cdecl]<int> GetInputType;
    // RendererのMaterial値を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, byte*, NativeMaterialParameterValue*,
        int> SetRendererMaterialParameter;
    // RendererのMaterial値を取得する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, NativeMaterialParameterValue*,
        int> GetRendererMaterialParameter;
    // RendererのMaterial値を解除する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, int> ClearRendererMaterialParameter;
    // GPUのRayTracing対応を返す
    internal static delegate* unmanaged[Cdecl]<int> IsRayTracingSupported;
    // RayTracingの実行状態を返す
    internal static delegate* unmanaged[Cdecl]<int> IsRayTracingActive;
    // 名前から描画Passと世代を解決する
    internal static delegate* unmanaged[Cdecl]<byte*, ulong*, ulong*, int> ResolveRenderFeaturePass;
    // 描画Passの世代を検証する
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int> ValidateRenderFeaturePass;
    // 実行中の描画Passを切り替える
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int, int> SetRenderFeaturePassEnabled;
    // 描画Passの画面出力を切り替える
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int, int> SetRenderFeaturePassSceneColorOutput;
    // 実行中の描画Groupを切り替える
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> SetRenderFeatureGroupEnabled;
    // 実行中の描画Pass値を設定する
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, ulong, byte*, NativeMaterialParameterValue*,
        int> SetRenderFeaturePassParameter;
    // 実行中の描画Pass値を取得する
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, ulong, NativeMaterialParameterValue*,
        int> GetRenderFeaturePassParameter;
    // 実行中の描画Pass値を解除する
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, ulong, int> ClearRenderFeaturePassParameter;
    // 描画Passの実行変更を解除する
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int> ResetRenderFeaturePass;
    // 描画構成の実行変更を解除する
    internal static delegate* unmanaged[Cdecl]<void> ResetRenderFeatureOverrides;
    // マウス範囲制御の状態を返す
    internal static delegate* unmanaged[Cdecl]<int> GetMouseRangeControl;
    // マウス範囲制御を設定する
    internal static delegate* unmanaged[Cdecl]<int, void> SetMouseRangeControl;
    // Entityの保存参照IDを取得する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, AssetGUID*, ulong*, int*, void> GetEntityReferenceIdentity;
    // レイに最も近い接触を返す
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, uint, uint, NativeRaycastHit*,
        int> PhysicsRaycast;
    // レイの全接触を距離順で返す
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, uint, uint, NativeRaycastHit*,
        int, int> PhysicsRaycastAll;
    // QueryのTrigger対象設定を返す
    internal static delegate* unmanaged[Cdecl]<int> GetQueriesHitTriggers;
    // QueryのTrigger対象設定を変更する
    internal static delegate* unmanaged[Cdecl]<int, void> SetQueriesHitTriggers;
    // 画面座標からワールドレイを作る
    internal static delegate* unmanaged[Cdecl]<float, float, NativeVector3*, NativeVector3*, int> ScreenPointToRay;
    // View内のマウス座標を返す
    internal static delegate* unmanaged[Cdecl]<NativeVector2*, int> GetMousePositionInView;
    // 名前からCollisionのマスクを取得する
    internal static delegate* unmanaged[Cdecl]<byte*, uint> GetCollisionTypeMaskByName;
    // 指定した補間曲線の値を返す
    internal static delegate* unmanaged[Cdecl]<int, float, float> EasedValue;
    // Colliderの形状値を取得する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void*, int, int> CollisionGetShapeProperty;
    // Colliderの形状値を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void*, int, int> CollisionSetShapeProperty;
    // 指定Clipの再生時間を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, float> GetSkinnedAnimationDuration;
    // AnimatorのParameterを設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, float, int, int> SetAnimatorParameter;
    // AnimatorのParameterを取得する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, float*, int*, int> GetAnimatorParameter;
    // 指定Clipを先頭から再生する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> PlaySkinnedAnimation;
    // 再生中のClip名をコピーする
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopySkinnedAnimationCurrentClip;
    // 骨格アニメーションの実行状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeSkinnedAnimationRuntimeState*,
        int> GetSkinnedAnimationRuntimeState;
    // Particleの再生操作を予約する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int, void> ParticleSystemControl;
    // Particleの実行状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int> ParticleSystemState;
    // Bufferの要素数を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int> DynamicBufferLength;
    // Bufferの要素を指定範囲へコピーする
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int, void*, int, int> DynamicBufferCopy;
    // Bufferへ要素の変更を適用する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int, int, void*, int, int> DynamicBufferMutate;
    // Playerのゲーム入力遮断状態を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetUIBlocksGameplayInput;
    // Playerのパッド割当を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetPlayerGamepadIndex;
    // Playerのキーとマウスの割当を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetPlayerKeyboardMouseEnabled;
    // Playerの入力可否を返す
    internal static delegate* unmanaged[Cdecl]<int, int> GetPlayerInputAvailable;
    // Playerのパッド振動を開始する
    internal static delegate* unmanaged[Cdecl]<int, float, float, float, float, float, uint> PlayPlayerVibration;
    // Playerのパッド振動を止める
    internal static delegate* unmanaged[Cdecl]<int, uint, void> StopPlayerVibration;
    // UIの選択状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeUISelectableRuntimeState*,
        int> GetUISelectableRuntimeState;
    // UIの補間済み表示値を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeUIProgressRuntimeState*,
        int> GetUIProgressRuntimeState;
    // Canvasの入力遮断状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetCanvasInputLocked;
    // UIボタンのクリック状態を返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int> GetUIButtonClicked;
    // Canvasの入力割当をコピーする
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int*, int, int> CanvasCopyInputBindings;
    // Canvasの入力割当を設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int*, int, void> CanvasSetInputBindings;
    // Canvasの選択表サイズを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int*, int*, int> CanvasGetNavigationTableSize;
    // Canvasの選択表サイズを変更する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int> CanvasResizeNavigationTable;
    // Canvasの選択先Entityを返す
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, NativeEntity*, int> CanvasGetNavigationCell;
    // Canvasの選択先Entityを設定する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, NativeEntity, int> CanvasSetNavigationCell;
    // フレーム終端の終了を要求する
    internal static delegate* unmanaged[Cdecl]<void> RequestApplicationQuit;
    // ワールド座標を画面座標へ変換する
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3*, int> WorldToScreenPoint;
    // 画面座標をCanvas座標へ変換する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector2, NativeVector2*,
        int> CanvasScreenToLocalPoint;

    // Scriptの詳細計測を開始する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, byte*, ulong> BeginScriptSample;
    // Scriptの詳細計測を終了する
    internal static delegate* unmanaged[Cdecl]<ulong, void> EndScriptSample;
    // Scene切替時にEntityを保持する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> DontDestroyOnLoad;

    // 検証済みのNative接続を保持する
    internal static void SetCallbacks(NativeAPITable* callbacks) {
        BeginScriptSample = callbacks->beginScriptSample;
        EndScriptSample = callbacks->endScriptSample;
        DontDestroyOnLoad = callbacks->dontDestroyOnLoad;

        // C++側から渡されたECSアクセス用の関数テーブルを保持する
        GetDeltaTime = callbacks->getDeltaTime;
        GetFixedDeltaTime = callbacks->getFixedDeltaTime;
        Log = callbacks->log;
        GetKey = callbacks->getKey;
        GetKeyDown = callbacks->getKeyDown;
        GetKeyUp = callbacks->getKeyUp;
        GetMouseButton = callbacks->getMouseButton;
        GetMouseButtonDown = callbacks->getMouseButtonDown;
        GetMouseButtonUp = callbacks->getMouseButtonUp;
        GetMousePosition = callbacks->getMousePosition;
        GetMouseDelta = callbacks->getMouseDelta;
        GetMouseWheel = callbacks->getMouseWheel;
        GetGamepadButton = callbacks->getGamepadButton;
        GetGamepadButtonDown = callbacks->getGamepadButtonDown;
        IsGamepadConnected = callbacks->isGamepadConnected;
        GetLeftStick = callbacks->getLeftStick;
        GetRightStick = callbacks->getRightStick;
        GetLeftTrigger = callbacks->getLeftTrigger;
        GetRightTrigger = callbacks->getRightTrigger;
        IsAlive = callbacks->isAlive;
        CopyName = callbacks->copyName;
        SetName = callbacks->setName;
        GetActiveSelf = callbacks->getActiveSelf;
        SetActiveSelf = callbacks->setActiveSelf;
        GetActiveInHierarchy = callbacks->getActiveInHierarchy;
        GetParent = callbacks->getParent;
        GetFirstChild = callbacks->getFirstChild;
        GetNextSibling = callbacks->getNextSibling;
        SetParent = callbacks->setParent;
        GetPosition = callbacks->getPosition;
        SetPosition = callbacks->setPosition;
        GetLocalPosition = callbacks->getLocalPosition;
        SetLocalPosition = callbacks->setLocalPosition;
        GetLocalScale = callbacks->getLocalScale;
        SetLocalScale = callbacks->setLocalScale;
        GetLocalRotation = callbacks->getLocalRotation;
        SetLocalRotation = callbacks->setLocalRotation;
        GetRotation = callbacks->getRotation;
        SetRotation = callbacks->setRotation;
        GetLossyScale = callbacks->getLossyScale;
        HasComponent = callbacks->hasComponent;
        GetComponentInstanceID = callbacks->getComponentInstanceID;
        AddComponent = callbacks->addComponent;
        RemoveComponent = callbacks->removeComponent;
        DestroyEntity = callbacks->destroyEntity;
        GetScriptEnabled = callbacks->getScriptEnabled;
        SetScriptEnabled = callbacks->setScriptEnabled;
        GetScriptInstance = callbacks->getScriptInstance;
        AttachScript = callbacks->attachScript;
        RemoveScript = callbacks->removeScript;
        GetComponentProperty = callbacks->getComponentProperty;
        SetComponentProperty = callbacks->setComponentProperty;
        GetComponentStringProperty = callbacks->getComponentStringProperty;
        SetComponentStringProperty = callbacks->setComponentStringProperty;
        GetUnscaledDeltaTime = callbacks->getUnscaledDeltaTime;
        GetUnscaledFixedDeltaTime = callbacks->getUnscaledFixedDeltaTime;
        GetTimeSinceStartup = callbacks->getTimeSinceStartup;
        GetUnscaledTime = callbacks->getUnscaledTime;
        GetTimeScale = callbacks->getTimeScale;
        SetTimeScale = callbacks->setTimeScale;
        GetFrameCount = callbacks->getFrameCount;
        AssetExists = callbacks->assetExists;
        CopyAssetDisplayName = callbacks->copyAssetDisplayName;
        CreateEntity = callbacks->createEntity;
        InstantiatePrefab = callbacks->instantiatePrefab;
        InstantiateEntity = callbacks->instantiateEntity;
        LoadSceneAdditive = callbacks->loadSceneAdditive;
        LoadSceneSingle = callbacks->loadSceneSingle;
        PreloadScene = callbacks->preloadScene;
        ReloadActiveScene = callbacks->reloadActiveScene;
        UnloadScene = callbacks->unloadScene;
        IsSceneInstanceAlive = callbacks->isSceneInstanceAlive;
        SetParentKeepWorld = callbacks->setParentKeepWorld;
        GetGamepadButtonIndexed = callbacks->getGamepadButtonIndexed;
        GetGamepadButtonDownIndexed = callbacks->getGamepadButtonDownIndexed;
        GetGamepadButtonUpIndexed = callbacks->getGamepadButtonUpIndexed;
        GetGamepadAxisIndexed = callbacks->getGamepadAxis;
        IsGamepadConnectedIndexed = callbacks->isGamepadConnectedIndexed;
        GetConnectedGamepadCount = callbacks->getConnectedGamepadCount;
        GetHasFocus = callbacks->getHasFocus;
        CopyTextInput = callbacks->copyTextInput;
        CopyProjectRoot = callbacks->copyProjectRoot;
        CopyUserSettingsRoot = callbacks->copyUserSettingsRoot;
        AudioPlay = callbacks->audioPlay;
        AudioPause = callbacks->audioPause;
        AudioStop = callbacks->audioStop;
        AudioIsPlaying = callbacks->audioIsPlaying;
        ReportScriptException = callbacks->reportScriptException;
        IsUpdateInterrupted = callbacks->isUpdateInterrupted;
        ResolveEntityRef = callbacks->resolveEntityRef;
        LineSetPoints = callbacks->lineSetPoints;
        LineDrawImmediate = callbacks->lineDrawImmediate;
        LineDrawSphereImmediate = callbacks->lineDrawSphereImmediate;
        LineAddPoint = callbacks->lineAddPoint;
        LineUpdatePoint = callbacks->lineUpdatePoint;
        CopyTag = callbacks->copyTag;
        SetTag = callbacks->setTag;
        GetVisibilityLayerMask = callbacks->getVisibilityLayerMask;
        SetVisibilityLayerMask = callbacks->setVisibilityLayerMask;
        GetCollisionTypeMask = callbacks->getCollisionTypeMask;
        GetCollisionRuntimeState = callbacks->getCollisionRuntimeState;
        SetCollisionTypeMask = callbacks->setCollisionTypeMask;
        FindEntityByName = callbacks->findEntityByName;
        FindEntityByTag = callbacks->findEntityByTag;
        FindEntitiesByTag = callbacks->findEntitiesByTag;
        FindEntityByComponent = callbacks->findEntityByComponent;
        FindEntitiesByComponent = callbacks->findEntitiesByComponent;
        LineDrawShape = callbacks->lineDrawShape;
        GetIgnoreParentRotation = callbacks->getIgnoreParentRotation;
        SetIgnoreParentRotation = callbacks->setIgnoreParentRotation;
        GetIgnoreParentScale = callbacks->getIgnoreParentScale;
        SetIgnoreParentScale = callbacks->setIgnoreParentScale;
        GetInputType = callbacks->getInputType;
        GetMouseRangeControl = callbacks->getMouseRangeControl;
        SetMouseRangeControl = callbacks->setMouseRangeControl;
        SetRendererMaterialParameter = callbacks->setRendererMaterialParameter;
        GetRendererMaterialParameter = callbacks->getRendererMaterialParameter;
        ClearRendererMaterialParameter = callbacks->clearRendererMaterialParameter;
        IsRayTracingSupported = callbacks->isRayTracingSupported;
        IsRayTracingActive = callbacks->isRayTracingActive;
        ResolveRenderFeaturePass = callbacks->resolveRenderFeaturePass;
        ValidateRenderFeaturePass = callbacks->validateRenderFeaturePass;
        SetRenderFeaturePassEnabled = callbacks->setRenderFeaturePassEnabled;
        SetRenderFeaturePassSceneColorOutput = callbacks->setRenderFeaturePassSceneColorOutput;
        SetRenderFeatureGroupEnabled = callbacks->setRenderFeatureGroupEnabled;
        SetRenderFeaturePassParameter = callbacks->setRenderFeaturePassParameter;
        GetRenderFeaturePassParameter = callbacks->getRenderFeaturePassParameter;
        ClearRenderFeaturePassParameter = callbacks->clearRenderFeaturePassParameter;
        ResetRenderFeaturePass = callbacks->resetRenderFeaturePass;
        ResetRenderFeatureOverrides = callbacks->resetRenderFeatureOverrides;
        GetEntityReferenceIdentity = callbacks->getEntityReferenceIdentity;
        PhysicsRaycast = callbacks->physicsRaycast;
        PhysicsRaycastAll = callbacks->physicsRaycastAll;
        GetQueriesHitTriggers = callbacks->getQueriesHitTriggers;
        SetQueriesHitTriggers = callbacks->setQueriesHitTriggers;
        ScreenPointToRay = callbacks->screenPointToRay;
        GetMousePositionInView = callbacks->getMousePositionInView;
        GetCollisionTypeMaskByName = callbacks->getCollisionTypeMaskByName;
        EasedValue = callbacks->easedValue;
        CollisionGetShapeProperty = callbacks->collisionGetShapeProperty;
        CollisionSetShapeProperty = callbacks->collisionSetShapeProperty;
        GetSkinnedAnimationDuration = callbacks->getSkinnedAnimationDuration;
        SetAnimatorParameter = callbacks->setAnimatorParameter;
        GetAnimatorParameter = callbacks->getAnimatorParameter;
        PlaySkinnedAnimation = callbacks->playSkinnedAnimation;
        CopySkinnedAnimationCurrentClip = callbacks->copySkinnedAnimationCurrentClip;
        GetSkinnedAnimationRuntimeState = callbacks->getSkinnedAnimationRuntimeState;
        ParticleSystemControl = callbacks->particleSystemControl;
        ParticleSystemState = callbacks->particleSystemState;
        DynamicBufferLength = callbacks->dynamicBufferLength;
        DynamicBufferCopy = callbacks->dynamicBufferCopy;
        DynamicBufferMutate = callbacks->dynamicBufferMutate;
        GetUIBlocksGameplayInput = callbacks->getUIBlocksGameplayInput;
        GetPlayerGamepadIndex = callbacks->getPlayerGamepadIndex;
        GetPlayerKeyboardMouseEnabled = callbacks->getPlayerKeyboardMouseEnabled;
        GetPlayerInputAvailable = callbacks->getPlayerInputAvailable;
        PlayPlayerVibration = callbacks->playPlayerVibration;
        StopPlayerVibration = callbacks->stopPlayerVibration;
        GetUISelectableRuntimeState = callbacks->getUISelectableRuntimeState;
        GetUIProgressRuntimeState = callbacks->getUIProgressRuntimeState;
        GetCanvasInputLocked = callbacks->getCanvasInputLocked;
        GetUIButtonClicked = callbacks->getUIButtonClicked;
        CanvasCopyInputBindings = callbacks->canvasCopyInputBindings;
        CanvasSetInputBindings = callbacks->canvasSetInputBindings;
        CanvasGetNavigationTableSize = callbacks->canvasGetNavigationTableSize;
        CanvasResizeNavigationTable = callbacks->canvasResizeNavigationTable;
        CanvasGetNavigationCell = callbacks->canvasGetNavigationCell;
        CanvasSetNavigationCell = callbacks->canvasSetNavigationCell;
        RequestApplicationQuit = callbacks->requestApplicationQuit;
        WorldToScreenPoint = callbacks->worldToScreenPoint;
        CanvasScreenToLocalPoint = callbacks->canvasScreenToLocalPoint;
        AudioPlayOneShot = callbacks->audioPlayOneShot;
        AudioUnPause = callbacks->audioUnPause;
    }

}
