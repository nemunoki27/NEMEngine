#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RayTracingProfile structures
	//============================================================================
	enum class RayTracingExecutionPoint : uint8_t {

		AfterLighting,
		AfterTransparent,
	};

	enum class RayTracingTextureSource : uint8_t {

		SceneColor,
		SceneDepth,
		GBufferAlbedo,
		GBufferNormal,
		GBufferPosition,
		GBufferMaterial,
		GBufferEmissive,
		GBufferFlags,
	};

	struct RayTracingInputBinding {

		std::string shaderResource;
		RayTracingTextureSource source = RayTracingTextureSource::SceneColor;
	};

	struct RayTracingTextureBinding {

		std::string shaderResource;
		AssetID texture{};
	};

	struct RayTracingEffectSettings {

		UUID id{};
		std::string name = "Ray Tracing Effect";
		bool enabled = true;
		bool gameView = true;
		bool sceneView = true;
		RayTracingExecutionPoint executionPoint =
			RayTracingExecutionPoint::AfterLighting;
		AssetID material{};
		uint32_t rayGenerationIndex = 0;
		std::vector<RayTracingInputBinding> inputs{};
		std::vector<RayTracingTextureBinding> textures{};
		MaterialParameterSet parameterOverrides{};
	};

	struct RayTracingProfileAsset {

		AssetID guid{};
		std::string name = "Ray Tracing Profile";
		uint32_t version = 1;
		std::vector<RayTracingEffectSettings> effects{};
	};

	bool FromJson(const nlohmann::json& data,
		RayTracingProfileAsset& outAsset);
	nlohmann::json ToJson(const RayTracingProfileAsset& asset);
} // Engine
