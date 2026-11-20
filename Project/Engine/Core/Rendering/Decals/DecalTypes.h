#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

// c++
#include <cstdint>
#include <array>
#include <vector>

namespace Engine {

	//============================================================================
	//	Decal structures
	//	通常描画と反射で共有する投影データ
	//============================================================================
	struct DecalGPUData {

		Matrix4x4 worldToDecal = Matrix4x4::Identity();
		Vector4 tangentX{};
		Vector4 tangentY{};
		Vector4 direction{};
		Color4 baseColor = Color4::White();
		Color4 emissive{};
		Vector4 surface{};
		Vector4 uvTransform{1.0f, 0.0f, 0.0f, 1.0f};
		// 角度開始と終了のcos、UV平行移動
		Vector4 fade{};
		uint32_t baseColorTexture = UINT32_MAX;
		uint32_t normalTexture = UINT32_MAX;
		uint32_t metallicRoughnessTexture = UINT32_MAX;
		uint32_t emissiveTexture = UINT32_MAX;
		uint32_t channels = 1u;
		uint32_t receiverMask = 0x00ffffffu;
		uint32_t graphOffset = 0;
		uint32_t graphCount = 0;
		Vector4 hexTiling{};
	};
	static_assert(sizeof(DecalGPUData) == 240);

	struct DecalProjection {

		Matrix4x4 decalToWorld = Matrix4x4::Identity();
		Matrix4x4 worldToDecal = Matrix4x4::Identity();
		Vector3 center{};
	};

	struct DecalDrawItem {

		Entity entity = Entity::Null();
		int32_t sortOrder = 0;
		DecalProjection projection{};
		DecalGPUData gpu{};
		std::array<TextureOwnership, 4> textures{};
		std::vector<TextureOwnership> graphTextures;
		std::vector<uint32_t> graphWords;
	};

	struct DecalReceiverRange {

		uint32_t offset = 0;
		uint32_t count = 0;
	};
}
