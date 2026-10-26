#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <vector>

namespace Engine {

	enum class ParticleSourceKind : uint8_t {

		Analytic, MeshVertex, MeshEdge, MeshSurface, Texture, CameraSurface,
	};

	enum class ParticleSourceChannel : uint8_t {

		Red, Green, Blue, Alpha, Luminance,
	};

	//============================================================================
	//	ParticleSourceSettings structure
	//	GPU粒子の発生元と継承する表面情報を指定する
	//============================================================================
	struct ParticleSourceSettings {

		ParticleSourceKind kind = ParticleSourceKind::Analytic;
		AssetID mesh{};
		SceneEntityReference renderer{};
		std::vector<uint32_t> subMeshes;
		AssetID texture{};
		SceneEntityReference camera{};
		Vector2 textureSize{1.0f, 1.0f};
		float surfaceOffset = 0.0f;
		bool sequential = false;
		bool attachToSurface = false;
		bool inheritColor = false;
		float inheritVelocity = 0.0f;
		bool useMask = false;
		bool maskFromVertexColors = false;
		AssetID maskTexture{};
		ParticleSourceChannel maskChannel = ParticleSourceChannel::Alpha;
		float maskMinimum = 0.0f;
		float maskMaximum = 1.0f;
	};

	// 参照AssetとEntityを既存の保存形式で読み書きする
	void from_json(const nlohmann::json& in, ParticleSourceSettings& settings);
	void to_json(nlohmann::json& out, const ParticleSourceSettings& settings);
}
