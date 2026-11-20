#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

namespace Engine {

	class ECSWorld;
	struct Entity;
	struct SubMeshMaterial;

	//============================================================================
	//	HexTilingComponent struct
	//	六角形タイリングの設定
	//============================================================================
	struct HexTilingComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		bool enabled = true;
		float tileSize = 1.0f;
		float randomRotation = 0.0f;
		int32_t seed = 0;
		std::vector<uint64_t> excludedSubMeshIDs;
		std::vector<uint32_t> excludedSubMeshIndices;
	};

	bool IsHexTilingSubMeshEnabled(const HexTilingComponent& component, const SubMeshMaterial& subMesh, uint32_t index);
	void SetHexTilingSubMeshEnabled(HexTilingComponent& component, const SubMeshMaterial& subMesh, uint32_t index, bool enabled);
	Vector4 ResolveHexTiling(const ECSWorld& world, Entity entity, int32_t subMeshIndex = -1);

	void from_json(const nlohmann::json& in, HexTilingComponent& component);
	void to_json(nlohmann::json& out, const HexTilingComponent& component);

} // Engine
