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
	inline constexpr uint32_t kManagedAbiVersion = 8;

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
		Unsupported,
	};

	// 1フィールドのschemaでcollectionやnullableはelementを持つ再帰構造、buildやreload時にschema JSONを一度だけparseして構築しInspectorが参照する
	struct ManagedFieldSchema {

		std::string fieldId;             // Stable Serialized Field GUID で保存の主キー
		std::string name;                // 現在の field 名で表示と legacy 照合に使う
		std::string declaringType;       // 宣言型で継承時の識別に使う
		std::vector<std::string> formerNames; // [FormerlySerializedAs] の旧名

		ManagedSerializedFieldKind kind = ManagedSerializedFieldKind::None;
		std::shared_ptr<ManagedFieldSchema> element; // Array/List/Nullable の要素

		// enum
		std::string enumUnderlying;
		std::vector<std::string> enumNames;
		std::vector<std::string> enumValues; // long/ulong 精度を保つため文字列で保持

		// reference filter
		std::string assetType;   // AssetRef<T> の native AssetType 名
		std::string scriptType;  // ScriptRef<T> の対象 script 完全名

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
		std::string header;

		// C#インスタンス生成直後の既定値JSONでauthoring未設定時の初期値
		std::string defaultValueJson;
	};

	// 1 script型のserialized field schema
	struct ManagedScriptSchema {

		std::string scriptTypeId;
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
		using IsAliveCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using GetBoolCallback = int32_t(__cdecl*)(ManagedNativeEntity);
		using SetBoolCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
		using CopyStringCallback = int32_t(__cdecl*)(ManagedNativeEntity, char*, int32_t);
		using SetStringCallback = void(__cdecl*)(ManagedNativeEntity, const char*);
		using GetEntityCallback = ManagedNativeEntity(__cdecl*)(ManagedNativeEntity);
		using SetParentCallback = void(__cdecl*)(ManagedNativeEntity, ManagedNativeEntity);
		// ObjectModel: generic component access / Entity.Destroy / ScriptBehaviour.Enabled / world rotation・lossyScale
		using GetComponentTypeIdCallback = int32_t(__cdecl*)(const char*);
		using HasComponentCallback = int32_t(__cdecl*)(ManagedNativeEntity, int32_t);
		using ComponentMutateCallback = void(__cdecl*)(ManagedNativeEntity, int32_t);
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
		using AssetExistsCallback = int32_t(__cdecl*)(uint64_t);
		using CopyAssetStringCallback = int32_t(__cdecl*)(uint64_t, char*, int32_t);
		// Gameplay v7のEntity生成とPrefabとSceneとSetParentのworldPositionStays
		using CreateEntityCallback = ManagedNativeEntity(__cdecl*)(const char*, ManagedNativeEntity);
		using InstantiatePrefabCallback = ManagedNativeEntity(__cdecl*)(uint64_t, ManagedVector3, ManagedQuaternion, int32_t, ManagedNativeEntity);
		using LoadSceneCallback = uint64_t(__cdecl*)(uint64_t);
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
		// Diagnostics v8のscript callback例外の構造化報告でJSON DTOを1件渡す
		using ReportStringCallback = void(__cdecl*)(const char*);

		GetDeltaTimeCallback getDeltaTime = nullptr;
		GetDeltaTimeCallback getFixedDeltaTime = nullptr;
		LogCallback log = nullptr;
		GetInputButtonCallback getKey = nullptr;
		GetInputButtonCallback getKeyDown = nullptr;
		GetInputButtonCallback getKeyUp = nullptr;
		GetInputButtonCallback getMouseButton = nullptr;
		GetInputButtonCallback getMouseButtonDown = nullptr;
		GetInputButtonCallback getMouseButtonUp = nullptr;
		GetVector2Callback getMousePosition = nullptr;
		GetVector2Callback getMouseDelta = nullptr;
		GetDeltaTimeCallback getMouseWheel = nullptr;
		GetInputButtonCallback getGamepadButton = nullptr;
		GetInputButtonCallback getGamepadButtonDown = nullptr;
		GetNativeBoolCallback isGamepadConnected = nullptr;
		GetVector2Callback getLeftStick = nullptr;
		GetVector2Callback getRightStick = nullptr;
		GetDeltaTimeCallback getLeftTrigger = nullptr;
		GetDeltaTimeCallback getRightTrigger = nullptr;
		IsAliveCallback isAlive = nullptr;
		CopyStringCallback copyName = nullptr;
		SetStringCallback setName = nullptr;
		GetBoolCallback getActiveSelf = nullptr;
		SetBoolCallback setActiveSelf = nullptr;
		GetBoolCallback getActiveInHierarchy = nullptr;
		GetEntityCallback getParent = nullptr;
		GetEntityCallback getFirstChild = nullptr;
		GetEntityCallback getNextSibling = nullptr;
		SetParentCallback setParent = nullptr;
		GetVector3Callback getPosition = nullptr;
		SetVector3Callback setPosition = nullptr;
		GetVector3Callback getLocalPosition = nullptr;
		SetVector3Callback setLocalPosition = nullptr;
		GetVector3Callback getLocalScale = nullptr;
		SetVector3Callback setLocalScale = nullptr;
		GetQuaternionCallback getLocalRotation = nullptr;
		SetQuaternionCallback setLocalRotation = nullptr;
		// world rotation / world(lossy) scale
		GetQuaternionCallback getRotation = nullptr;
		SetQuaternionCallback setRotation = nullptr;
		GetVector3Callback getLossyScale = nullptr;
		// generic component accessでcompact type idベース、型名からidはgetComponentTypeIdで一度だけ解決する
		GetComponentTypeIdCallback getComponentTypeId = nullptr;
		HasComponentCallback hasComponent = nullptr;
		ComponentMutateCallback addComponent = nullptr;
		ComponentMutateCallback removeComponent = nullptr;
		// Entity破棄でWorldCommandBuffer経由の遅延適用
		DestroyEntityCallback destroyEntity = nullptr;
		// ScriptBehaviour.Enabledでowner EntityとscriptSlotIDでruntime entryを特定する
		GetScriptEnabledCallback getScriptEnabled = nullptr;
		SetScriptEnabledCallback setScriptEnabled = nullptr;
		// 自動生成component bindingのtyped property accessでdispatchは生成コードが実装する
		GetComponentPropertyCallback getComponentProperty = nullptr;
		SetComponentPropertyCallback setComponentProperty = nullptr;
		GetComponentStringPropertyCallback getComponentStringProperty = nullptr;
		SetComponentStringPropertyCallback setComponentStringProperty = nullptr;
		// Gameplay v7のTime拡張でscaledとunscaledを分離、getDeltaTimeとgetFixedDeltaTimeはscaled値を返す
		GetDeltaTimeCallback getUnscaledDeltaTime = nullptr;
		GetDeltaTimeCallback getUnscaledFixedDeltaTime = nullptr;
		GetDoubleCallback getTimeSinceStartup = nullptr;
		GetDoubleCallback getUnscaledTime = nullptr;
		GetDeltaTimeCallback getTimeScale = nullptr;
		SetFloatCallback setTimeScale = nullptr;
		GetUInt64Callback getFrameCount = nullptr;
		// Gameplay v7のAssetRef runtime resolveでUUID主体、pointerやpathは返さない
		AssetExistsCallback assetExists = nullptr;
		CopyAssetStringCallback copyAssetDisplayName = nullptr;
		// Gameplay v7のEntity生成とPrefabとSceneとSetParentのworldPositionStays
		CreateEntityCallback createEntity = nullptr;
		InstantiatePrefabCallback instantiatePrefab = nullptr;
		LoadSceneCallback loadSceneAdditive = nullptr;
		UnloadSceneCallback unloadScene = nullptr;
		SceneInstanceAliveCallback isSceneInstanceAlive = nullptr;
		SetParentKeepWorldCallback setParentKeepWorld = nullptr;
		// Gameplay v7のraw Input拡張多gamepadとaxisとtextとfocus
		GamepadIndexedButtonCallback getGamepadButtonIndexed = nullptr;
		GamepadIndexedButtonCallback getGamepadButtonDownIndexed = nullptr;
		GamepadIndexedButtonCallback getGamepadButtonUpIndexed = nullptr;
		GamepadAxisCallback getGamepadAxis = nullptr;
		GamepadConnectedCallback isGamepadConnectedIndexed = nullptr;
		GetNativeBoolCallback getConnectedGamepadCount = nullptr;
		GetNativeBoolCallback getHasFocus = nullptr;
		CopyTextCallback copyTextInput = nullptr;
		// Gameplay v7のproject rootパスでInputActions.json等のProjectSettings解決用
		CopyTextCallback copyProjectRoot = nullptr;
		// Gameplay v7のAudioSource gameplay method
		EntityActionCallback audioPlay = nullptr;
		EntityActionCallback audioPause = nullptr;
		EntityActionCallback audioStop = nullptr;
		GetBoolCallback audioIsPlaying = nullptr;
		// Diagnostics v8のscript callback例外の構造化報告
		ReportStringCallback reportScriptException = nullptr;
	};

	// C#側から受け取るscript typeのメタdataでStable GUID主キーの固定長ABI
	struct ManagedScriptTypeDescriptor {

		char scriptTypeId[40]{};   // 正規化GUID 36 文字と null
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
	static_assert(std::is_standard_layout_v<ManagedCollisionEvent>);
	static_assert(std::is_standard_layout_v<ManagedScriptTypeDescriptor>);
	static_assert(sizeof(ManagedScriptTypeDescriptor) == 40 + 256 + 128 + 260 + 4 + 4);

	static_assert(sizeof(ManagedWorldHandle) == 8);
	static_assert(sizeof(ManagedScriptInstanceHandle) == 8);
	static_assert(sizeof(ManagedNativeEntity) == 16);
	static_assert(sizeof(ManagedAbiHeader) == 16);
} // Engine
