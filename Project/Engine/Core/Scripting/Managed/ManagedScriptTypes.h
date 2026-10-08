#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace Engine {

	//============================================================================
	//	Managed ABI定数
	//============================================================================
	// NativeとManagedを同じ版で接続する
	inline constexpr uint32_t kManagedABIVersion = 67;

	// Nativeが提供する機能bit
	enum class ManagedCapability : uint64_t {

		Core = 1ull << 0,			   // 時刻とログ
		Input = 1ull << 1,			   // 入力
		Entity = 1ull << 2,			   // 名前と有効状態
		Hierarchy = 1ull << 3,		   // 親子関係
		Transform = 1ull << 4,		   // Transform
		ObjectModel = 1ull << 5,	   // Component操作とEntity破棄とScriptの有効状態
		ComponentBindings = 1ull << 6, // 生成Componentのproperty操作
		Gameplay = 1ull << 7, // ゲーム用の時間と生成と再生操作
	};

	// Nativeが提供する全機能bit
	inline constexpr uint64_t kManagedCapabilitiesAll =
		static_cast<uint64_t>(ManagedCapability::Core) | static_cast<uint64_t>(ManagedCapability::Input) |
		static_cast<uint64_t>(ManagedCapability::Entity) | static_cast<uint64_t>(ManagedCapability::Hierarchy) |
		static_cast<uint64_t>(ManagedCapability::Transform) | static_cast<uint64_t>(ManagedCapability::ObjectModel) |
		static_cast<uint64_t>(ManagedCapability::ComponentBindings) | static_cast<uint64_t>(ManagedCapability::Gameplay);

	// NativeとManagedで共有する結果コード
	enum class ManagedStatus : int32_t {

		Ok = 0,
		InvalidArgument,
		InvalidWorldHandle,
		InvalidEntityHandle,
		InvalidInstanceHandle,
		ABIMismatch,
		Unsupported,
		SerializationError,
		ScriptException,
		InternalError,
		// 必要容量を再取得できるBuffer不足の結果
		BufferTooSmall,
	};

	//============================================================================
	//	Managed ABI値型
	//============================================================================
	// C#のAssetGUIDと同一レイアウト
	struct ManagedAssetGUID {

		uint64_t high = 0;
		uint64_t low = 0;
	};

	// Worldの世代付き参照
	struct ManagedWorldHandle {

		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;

		constexpr bool IsValid() const noexcept { return index != 0xFFFFFFFFu && generation != 0; }
	};

	// Scriptの世代付き参照
	struct ManagedScriptInstanceHandle {

		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;

		// 既定値の世代0を無効として扱う
		constexpr bool IsValid() const noexcept { return index != 0xFFFFFFFFu && generation != 0; }
		static constexpr ManagedScriptInstanceHandle Null() noexcept { return {}; }
	};

	// C#へ渡すエンティティ参照
	struct ManagedNativeEntity {

		ManagedWorldHandle world{};
		uint32_t index = 0xFFFFFFFF;
		uint32_t generation = 0;

		// Entityの初回世代0は有効として扱う
		constexpr bool IsValid() const noexcept { return world.IsValid() && index != 0xFFFFFFFFu; }
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
		// 始点からの距離
		float distance = 0.0f;

		// Collider内の形状番号
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

	// C#と共有するColor3
	struct ManagedColor3 {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
	};
	// C#と共有するColor4
	struct ManagedColor4 {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;
	};

	// C#と共有するLineの点と点列内の番号
	struct ManagedLinePoint {

		ManagedVector3 position{};
		ManagedColor4 color{};
		float thickness = 1.0f;
		// 点列内の番号で未追加は-1
		int32_t index = -1;
	};

	// C#から要求するBufferの変更操作
	enum class ManagedDynamicBufferOperation : int32_t {

		Replace,
		Append,
		SetElement,
		RemoveAt,
		Resize,
		Clear,
	};

	// C#へ返すAnimationの再生状態
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

	// C#と共有する即時描画形状
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

	// Materialの8バイト境界を保つ即時描画形状
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

	// Material値を変更するRenderer種別
	enum class ManagedRendererMaterialTarget : int32_t {

		Mesh = 0,
		Sprite,
		Text,
		Primitive,
		Line,
	};

	// Native内部のvariant番号と独立したMaterial値種別
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

	// 16byteの値と型番号を共有するMaterial値
	struct ManagedMaterialParameterValue {

		uint64_t data0 = 0;
		uint64_t data1 = 0;
		int32_t type = 0;
		int32_t reserved = 0;
	};

	// ABIの版と配置と機能bitを共有する先頭情報
	struct ManagedABIHeader {

		uint32_t abiVersion = 0;
		uint32_t structSize = 0;
		uint64_t capabilities = 0;
		uint64_t bindingFingerprint = 0;
	};

	// C#へ渡すNative接続
	struct ManagedNativeAPITable {

		//--------- structure ----------------------------------------------------

		using BeginScriptSampleCallback = uint64_t(__cdecl*)(ManagedNativeEntity, uint64_t, const char*);
		using EndScriptSampleCallback = void(__cdecl*)(uint64_t);
		using DontDestroyOnLoadCallback = int32_t(__cdecl*)(ManagedNativeEntity);
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
		// RendererのMaterial値を読み書きする
		using SetRendererMaterialParameterCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, uint64_t, const char*, const ManagedMaterialParameterValue*);
		using GetRendererMaterialParameterCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, uint64_t, ManagedMaterialParameterValue*);
		using ClearRendererMaterialParameterCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, uint64_t);
		// 描画Passの実行値を読み書きする
		using ResolveRenderFeaturePassCallback = int32_t(__cdecl*)(const char*, uint64_t*, uint64_t*);
		using ValidateRenderFeaturePassCallback = int32_t(__cdecl*)(uint64_t, uint64_t);
		using SetRenderFeaturePassEnabledCallback = int32_t(__cdecl*)(uint64_t, uint64_t, int32_t);
		using SetRenderFeatureGroupEnabledCallback = int32_t(__cdecl*)(const char*, int32_t);
		using SetRenderFeaturePassParameterCallback = int32_t(__cdecl*)(
			uint64_t, uint64_t, uint64_t, const char*, const ManagedMaterialParameterValue*);
		using GetRenderFeaturePassParameterCallback = int32_t(__cdecl*)(
			uint64_t, uint64_t, uint64_t, ManagedMaterialParameterValue*);
		using ClearRenderFeaturePassParameterCallback = int32_t(__cdecl*)(uint64_t, uint64_t, uint64_t);
		using ResetRenderFeaturePassCallback = int32_t(__cdecl*)(uint64_t, uint64_t);
		using ResetRenderFeatureOverridesCallback = void(__cdecl*)();
		// Colliderの単一形状を読み書きする
		using CollisionGetShapeCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, void*, int32_t);
		using CollisionSetShapeCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, const void*, int32_t);
		// 指定Clipの再生時間を返す
		using GetSkinnedAnimationDurationCallback = float(__cdecl*)(ManagedNativeEntity, const char*);
		using SetAnimatorParameterCallback = int32_t(__cdecl*)(ManagedNativeEntity, const char*, int32_t, float, int32_t);
		using GetAnimatorParameterCallback = int32_t(__cdecl*)(ManagedNativeEntity, const char*, int32_t, float*, int32_t*);
		// 指定Clipの再生を要求する
		using PlaySkinnedAnimationCallback = void(__cdecl*)(ManagedNativeEntity, const char*);
		using GetSkinnedAnimationRuntimeStateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, ManagedSkinnedAnimationRuntimeState*);
		using GetUISelectableRuntimeStateCallback = int32_t(__cdecl*)(ManagedNativeEntity, ManagedUISelectableRuntimeState*);
		using GetUIProgressRuntimeStateCallback = int32_t(__cdecl*)(ManagedNativeEntity, ManagedUIProgressRuntimeState*);
		using GetUIButtonClickedCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t);
		using IsAliveCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using GetBoolCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using SetBoolCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
		using CopyStringCallback = int32_t(__cdecl*)(ManagedNativeEntity, char*, int32_t);
		using SetStringCallback = void(__cdecl*)(ManagedNativeEntity, const char*);
		using GetEntityCallback = ManagedNativeEntity(__cdecl*)(ManagedNativeEntity);
		using SetParentCallback = void(__cdecl*)(ManagedNativeEntity, ManagedNativeEntity);
		// ComponentとScriptとEntityを操作する
		using HasComponentCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t);
		using GetComponentInstanceIDCallback = uint64_t(__cdecl*)(ManagedNativeEntity, int32_t);
		using ComponentMutateCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
		using DynamicBufferLengthCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t);
		using DynamicBufferCopyCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, int32_t, void*, int32_t);
		using DynamicBufferMutateCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, int32_t, int32_t, const void*, int32_t);
		using DestroyEntityCallback = void(__cdecl*)(ManagedNativeEntity);
		using GetScriptEnabledCallback = int32_t(__cdecl*)(ManagedNativeEntity, uint64_t);
		using SetScriptEnabledCallback = void(__cdecl*)(ManagedNativeEntity, uint64_t, int32_t);
		// 生成Componentの値と文字列を転送する
		using GetComponentPropertyCallback = ManagedStatus(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, void*, int32_t);
		using SetComponentPropertyCallback = ManagedStatus(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, const void*, int32_t);
		using GetComponentStringPropertyCallback = ManagedStatus(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, char*, int32_t, int32_t*);
		using SetComponentStringPropertyCallback = ManagedStatus(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, const char*, int32_t);
		// 時刻とAssetの情報を取得する
		using GetDoubleCallback = double(__cdecl*)();
		using GetUInt64Callback = uint64_t(__cdecl*)();
		using SetFloatCallback = void(__cdecl*)(float);
		using AssetExistsCallback = int32_t(__cdecl*)(ManagedAssetGUID);
		using CopyAssetStringCallback = int32_t(__cdecl*)(ManagedAssetGUID, char*, int32_t);
		// EntityとSceneの生成と親変更を要求する
		using CreateEntityCallback = ManagedNativeEntity(__cdecl*)(const char*, ManagedNativeEntity);
		using InstantiatePrefabCallback = ManagedNativeEntity(__cdecl*)(
			ManagedAssetGUID, ManagedVector3, ManagedQuaternion, int32_t, ManagedNativeEntity);
		using InstantiateEntityCallback = ManagedNativeEntity(__cdecl*)(
			ManagedNativeEntity, ManagedVector3, ManagedQuaternion, int32_t, ManagedNativeEntity);
		using LoadSceneCallback = uint64_t(__cdecl*)(ManagedAssetGUID);
		using UnloadSceneCallback = void(__cdecl*)(uint64_t);
		using SetParentKeepWorldCallback = void(__cdecl*)(ManagedNativeEntity, ManagedNativeEntity, int32_t);
		using SceneInstanceAliveCallback = int32_t(__cdecl*)(uint64_t);
		// 機器入力とPlayer割当を取得する
		using GamepadIndexedButtonCallback = int32_t(__cdecl*)(int32_t, int32_t);
		using GamepadAxisCallback = float(__cdecl*)(int32_t, int32_t);
		using GamepadConnectedCallback = int32_t(__cdecl*)(int32_t);
		using PlayerIndexCallback = int32_t(__cdecl*)(int32_t);
		using InputPlayVibrationCallback = uint32_t(__cdecl*)(int32_t, float, float, float, float, float);
		using InputStopVibrationCallback = void(__cdecl*)(int32_t, uint32_t);
		using CopyTextCallback = int32_t(__cdecl*)(char*, int32_t);
		// Audioの再生を要求する
		using EntityActionCallback = void(__cdecl*)(ManagedNativeEntity);
		using AudioPlayOneShotCallback = void(__cdecl*)(ManagedNativeEntity, ManagedAssetGUID, float);
		// Script例外のJSONを受け取る
		using ReportStringCallback = void(__cdecl*)(const char*);
		// 指定した補間曲線の値を返す
		using EasedValueCallback = float(__cdecl*)(int32_t, float);
		// Entity上のScriptを型IDで検索する
		using GetScriptInstanceCallback = ManagedScriptInstanceHandle(__cdecl*)(ManagedNativeEntity, const char*);
		// 追加したScriptの世代付き参照を返す
		using AttachScriptCallback = ManagedScriptInstanceHandle(__cdecl*)(ManagedNativeEntity, const char*);
		using RemoveScriptCallback = void(__cdecl*)(ManagedNativeEntity, uint64_t);
		using ResolveEntityRefCallback = ManagedNativeEntity(__cdecl*)(ManagedAssetGUID, uint64_t, ManagedNativeEntity);
		// Entityの保存参照IDを取得する
		using GetEntityRefIdentityCallback = void(__cdecl*)(ManagedNativeEntity, ManagedAssetGUID*, uint64_t*, int32_t*);
		// 最近のレイ接触と全接触を取得する
		using PhysicsRaycastCallback = int32_t(__cdecl*)(
			ManagedVector3, ManagedVector3, float, uint32_t, uint32_t, uint32_t, ManagedRaycastHit*);
		using PhysicsRaycastAllCallback = int32_t(__cdecl*)(
			ManagedVector3, ManagedVector3, float, uint32_t, uint32_t, uint32_t, ManagedRaycastHit*, int32_t);
		// 画面座標からワールドレイを作る
		using ScreenPointToRayCallback = int32_t(__cdecl*)(float, float, ManagedVector3*, ManagedVector3*);
		// ワールド座標を画面座標へ変換する
		using WorldToScreenPointCallback = int32_t(__cdecl*)(ManagedVector3, ManagedVector3*);
		// View内のマウス座標を取得する
		using GetMousePositionInViewCallback = int32_t(__cdecl*)(ManagedVector2*);
		// 名前からCollisionのマスクを取得する
		using GetCollisionTypeMaskByNameCallback = uint32_t(__cdecl*)(const char*);
		// Lineの点列更新と即時描画を要求する
		using LineSetPointsCallback = void(__cdecl*)(ManagedNativeEntity, const ManagedLinePoint*, int32_t, int32_t);
		using LineAddPointCallback = int32_t(__cdecl*)(ManagedNativeEntity, ManagedLinePoint);
		using LineUpdatePointCallback = void(__cdecl*)(ManagedNativeEntity, ManagedLinePoint);
		using LineDrawImmediateCallback = void(__cdecl*)(
			const ManagedLinePoint*, int32_t, int32_t, int32_t, ManagedAssetGUID);
		using LineDrawSphereImmediateCallback = void(__cdecl*)(
			ManagedVector3, float, ManagedColor4, int32_t, float, ManagedAssetGUID);
		// 名前とTagとComponentからEntityを検索する
		using FindByStringCallback = ManagedNativeEntity(__cdecl*)(const char*);
		using FindManyByStringCallback = int32_t(__cdecl*)(const char*, ManagedNativeEntity*, int32_t);
		using FindByComponentCallback = ManagedNativeEntity(__cdecl*)(int32_t);
		using FindManyByComponentCallback = int32_t(__cdecl*)(int32_t, ManagedNativeEntity*, int32_t);
		// 指定形状の即時描画を要求する
		using LineDrawShapeCallback = void(__cdecl*)(const ManagedLineShape*);
		// ParticleSystemの再生操作と実行状態照会
		using ParticleSystemControlCallback = void(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, int32_t);
		using ParticleSystemStateCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t);
		// Canvasの機器別入力割当を読み書きする
		using CanvasCopyInputBindingsCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, int32_t*, int32_t);
		using CanvasSetInputBindingsCallback = void(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, const int32_t*, int32_t);
		// Canvasの選択表を読み書きする
		using CanvasGetNavigationTableSizeCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t*, int32_t*);
		using CanvasResizeNavigationTableCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t);
		using CanvasGetNavigationCellCallback = int32_t(__cdecl*)(
			ManagedNativeEntity, int32_t, int32_t, ManagedNativeEntity*);
		using CanvasSetNavigationCellCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t, int32_t, ManagedNativeEntity);
		// Canvasローカル座標への変換
		using CanvasScreenToLocalPointCallback = int32_t(__cdecl*)(ManagedNativeEntity, ManagedVector2, ManagedVector2*);
		// Application.Quitの終了要求
		using ApplicationQuitCallback = void(__cdecl*)();

		//--------- variables ----------------------------------------------------

		// ABI情報は最初のfieldとして保持する
		ManagedABIHeader header{};

#include <Engine/Core/Scripting/Managed/Generated/ManagedNativeAPIFields.generated.inl>
	};

	// C#から受け取るScript型のGUIDと表示情報
	struct ManagedScriptTypeDescriptor {

		char scriptTypeID[40]{}; // GUID36文字と終端
		char fullTypeName[256]{};
		char displayName[128]{};
		char sourcePath[260]{};			   // Scriptの定義元パス
		int32_t hasExplicitID = 0;		   // 型IDの明示状態
		int32_t defaultExecutionOrder = 0; // 既定の実行順
	};

	//============================================================================
	//	ABIレイアウト検証
	//	標準配置と固定サイズを検証する
	//============================================================================
	static_assert(std::is_standard_layout_v<ManagedWorldHandle>);
	static_assert(std::is_standard_layout_v<ManagedScriptInstanceHandle>);
	static_assert(std::is_standard_layout_v<ManagedNativeEntity>);
	static_assert(std::is_standard_layout_v<ManagedABIHeader>);
	static_assert(std::is_standard_layout_v<ManagedNativeAPITable>);
	static_assert(std::is_standard_layout_v<ManagedMaterialParameterValue>);
	static_assert(std::is_standard_layout_v<ManagedCollisionEvent>);
	static_assert(std::is_standard_layout_v<ManagedScriptTypeDescriptor>);
	static_assert(sizeof(ManagedScriptTypeDescriptor) == 40 + 256 + 128 + 260 + 4 + 4);

	static_assert(sizeof(ManagedWorldHandle) == 8);
	static_assert(sizeof(ManagedScriptInstanceHandle) == 8);
	static_assert(sizeof(ManagedNativeEntity) == 16);
	static_assert(sizeof(ManagedABIHeader) == 24);
	static_assert(sizeof(ManagedMaterialParameterValue) == 24);
#include <Engine/Core/Scripting/Managed/Generated/ManagedABILayout.generated.inl>
} // Engine
