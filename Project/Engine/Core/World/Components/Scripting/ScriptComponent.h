#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Behavior/BehaviorHandle.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	ScriptComponent struct
	//============================================================================
	// スクリプトの情報を保持するエントリ
	struct ScriptEntry {

		// 永続保存の主キー= Stable Script Type GUIDで正規化済み文字列
		// ファイル名/クラス名/namespace/列挙順/runtime indexに依存しない
		std::string scriptTypeId;
		// 同一entityに同typeを複数attachしても識別できる安定slot ID
		UUID scriptSlotID{};
		// 参照しているC#スクリプトアセット
		AssetID scriptAsset{};
		// 直近に解決できた完全修飾型名で表示とlegacy移行用、永続主キーではない
		std::string lastKnownTypeName;
		// 有効フラグ
		bool enabled = true;
		// インスペクターから編集するシリアライズフィールド
		nlohmann::json serializedFields = nlohmann::json::object();

		// ランタイムキャッシュでJSON非シリアライズ
		BehaviorHandle handle = BehaviorHandle::Null();
		// Stable GUIDから解決したcompact runtime type IDでreloadごとに変わる
		uint32_t resolvedRuntimeTypeID = 0;
		// runtime type IDが有効か
		bool resolvedRuntimeTypeValid = false;
		// serializedFieldsの編集リビジョンでruntime専用
		// この値が進んだときだけ生成済みインスタンスへ再適用する
		// authoring変更でのbumpは05_inspector_serializationで接続する拡張点
		uint32_t serializedRevision = 0;
	};

	// スクリプトコンポーネント
	struct ScriptComponent {

		std::vector<ScriptEntry> scripts;
	};

	// 新規ScriptEntryを生成する、scriptSlotIDを新規採番しランタイムキャッシュを初期化する
	// scriptTypeIdが未確定の経路では空文字を渡す
	inline ScriptEntry MakeScriptEntry(const std::string& scriptTypeId,
		const std::string& lastKnownTypeName, AssetID scriptAsset = {}) {

		ScriptEntry entry{};
		entry.scriptTypeId = scriptTypeId;
		entry.lastKnownTypeName = lastKnownTypeName;
		entry.scriptSlotID = UUID::New();
		entry.scriptAsset = scriptAsset;
		entry.enabled = true;
		entry.serializedFields = nlohmann::json::object();
		entry.handle = BehaviorHandle::Null();
		return entry;
	}

	// json変換
	void from_json(const nlohmann::json& in, ScriptEntry& entry);
	void to_json(nlohmann::json& out, const ScriptEntry& entry);
	void from_json(const nlohmann::json& in, ScriptComponent& component);
	void to_json(nlohmann::json& out, const ScriptComponent& component);

	ENGINE_REGISTER_COMPONENT(ScriptComponent, "Script");
} // Engine
