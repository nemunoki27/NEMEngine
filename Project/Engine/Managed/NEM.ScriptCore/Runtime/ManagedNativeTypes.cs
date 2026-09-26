using System.Runtime.InteropServices;


namespace NEMEngine;

// C++側 ManagedAbi と一致させるABI定数
internal static class ManagedAbi {

    // C++側 kManagedAbiVersion と一致させる
    // v2: managed script instance handle を int32 から NativeScriptInstanceHandle へ変更
    // v3: 型登録を CopyScriptTypeInfo(Stable GUID) へ変更し、GenerateScriptManifest を追加
    // v4: 固定長フィールドABIを撤廃し、二段階blob schema/runtime state API へ移行
    // v5: object model(generic component access / GameObject.Destroy / MonoBehaviour.Enabled / world rotation・lossyScale)を追加
    // v6: 自動生成 component binding 用の typed property access(get/set + string)を追加
    // v7: gameplay API(Time拡張/TimeScale, AssetRef解決, GameObject生成, Prefab/Scene, Input拡張, Audio/Animation/Application)を追加
    // v8: 診断 API(reportScriptException) と script descriptor の defaultExecutionOrder を追加
    // v9: GetComponent<Script> 用に entity の script instance を scriptTypeID で引く getScriptInstance を追加
    // v10: Scene 単一load用の loadSceneSingle を追加
    // v11: EntityRef を runtime entity へ解決する resolveEntityRef を追加
    // v12: ライン描画の lineSetPoints と即時描画の lineDrawImmediate lineDrawSphereImmediate を追加
    // v13: LineRendererComponent へ1点追加する lineAddPoint を追加
    // v14: Tag公開(copyTag/setTag)とLayerマスク公開(visibility/collision typeMask)とGameObject検索(byName/byTag/byComponent)を追加
    // v15: 即時形状描画の汎用 lineDrawShape を追加
    // v16: Transform 親追従の継承フラグ(ignoreParentRotation/ignoreParentScale)を追加
    // v17: 入力タイプとマウス範囲制御の get/set を追加
    // v18: MeshRenderer のマテリアル color 上書き setMeshMaterialColor を追加
    // v19: Mesh/Sprite/Text のマテリアル color の get/set(setRendererMaterialColor/getRendererMaterialColor)を追加
    // v20: GameObjectの保存identityを逆引きする getEntityReferenceIdentity を追加
    // v21: レイキャスト(physicsRaycast/physicsRaycastAll)とカメラレイ(screenPointToRay/getMousePositionInView)とCollisionタイプ名解決を追加
    // v22: AddComponent<Script> 用に entity へ script を runtime attach する attachScript を追加
    // v23: イージング関数 easedValue を追加、EasingType と t からイージング済みの値を返す
    // v26: UIが入力を消費したフレームのゲーム入力ブロック状態を追加
    // v27: UISelectableの決定入力配列取得と設定を追加
    // v28: UI入力配列をCanvasの上下左右と決定へ移行
    // v29: Application.Quitの終了要求を追加
    // v30: ワールド座標のGameView変換とCanvasローカル座標変換を追加
    // v32: AudioSourceのPlayOneShotとUnPauseを追加
    // v34: アセット参照を128bit AssetGUIDへ移行
    // v35: UserSettingsルート取得APIを追加
    // v36: Collision実行時状態をAuthoring設定から分離
    // v37: 型安全なDynamicBufferアクセスを追加
    // v43: 全Renderer共通の型付きMaterial Instance APIを追加
    // v44: 廃止した描画、画面遷移APIを削除
    // v45: RenderFeatureProfileの実行時パラメータAPIを追加
    // v46: ParticleSystemのUnity準拠再生操作と実行状態APIを追加
    // v47: RenderFeatureグループの有効状態APIを追加
    // v48: RenderFeaturePassをProfile世代付きUUIDハンドルへ変更
    // v49: Canvas遷移テーブルの取得と変更APIを追加
    // v50: RenderFeatureのSceneColor出力切り替えAPIを追加
    // v51: 入力タイプを実操作の取得専用に変更しsetInputTypeを削除
    // v52: アクティブSceneの再読み込みAPIを追加
    // v53: スクリプトの詳細計測区間を追加
    // v54: シーンを越えてルートGameObjectを保持するAPIを追加
    internal const uint Version = 56;

    // ネイティブが提供する機能カテゴリ
    internal const ulong CapabilityCore = 1ul << 0;
    internal const ulong CapabilityInput = 1ul << 1;
    internal const ulong CapabilityEntity = 1ul << 2;
    internal const ulong CapabilityHierarchy = 1ul << 3;
    internal const ulong CapabilityTransform = 1ul << 4;
    internal const ulong CapabilityObjectModel = 1ul << 5;
    internal const ulong CapabilityComponentBindings = 1ul << 6;
    internal const ulong CapabilityGameplay = 1ul << 7;

    // ScriptCoreが動作に必要とするcapability
    internal const ulong RequiredCapabilities =
        CapabilityCore | CapabilityInput | CapabilityEntity | CapabilityHierarchy | CapabilityTransform
        | CapabilityObjectModel | CapabilityComponentBindings | CapabilityGameplay;
}

// C++側 ManagedRendererMaterialTarget と一致させる
internal enum RendererMaterialTarget {

    Mesh = 0,
    Sprite,
    Text,
    Primitive,
    Line,
}

// C++側 ManagedMaterialParameterValueType と一致させる
internal enum NativeMaterialParameterValueType {

    Float = 0,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Texture,
    Int,
    UInt,
    Bool,
}

// C++側 ManagedMaterialParameterValue と同一レイアウト
[StructLayout(LayoutKind.Explicit, Size = 24)]
public struct NativeMaterialParameterValue {

    [FieldOffset(0)] internal ulong data0;
    [FieldOffset(8)] internal ulong data1;
    [FieldOffset(0)] internal float x;
    [FieldOffset(4)] internal float y;
    [FieldOffset(8)] internal float z;
    [FieldOffset(12)] internal float w;
    [FieldOffset(0)] internal int intValue;
    [FieldOffset(0)] internal uint uintValue;
    [FieldOffset(0)] internal AssetGUID assetID;
    [FieldOffset(16)] internal NativeMaterialParameterValueType type;
    [FieldOffset(20)] internal int reserved;
}

// C++側 ManagedAbiHeader と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct ManagedAbiHeader {

    public uint abiVersion;
    public uint structSize;
    public ulong capabilities;
    public ulong bindingFingerprint;
}

// C++側 ManagedScriptInstanceHandle と同一レイアウト。単純なint indexを境界で公開しない
[StructLayout(LayoutKind.Sequential)]
public readonly struct NativeScriptInstanceHandle {

    public readonly uint index;
    public readonly uint generation;

    public NativeScriptInstanceHandle(uint index, uint generation) {
        this.index = index;
        this.generation = generation;
    }

    // default(NativeScriptInstanceHandle) = {0,0} を valid と誤認しないため generation!=0 も要求する
    public bool IsValid => index != 0xffffffffu && generation != 0;
    public static NativeScriptInstanceHandle Null => new(0xffffffffu, 0);
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector3 {

    public float x;
    public float y;
    public float z;

    public static NativeVector3 From(Vector3 value) {
        return new NativeVector3 {
            x = value.x,
            y = value.y,
            z = value.z
        };
    }

    public Vector3 ToVector3() {
        return new Vector3(x, y, z);
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector2 {

    public float x;
    public float y;

    public static NativeVector2 From(Vector2 value) {
        return new NativeVector2 {
            x = value.x,
            y = value.y
        };
    }

    public Vector2 ToVector2() {
        return new Vector2(x, y);
    }
}

// C++側 ManagedRaycastHit と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeRaycastHit {

    public NativeEntity entity;
    public NativeVector3 point;
    public NativeVector3 normal;
    public float distance;
    public int shapeIndex;
    public int trigger;
}

// C++側 ManagedSkinnedAnimationRuntimeState と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeSkinnedAnimationRuntimeState {

    public float currentTime;
    public float currentDuration;
    public float blendTime;
    public int repeatCount;
    public int initialized;
    public int finished;
    public int inTransition;
}

// C++側 ManagedUISelectableRuntimeState と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeUISelectableRuntimeState {

    public int state;
    public int normalThisFrame;
    public int selectedThisFrame;
    public int submittedThisFrame;
    public int disabledThisFrame;
}

// C++側 ManagedUIProgressRuntimeState と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeUIProgressRuntimeState {

    public float displayedValue;
    public float delayedValue;
    public int initialized;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeColor4 {

    public float r;
    public float g;
    public float b;
    public float a;

    public static NativeColor4 From(Color4 value) {
        return new NativeColor4 {
            r = value.r,
            g = value.g,
            b = value.b,
            a = value.a
        };
    }

    public Color4 ToColor4() {
        return new Color4(r, g, b, a);
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeQuaternion {

    public float x;
    public float y;
    public float z;
    public float w;

    public static NativeQuaternion From(Quaternion value) {
        return new NativeQuaternion {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w
        };
    }

    public Quaternion ToQuaternion() {
        return new Quaternion(x, y, z, w);
    }
}

// 即時形状描画の種類、値は C++ ManagedLineShapeKind と一致させる
public enum LineShapeType {

    Circle2D = 0,
    Rect2D,
    Hemisphere,
    AABB,
    OBB,
    Cone,
    Arrow,
    Axis,
}

// C++側 ManagedLineShape と同一レイアウト、materialID を先頭に置き8バイト境界を揃える
[StructLayout(LayoutKind.Sequential)]
public struct NativeLineShape {

    public AssetGUID materialID;
    public int shapeType;
    public int division;
    public int is2D;
    public float radius;
    public float radius2;
    public float height;
    public float thickness;
    public NativeVector3 a;
    public NativeVector3 b;
    public NativeQuaternion rotation;
    public NativeColor4 color;
}
