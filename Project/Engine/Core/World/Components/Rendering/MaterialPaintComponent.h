#pragma once

#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <vector>

namespace Engine {

	struct MaterialPaintLayer {

		AssetID material{};
		uint32_t channel = 0;
		bool visible = true;
		bool heightBlend = false;
		float heightContrast = 1.0f;
	};

	// オブジェクト固有のペイント領域と素材を保持する
	struct MaterialPaintComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		AssetID atlas{};
		AssetID mask{};
		std::string geometryKey;
		uint32_t resolution = 1024;
		Matrix4x4 projectionFromLocal = Matrix4x4::Identity();
		std::vector<MaterialPaintLayer> layers;
		std::vector<AssetID> baseHeightTextures;
	};

	void to_json(nlohmann::json& out, const MaterialPaintComponent& component);
	void from_json(const nlohmann::json& in, MaterialPaintComponent& component);
}
