#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Engine {

	//============================================================================
	//	ManagedScript ABI constants
	//============================================================================
	// C++ / C#境界のABIバージョン、構造体レイアウトや関数テーブルを変えたら必ず上げる
	// v2: managed script instance handleをint32からindexとgenerationを持つhandleへ変更
	// v3:型登録をCopyScriptTypeNameからStable GUIDを渡すCopyScriptTypeInfoへ変更しGenerateScriptManifestを追加
	// v4:固定長フィールドABIを撤廃し二段階blob schemaとruntime state APIへ移行
	// v5: object modelとしてgeneric component accessやEntity.DestroyやScriptBehaviour.Enabledやworld rotation lossyScaleを追加
	// v6:自動生成component binding用の汎用typed property accessとManagedColor3 4を追加
	// v7: gameplay APIとしてTime拡張TimeScale frame tickやEntity生成やPrefab SceneやAssetRef解決やInput拡張やAudio Animation Applicationを追加
	// v8:診断APIのreportScriptExceptionとscript descriptorのdefaultExecutionOrderを追加
	// v9: GetComponent<Script>用にentityのscript instanceをscriptTypeIDで引くgetScriptInstanceを追加
	// v10: Scene単一load用のloadSceneSingleを追加
	// v11: EntityRef解決用のresolveEntityRefを追加
	// v12: ライン描画のlineSetPointsと即時描画のlineDrawImmediate lineDrawSphereImmediateを追加
	// v13: LineRendererComponentへ1点追加するlineAddPointを追加
	// v14: Tag公開とLayerマスク公開visibility typeMaskとEntity検索byName byTag byComponentを追加
	// v15: 即時形状描画の汎用lineDrawShapeを追加、円や箱や錐などをC++側生成で発行する
	// v16: Transform親追従の継承フラグ公開でignoreParentRotation ignoreParentScaleを追加
	// v17: 入力タイプとマウス範囲制御のget/setを追加
	// v18: MeshRendererのマテリアルcolor上書きsetMeshMaterialColorを追加
	// v19: MeshRendererのマテリアルcolor取得getMeshMaterialColorを追加
	// v20: Entityの保存identityを逆引きするgetEntityReferenceIdentityを追加
	// v21: レイキャストのphysicsRaycast physicsRaycastAllとカメラレイのscreenPointToRay getMousePositionInViewとgetCollisionTypeMaskByNameを追加
	// v22: AddComponent<Script>用にowner EntityへscriptTypeIDのscriptをruntime attachするattachScriptを追加
	// v23: イージング関数のeasedValueを追加、EasingTypeとtからイージング済みの値を返す
	// v26: UIが入力を消費したフレームのゲーム入力ブロック状態を追加
	// v27: UISelectableの決定入力配列取得と設定を追加
	// v28: UI入力配列をCanvasの上下左右と決定へ移行
	// v29: Application.Quitの終了要求を追加
	// v30: ワールド座標のGameView変換とCanvasローカル座標変換を追加
	// v32: AudioSourceのPlayOneShotとUnPauseを追加
	// v34: アセット参照を128bit AssetGUIDへ移行
	// v35: UserSettingsルート取得APIを追加
	// v36: Collision実行時状態をAuthoring設定から分離
	// v44: 廃止した描画、画面遷移APIを削除
	// v45: RenderFeatureProfileの実行時パラメータAPIを追加
	// v46: ParticleSystemの再生操作と実行状態APIを追加し旧エフェクトAPIを削除
	inline constexpr uint32_t kManagedAbiVersion = 46;

	// ネイティブが提供する機能カテゴリでcapability bitで有無を表す
	enum class ManagedCapability : uint64_t {

		Core = 1ull << 0,      // Time / Log
		Input = 1ull << 1,     // 入力
		Entity = 1ull << 2,    // Entity 名前 / アクティブ
		Hierarchy = 1ull << 3, // 親子関係
		Transform = 1ull << 4, // Transform
		ObjectModel = 1ull << 5, // generic component access / Entity.Destroy / ScriptBehaviour.Enabled
		ComponentBindings = 1ull << 6, // 自動生成 component wrapper 用の typed property access
		Gameplay = 1ull << 7,  // Time拡張/TimeScale/frame tick, Entity生成, Prefab/Scene, AssetRef解決, Audio/Animation/Application
	};

	// 現状ネイティブが提供する全capability
	inline constexpr uint64_t kManagedCapabilitiesAll =
		static_cast<uint64_t>(ManagedCapability::Core) |
		static_cast<uint64_t>(ManagedCapability::Input) |
		static_cast<uint64_t>(ManagedCapability::Entity) |
		static_cast<uint64_t>(ManagedCapability::Hierarchy) |
		static_cast<uint64_t>(ManagedCapability::Transform) |
		static_cast<uint64_t>(ManagedCapability::ObjectModel) |
		static_cast<uint64_t>(ManagedCapability::ComponentBindings) |
		static_cast<uint64_t>(ManagedCapability::Gameplay);

	// C++ / C#で共有する境界処理の結果コードで、値はC#側と一致させる
	enum class ManagedStatus : int32_t {

		Ok = 0,
		InvalidArgument,
		InvalidWorldHandle,
		InvalidEntityHandle,
		InvalidInstanceHandle,
		AbiMismatch,
		Unsupported,
		SerializationError,
		ScriptException,
		InternalError,
		// 二段階blob APIで呼び出し側bufferが不足した場合、必要sizeを取得し直して再試行する
		BufferTooSmall,
	};

	//============================================================================
	//	ManagedScript structures
	//============================================================================
	// C#のAssetGUIDと同一レイアウト
	struct ManagedAssetGUID {

		uint64_t high = 0;
		uint64_t low = 0;
	};

	// C#側のシリアライズフィールドの種類でschema JSONのkind文字列と対応する
	// 値はC#列挙とは独立で、C++側schema parse時に文字列から決める
	enum class ManagedSerializedFieldKind : int32_t {

		None = 0,
		Bool,
		Byte,
		SByte,
		Short,
		UShort,
		Int,
		UInt,
		Long,
		ULong,
		Float,
		Double,
		String,
		Enum,
		Vector2,
		Vector3,
		Vector4,
		Quaternion,
		Color3,
		Color4,
		Nullable,
		Array,
		List,
		AssetRef,
		EntityRef,
		ScriptRef,
		ComponentRef,
		Object,
		ManagedReference,
		Unsupported,
	};

	// 1フィールドのschemaでcollectionやnullableはelementを持つ再帰構造、buildやreload時にschema JSONを一度だけparseして構築しInspectorが参照する
	struct ManagedFieldSchema {

		std::string fieldID;             // Stable Serialized Field GUID で保存の主キー
		std::string name;                // 現在の field 名
		std::string declaringType;       // 宣言型で継承時の識別に使う

		ManagedSerializedFieldKind kind = ManagedSerializedFieldKind::None;
		std::shared_ptr<ManagedFieldSchema> element; // Array/List/Nullable の要素

		// enum
		std::string enumUnderlying;
		std::vector<std::string> enumNames;
		std::vector<std::string> enumValues; // long/ulong 精度を保つため文字列で保持

		// reference filter
		std::string assetType;   // AssetRef<T> の native AssetType 名
		std::string scriptType;  // ScriptRef<T> の対象 script 完全名
		std::string componentType; // ComponentRef<T> の対象 component 登録名

		// ManagedReference の候補型
		struct ReferenceCandidate {

			std::string type; // 候補型の完全名
			std::vector<std::shared_ptr<ManagedFieldSchema>> members;
		};

		// Object/ManagedReference の型完全名
		std::string objectType;
		// Object のメンバschema
		std::vector<std::shared_ptr<ManagedFieldSchema>> members;
		// ManagedReference の候補型一覧
		std::vector<ReferenceCandidate> candidates;

		// Inspector属性
		bool isPublic = false;
		bool isReadOnly = false;
		bool isHidden = false;
		bool multiline = false;
		bool hasRange = false;
		float rangeMin = 0.0f;
		float rangeMax = 0.0f;
		bool hasMin = false;
		float minValue = 0.0f;
		bool hasDragSpeed = false;
		float dragSpeed = 0.0f;
		std::string tooltip;
		std::string header;  // [SeparatorText] の区切り見出し
		std::string label;   // [Label] の表示ラベル上書き、空なら field 名を使う

		// C#インスタンス生成直後の既定値JSONでauthoring未設定時の初期値
		std::string defaultValueJson;
	};

	// 1 script型のserialized field schema
	struct ManagedScriptSchema {

		std::string scriptTypeID;
		std::string fullTypeName;
		int32_t schemaVersion = 0;
		std::vector<ManagedFieldSchema> fields;
	};

	// C#へ生のECSWorld*を渡さないための世代付きworldハンドル、実体ポインタはネイティブのManagedWorldRegistry内部だけが保持する
	struct ManagedWorldHandle {

		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;
	};

	// managed script instanceを指す世代付きハンドルで、単純なint indexを境界で公開しない
	struct ManagedScriptInstanceHandle {

		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;

		// generationが0は無効で、defaultやゼロ初期化のhandleをvalidと誤認しない
		constexpr bool IsValid() const noexcept { return index != 0xFFFFFFFFu && generation != 0; }
		static constexpr ManagedScriptInstanceHandle Null() noexcept { return {}; }
	};

	// C#へ渡すエンティティ参照
	struct ManagedNativeEntity {

		ManagedWorldHandle world{};
		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;
	};

	// C#と共有するVector3
	struct ManagedVector3 {

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	// C#と共有するVector2
	struct ManagedVector2 {

		float x = 0.0f;
		float y = 0.0f;
	};

	// C#へ渡す衝突情報
	struct ManagedCollisionEvent {

		// コールバックを受け取るEntityと相手Entity
		ManagedNativeEntity self{};
		ManagedNativeEntity other{};

		// 接触情報
		ManagedVector3 normal{};
		ManagedVector3 point{};
		float penetration = 0.0f;

		// 衝突した形状インデックス
		int32_t selfShapeIndex = 0;
		int32_t otherShapeIndex = 0;

		// Trigger接触か
		int32_t isTrigger = 0;
	};

	// C#へ渡すレイキャストのヒット情報
	struct ManagedRaycastHit {

		// ヒットしたEntity
		ManagedNativeEntity entity{};

		// ワールド空間のヒット点と法線
		ManagedVector3 point{};
		ManagedVector3 normal{};
		// originからの距離
		float distance = 0.0f;

		// CollisionComponent内の形状index
		int32_t shapeIndex = -1;
		// Trigger形状へのヒットか
		int32_t trigger = 0;
	};

	// C#と共有するQuaternion
	struct ManagedQuaternion {

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	// C#と共有するColor3とColor4でEngine::Color3 Color4と同一レイアウト
	struct ManagedColor3 {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
	};
	struct ManagedColor4 {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;
	};

	// C#のLinePointと同一レイアウト。indexはコンポーネント点列内の位置で、Engine::LinePointには持たせず変換時に無視する
	struct ManagedLinePoint {

		ManagedVector3 position{};
		ManagedColor4 color{};
		float thickness = 1.0f;
		// 点列内での位置。AddPointで採番されUpdatePointの対象指定に使う。-1は未追加
		int32_t index = -1;
	};

	// C#から要求するDynamicBuffer変更操作
	enum class ManagedDynamicBufferOperation :
		int32_t {

		Replace,
		Append,
		SetElement,
		RemoveAt,
		Resize,
		Clear,
	};

	// C#へ返すスキンアニメーションの固定長Runtime状態
	struct ManagedSkinnedAnimationRuntimeState {

		float currentTime = 0.0f;
		float currentDuration = 0.0f;
		float blendTime = 0.0f;
		int32_t repeatCount = 0;
		int32_t initialized = 0;
		int32_t finished = 0;
		int32_t inTransition = 0;
	};

	// C#へ返すUI選択のフレーム状態
	struct ManagedUISelectableRuntimeState {

		int32_t state = 0;
		int32_t normalThisFrame = 0;
		int32_t selectedThisFrame = 0;
		int32_t submittedThisFrame = 0;
		int32_t disabledThisFrame = 0;
	};

	// C#へ返すUIProgressの表示状態
	struct ManagedUIProgressRuntimeState {

		float displayedValue = 0.0f;
		float delayedValue = 0.0f;
		int32_t initialized = 0;
	};

	// 即時形状描画の種類、値はC#のLineShapeTypeと一致させる
	enum class ManagedLineShapeKind : int32_t {

		Circle2D = 0,
		Rect2D,
		Hemisphere,
		AABB,
		OBB,
		Cone,
		Arrow,
		Axis,
	};

	// C#と共有する即時形状の記述子、全形状を1つの構造で表す
	// materialIDを先頭に置き8バイト境界を揃える、以降は4バイト要素で詰める
	struct ManagedLineShape {

		ManagedAssetGUID materialID{};
		int32_t shapeType = 0;
		int32_t division = 8;
		int32_t is2D = 0;
		float radius = 1.0f;
		float radius2 = 0.0f;
		float height = 1.0f;
		float thickness = 1.0f;
		ManagedVector3 a{};
		ManagedVector3 b{};
		ManagedQuaternion rotation{};
		ManagedColor4 color{};
	};

	// RendererごとのMaterial Instance参照先
	enum class ManagedRendererMaterialTarget : int32_t {

		Mesh = 0,
		Sprite,
		Text,
		Primitive,
		Line,
	};

	// MaterialParameterValueのvariant indexと独立したABI用の型
	enum class ManagedMaterialParameterValueType : int32_t {

		Float = 0,
		Vector2,
		Vector3,
		Vector4,
		Color,
		Texture,
		Int,
		UInt,
		Bool,
	};

	// C#との境界では最大16byteの値と型だけを固定レイアウトで渡す
	struct ManagedMaterialParameterValue {

		uint64_t data0 = 0;
		uint64_t data1 = 0;
		int32_t type = 0;
		int32_t reserved = 0;
	};

	// ネイティブAPIテーブル先頭に置くABIヘッダでversionとsizeとcapabilityを検証に使う
	struct ManagedAbiHeader {

		uint32_t abiVersion = 0;
		uint32_t structSize = 0;
		uint64_t capabilities = 0;
	};

	// C#へ渡すネイティブAPI
	struct ManagedNativeApiTable {

		// 互換性検証用ヘッダで必ず先頭に置く
		ManagedAbiHeader header{};

		using GetDeltaTimeCallback = float(__cdecl*)();
		using GetVector2Callback = ManagedVector2(__cdecl*)();
		using GetVector3Callback = ManagedVector3(__cdecl*)(ManagedNativeEntity);
		using SetVector3Callback = void(__cdecl*)(ManagedNativeEntity, ManagedVector3);
		using GetQuaternionCallback = ManagedQuaternion(__cdecl*)(ManagedNativeEntity);
		using SetQuaternionCallback = void(__cdecl*)(ManagedNativeEntity, ManagedQuaternion);
		using LogCallback = void(__cdecl*)(int32_t, const char*);
		using GetInputButtonCallback = int32_t(__cdecl*)(int32_t);
		using GetNativeBoolCallback = int32_t(__cdecl*)();
		using SetNativeIntCallback = void(__cdecl*)(int32_t);
		// RendererのMaterial Instanceへ型付きパラメータを読み書きする
		using SetRendererMaterialParameterCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, uint64_t, const char*,
			const ManagedMaterialParameterValue*);
		using GetRendererMaterialParameterCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, uint64_t,
			ManagedMaterialParameterValue*);
		using ClearRendererMaterialParameterCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, uint64_t);
		// RenderFeatureProfileへ実行時オーバーライドを設定する
		using SetRenderFeaturePassEnabledCallback = int32_t(__cdecl*)(const char*, int32_t);
		using SetRenderFeaturePassParameterCallback = int32_t(__cdecl*)(
			const char*, uint64_t, const char*, const ManagedMaterialParameterValue*);
		using ClearRenderFeaturePassParameterCallback = int32_t(__cdecl*)(const char*, uint64_t);
		using ResetRenderFeaturePassCallback = int32_t(__cdecl*)(const char*);
		using ResetRenderFeatureOverridesCallback = void(__cdecl*)();
		// CollisionComponentの単一形状をpropIdで読み書きする
		using CollisionGetShapeCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, void*, int32_t);
		using CollisionSetShapeCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, const void*, int32_t);
		// 指定クリップ名のアニメーション合計長を返す
		using GetSkinnedAnimationDurationCallback = float(__cdecl*)(ManagedNativeEntity, const char*);
		// 指定クリップを頭から再生する、終了フラグを同フレームで下ろす
		using PlaySkinnedAnimationCallback = void(__cdecl*)(ManagedNativeEntity, const char*);
		using GetSkinnedAnimationRuntimeStateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, ManagedSkinnedAnimationRuntimeState*);
		using GetUISelectableRuntimeStateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, ManagedUISelectableRuntimeState*);
		using GetUIProgressRuntimeStateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, ManagedUIProgressRuntimeState*);
		using GetUIButtonClickedCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t);
		using IsAliveCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using GetBoolCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using SetBoolCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
		using CopyStringCallback = int32_t(__cdecl*)(ManagedNativeEntity, char*, int32_t);
		using SetStringCallback = void(__cdecl*)(ManagedNativeEntity, const char*);
		using GetEntityCallback = ManagedNativeEntity(__cdecl*)(ManagedNativeEntity);
		using SetParentCallback = void(__cdecl*)(ManagedNativeEntity, ManagedNativeEntity);
		// ObjectModel: generic component access / Entity.Destroy / ScriptBehaviour.Enabled / world rotation・lossyScale
		using HasComponentCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t);
		using ComponentMutateCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
		using DynamicBufferLengthCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t);
		using DynamicBufferCopyCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, int32_t, void*, int32_t);
		using DynamicBufferMutateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, int32_t,
			int32_t, const void*, int32_t);
		using DestroyEntityCallback = void(__cdecl*)(ManagedNativeEntity);
		using GetScriptEnabledCallback = int32_t(__cdecl*)(ManagedNativeEntity, uint64_t);
		using SetScriptEnabledCallback = void(__cdecl*)(ManagedNativeEntity, uint64_t, int32_t);
		// ComponentBindingsは自動生成wrapperのtyped property access、PODはvalueとoutValueへbyteコピーしC#のManaged構造体と同一レイアウトでstringは別系統
		using GetComponentPropertyCallback = ManagedStatus(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, void*, int32_t);
		using SetComponentPropertyCallback = ManagedStatus(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, const void*, int32_t);
		using GetComponentStringPropertyCallback = ManagedStatus(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, char*, int32_t, int32_t*);
		using SetComponentStringPropertyCallback = ManagedStatus(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, const char*, int32_t);
		// Gameplay v7のTime拡張とTimeScaleとAsset解決とEntity生成
		using GetDoubleCallback = double(__cdecl*)();
		using GetUInt64Callback = uint64_t(__cdecl*)();
		using SetFloatCallback = void(__cdecl*)(float);
		using AssetExistsCallback = int32_t(__cdecl*)(ManagedAssetGUID);
		using CopyAssetStringCallback = int32_t(__cdecl*)(ManagedAssetGUID, char*, int32_t);
		// Gameplay v7のEntity生成とPrefabとSceneとSetParentのworldPositionStays
		using CreateEntityCallback = ManagedNativeEntity(__cdecl*)(const char*, ManagedNativeEntity);
		using InstantiatePrefabCallback = ManagedNativeEntity(__cdecl*)(ManagedAssetGUID, ManagedVector3, ManagedQuaternion, int32_t, ManagedNativeEntity);
		using LoadSceneCallback = uint64_t(__cdecl*)(ManagedAssetGUID);
		using UnloadSceneCallback = void(__cdecl*)(uint64_t);
		using SetParentKeepWorldCallback = void(__cdecl*)(ManagedNativeEntity, ManagedNativeEntity, int32_t);
		using SceneInstanceAliveCallback = int32_t(__cdecl*)(uint64_t);
		// Gameplay v7のraw Input拡張多gamepadとaxisとtextとfocus
		using GamepadIndexedButtonCallback = int32_t(__cdecl*)(int32_t, int32_t);
		using GamepadAxisCallback = float(__cdecl*)(int32_t, int32_t);
		using GamepadConnectedCallback = int32_t(__cdecl*)(int32_t);
		using CopyTextCallback = int32_t(__cdecl*)(char*, int32_t);
		// Gameplay v7のAudioSource gameplay methodでentityのAudioSourceComponentを操作する
		using EntityActionCallback = void(__cdecl*)(ManagedNativeEntity);
		using AudioPlayOneShotCallback = void(__cdecl*)(ManagedNativeEntity, ManagedAssetGUID, float);
		// Diagnostics v8のscript callback例外の構造化報告でJSON DTOを1件渡す
		using ReportStringCallback = void(__cdecl*)(const char*);
		// v23のイージング関数、EasingTypeとtからイージング済みの値を返す
		using EasedValueCallback = float(__cdecl*)(int32_t, float);
		// GetComponent<Script> v9のentity上でscriptTypeID一致のscript instanceハンドルを引く
		using GetScriptInstanceCallback = ManagedScriptInstanceHandle(__cdecl*)(ManagedNativeEntity, const char*);
		// AddComponent<Script> v22のentityへscriptTypeIDのscriptをruntime attachする、成否を返す
		using AttachScriptCallback = int32_t(__cdecl*)(ManagedNativeEntity, const char*);
		using ResolveEntityRefCallback = ManagedNativeEntity(__cdecl*)(ManagedAssetGUID, uint64_t);
		// v20のEntity保存identity逆引き、sourceAssetとlocalFileIDとkindを返す
		using GetEntityRefIdentityCallback = void(__cdecl*)(ManagedNativeEntity, ManagedAssetGUID*, uint64_t*, int32_t*);
		// v21のレイキャスト、単発は最近ヒットを返しAllはヒット総数を返してcapacity分だけ書く
		using PhysicsRaycastCallback = int32_t(__cdecl*)(ManagedVector3, ManagedVector3, float, uint32_t, uint32_t, ManagedRaycastHit*);
		using PhysicsRaycastAllCallback = int32_t(__cdecl*)(ManagedVector3, ManagedVector3, float, uint32_t, uint32_t, ManagedRaycastHit*, int32_t);
		// v21のカメラレイ、GameViewピクセル座標からレイを作る
		using ScreenPointToRayCallback = int32_t(__cdecl*)(float, float, ManagedVector3*, ManagedVector3*);
		// v30のワールド座標からGameViewピクセル座標への変換
		using WorldToScreenPointCallback = int32_t(__cdecl*)(ManagedVector3, ManagedVector3*);
		// v21のGameView内マウス座標、View外は0を返す
		using GetMousePositionInViewCallback = int32_t(__cdecl*)(ManagedVector2*);
		// v21のCollisionタイプ名からビットマスクを引く、未登録は0
		using GetCollisionTypeMaskByNameCallback = uint32_t(__cdecl*)(const char*);
		// ライン描画v12でcomponentの点列設定と即時描画
		using LineSetPointsCallback = void(__cdecl*)(ManagedNativeEntity, const ManagedLinePoint*, int32_t, int32_t);
		using LineAddPointCallback = int32_t(__cdecl*)(ManagedNativeEntity, ManagedLinePoint);
		using LineUpdatePointCallback = void(__cdecl*)(ManagedNativeEntity, ManagedLinePoint);
		using LineDrawImmediateCallback = void(__cdecl*)(const ManagedLinePoint*, int32_t, int32_t, int32_t, ManagedAssetGUID);
		using LineDrawSphereImmediateCallback = void(__cdecl*)(ManagedVector3, float, ManagedColor4, int32_t, float, ManagedAssetGUID);
		// v14のEntity検索で名前やタグから1件、タグやcomponentから複数件をbufferへ詰める
		using FindByStringCallback = ManagedNativeEntity(__cdecl*)(const char*);
		using FindManyByStringCallback = int32_t(__cdecl*)(const char*, ManagedNativeEntity*, int32_t);
		using FindByComponentCallback = ManagedNativeEntity(__cdecl*)(int32_t);
		using FindManyByComponentCallback = int32_t(__cdecl*)(int32_t, ManagedNativeEntity*, int32_t);
		// v15の即時形状描画、記述子1件を渡してC++側で線分へ展開する
		using LineDrawShapeCallback = void(__cdecl*)(const ManagedLineShape*);
		// ParticleSystemの再生操作と実行状態照会
		using ParticleSystemControlCallback = void(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, int32_t);
		using ParticleSystemStateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t);
		// Canvasの入力配列を操作種別とデバイス別に取得設定する
		using CanvasCopyInputBindingsCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, int32_t*, int32_t);
		using CanvasSetInputBindingsCallback = void(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, const int32_t*, int32_t);
		// Canvasローカル座標への変換
		using CanvasScreenToLocalPointCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, ManagedVector2, ManagedVector2*);
		// Application.Quitの終了要求
		using ApplicationQuitCallback = void(__cdecl*)();

#include <Engine/Core/Scripting/Managed/Generated/ManagedNativeApiFields.generated.inl>
	};

	// C#側から受け取るscript typeのメタdataでStable GUID主キーの固定長ABI
	struct ManagedScriptTypeDescriptor {

		char scriptTypeID[40]{};   // 正規化GUID 36 文字と null
		char fullTypeName[256]{};
		char displayName[128]{};
		char sourcePath[260]{};    // 定義元 .cs パスで drag&drop source 照合用
		int32_t hasExplicitId = 0; // ScriptTypeId 属性が明示されていたか
		int32_t defaultExecutionOrder = 0; // DefaultExecutionOrder 属性の値で未指定は 0
	};

	//============================================================================
	//	ABIレイアウト検証
	//	C#側の[StructLayout(Sequential)]と一致していることを保証する
	//============================================================================
	static_assert(std::is_standard_layout_v<ManagedWorldHandle>);
	static_assert(std::is_standard_layout_v<ManagedScriptInstanceHandle>);
	static_assert(std::is_standard_layout_v<ManagedNativeEntity>);
	static_assert(std::is_standard_layout_v<ManagedAbiHeader>);
	static_assert(std::is_standard_layout_v<ManagedNativeApiTable>);
	static_assert(std::is_standard_layout_v<ManagedMaterialParameterValue>);
	static_assert(std::is_standard_layout_v<ManagedCollisionEvent>);
	static_assert(std::is_standard_layout_v<ManagedScriptTypeDescriptor>);
	static_assert(sizeof(ManagedScriptTypeDescriptor) == 40 + 256 + 128 + 260 + 4 + 4);

	static_assert(sizeof(ManagedWorldHandle) == 8);
	static_assert(sizeof(ManagedScriptInstanceHandle) == 8);
	static_assert(sizeof(ManagedNativeEntity) == 16);
	static_assert(sizeof(ManagedAbiHeader) == 16);
	static_assert(sizeof(ManagedMaterialParameterValue) == 24);
} // Engine
