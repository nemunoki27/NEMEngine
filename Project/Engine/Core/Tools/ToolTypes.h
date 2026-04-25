#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <cstdint>
#include <string>
#include <string_view>

namespace Engine {

	// front
	class AssetDatabase;
	class ECSWorld;
	struct SceneHeader;
	struct SystemContext;

	//============================================================================
	//	Tool structures
	//============================================================================

	// ツールの所有元。Game側で追加したツールも同じレジストリで扱う
	enum class ToolOwner : uint32_t {

		Engine,
		Game,
	};

	// ツールの実行条件や性質を表すフラグ
	enum class ToolFlags : uint32_t {

		None = 0,
		EditOnly = 1 << 0,
		AllowPlayMode = 1 << 1,
		Experimental = 1 << 2,
	};

	inline ToolFlags operator|(ToolFlags lhs, ToolFlags rhs) {

		return static_cast<ToolFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
	}

	inline bool HasToolFlag(ToolFlags flags, ToolFlags target) {

		return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(target)) != 0;
	}

	// ツール一覧や検索に使う情報
	struct ToolDescriptor {

		std::string id;
		std::string name;
		std::string category = "General";
		std::string description;
		ToolOwner owner = ToolOwner::Engine;
		ToolFlags flags = ToolFlags::EditOnly;
		int32_t order = 0;
	};

	// ツール実行時に渡すエンジン状態。ImGuiには依存させない
	struct ToolContext {

		ECSWorld* world = nullptr;
		AssetDatabase* assetDatabase = nullptr;
		SystemContext* systemContext = nullptr;

		const SceneHeader* activeSceneHeader = nullptr;
		AssetID activeSceneAsset{};
		UUID activeSceneInstanceID{};
		std::string_view activeScenePath{};

		bool isPlaying = false;
		bool canEditScene = false;
		float deltaTime = 0.0f;
	};
} // Engine
