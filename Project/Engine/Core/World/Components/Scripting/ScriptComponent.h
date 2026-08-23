#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Components/Core/DynamicBuffer.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <span>
#include <string_view>

namespace Engine {

	//============================================================================
	//	ScriptComponent struct
	//============================================================================
	// スクリプトの情報を保持するエントリ
	struct ScriptEntry {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		// JSONと文字列を含むためエントリ本体はチャンク外へ置く
		static constexpr uint32_t kInternalBufferCapacity = 0;
		static constexpr bool kSerializable = false;

		// 永続保存の主キー
		std::string scriptTypeID;
		// 同一entityに同typeを複数attachしても識別できる安定slot ID
		UUID scriptSlotID{};
		// 参照しているC#スクリプトアセット
		AssetID scriptAsset{};
		// 直近に解決できた完全修飾型名、表示とMissing Script診断に使う
		std::string lastKnownTypeName;
		// 有効フラグ
		bool enabled = true;
		// インスペクターから編集するシリアライズフィールド
		nlohmann::json serializedFields = nlohmann::json::object();
	};

	// スクリプトコンポーネント
	struct ScriptComponent {

		static constexpr std::string_view kTypeName = "Script";
		static constexpr bool kHasECSHooks = true;

		static void OnAdded(
			ECSWorld& world, const Entity& entity, ScriptComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, ScriptComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, ScriptComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, ScriptComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const ScriptComponent& component, nlohmann::json& out);
	};

	// 新規ScriptEntryを生成する、scriptSlotIDを新規採番しランタイムキャッシュを初期化する
	// scriptTypeIDが未確定の経路では空文字を渡す
	inline ScriptEntry MakeScriptEntry(const std::string& scriptTypeID,
		const std::string& lastKnownTypeName, AssetID scriptAsset = {}) {

		ScriptEntry entry{};
		entry.scriptTypeID = scriptTypeID;
		entry.lastKnownTypeName = lastKnownTypeName;
		entry.scriptSlotID = UUID::New();
		entry.scriptAsset = scriptAsset;
		entry.enabled = true;
		entry.serializedFields = nlohmann::json::object();
		return entry;
	}

	// json変換
	void from_json(const nlohmann::json& in, ScriptEntry& entry);
	void to_json(nlohmann::json& out, const ScriptEntry& entry);
	void from_json(const nlohmann::json& in, ScriptComponent& component);
	void to_json(nlohmann::json& out, const ScriptComponent& component);
	std::span<ScriptEntry> GetScriptEntries(
		ECSWorld& world, const Entity& entity);
	std::span<const ScriptEntry> GetScriptEntries(
		const ECSWorld& world, const Entity& entity);
	void SetScriptEntries(ECSWorld& world, const Entity& entity,
		std::span<const ScriptEntry> entries);
	void SerializeScriptEntries(
		std::span<const ScriptEntry> entries, nlohmann::json& out);

} // Engine
