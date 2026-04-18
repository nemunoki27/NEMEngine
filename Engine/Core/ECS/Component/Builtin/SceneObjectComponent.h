#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Asset/AssetTypes.h>

namespace Engine {

	// front
	struct Entity;
	class ECSWorld;

	//============================================================================
	//	SceneObjectComponent struct
	//============================================================================

	// 名前
	struct SceneObjectComponent {

		// シーン、プレファブファイル内での永続ID
		UUID localFileID{};

		// 自分自身のアクティブ状態
		bool activeSelf = true;

		// ランタイム使用用
		AssetID sourceAsset{};
		UUID sceneInstanceID{};
		// 親階層を含めたアクティブ状態
		bool activeInHierarchy = true;

		// カメラのカリングマスクと照合する可視レイヤーマスク
		uint32_t visibilityLayerMask = 0xFFFFFFFFu;
	};

	// json変換
	void from_json(const nlohmann::json& in, SceneObjectComponent& component);
	void to_json(nlohmann::json& out, const SceneObjectComponent& component);

	// helpers
	// 階層内でアクティブか
	bool IsEntityActiveInHierarchy(ECSWorld& world, const Entity& entity);

	ENGINE_REGISTER_COMPONENT(SceneObjectComponent, "SceneObject");
} // Engine