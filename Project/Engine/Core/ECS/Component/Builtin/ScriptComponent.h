#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/ECS/Behavior/BehaviorHandle.h>
#include <Engine/Core/Asset/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	ScriptComponent struct
	//============================================================================

	// スクリプトの情報を保持するエントリ
	struct ScriptEntry {

		// スクリプトの型名
		std::string type;
		// 参照しているC#スクリプトアセット
		AssetID scriptAsset{};
		// 有効フラグ
		bool enabled = true;
		// インスペクターから編集するシリアライズフィールド
		nlohmann::json serializedFields = nlohmann::json::object();

		// ランタイム
		BehaviorHandle handle = BehaviorHandle::Null();
	};

	// スクリプトコンポーネント
	struct ScriptComponent {

		std::vector<ScriptEntry> scripts;
	};

	// json変換
	void from_json(const nlohmann::json& in, ScriptEntry& entry);
	void to_json(nlohmann::json& out, const ScriptEntry& entry);
	void from_json(const nlohmann::json& in, ScriptComponent& component);
	void to_json(nlohmann::json& out, const ScriptComponent& component);

	ENGINE_REGISTER_COMPONENT(ScriptComponent, "Script");
} // Engine
