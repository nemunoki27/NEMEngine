#pragma once

#include "ProductBinary.h"
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>

namespace Engine::ProductMesh {

	enum class Section { Geometry, Layout, Animation };

	bool Cook(const std::filesystem::path& source, AssetID mesh, std::vector<uint8_t>& bytes);
	void CookLayout(const std::filesystem::path& source, std::vector<uint8_t>& bytes);
	std::span<const uint8_t> ReadSection(const std::filesystem::path& path, Section section, std::vector<uint8_t>& storage);
	void LoadGeometry(const std::filesystem::path& path, ImportedMeshAsset& mesh);
	void LoadAnimation(const std::filesystem::path& path, SkinnedMeshAnimationSet& animation);

	template<class A>
	void TexturePaths(A& archive, ImportedMeshTextureSet& textures) {
		using ProductBinary::String;
		String(archive, textures.baseColorTexturePath);
		String(archive, textures.normalTexturePath);
		String(archive, textures.metallicRoughnessTexturePath);
		String(archive, textures.metallicTexturePath);
		String(archive, textures.roughnessTexturePath);
		String(archive, textures.displacementTexturePath);
		String(archive, textures.specularTexturePath);
		String(archive, textures.opacityTexturePath);
		String(archive, textures.emissiveTexturePath);
		String(archive, textures.occlusionTexturePath);
	}
}
