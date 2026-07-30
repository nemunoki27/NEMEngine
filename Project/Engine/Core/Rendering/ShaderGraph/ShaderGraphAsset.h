#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	//============================================================================
	//	ShaderGraph structures
	//============================================================================
	enum class ShaderGraphDomain : uint8_t {

		Surface,
		PostProcess,
	};

	enum class ShaderGraphSurfaceMode : uint8_t {

		Opaque,
		Transparent,
	};

	enum class ShaderGraphValueType : uint8_t {

		Invalid = 0,
		Float,
		Float2,
		Float3,
		Float4,
		Color,
		Texture2D,
	};

	enum class ShaderGraphNodeKind : uint8_t {

		SurfaceOutput,
		PostProcessOutput,
		Parameter,
		Constant,
		UV,
		WorldNormal,
		WorldPosition,
		Add,
		Multiply,
		Lerp,
		OneMinus,
		Saturate,
		TextureSample,
		NormalUnpack,
		SceneColor,
	};

	struct ShaderGraphParameter {

		UUID id{};
		std::string name;
		ShaderGraphValueType type = ShaderGraphValueType::Float;
		MaterialParameterSemantic semantic = MaterialParameterSemantic::None;
		MaterialParameterValue defaultValue{};
	};

	struct ShaderGraphNode {

		UUID id{};
		ShaderGraphNodeKind kind = ShaderGraphNodeKind::Constant;
		UUID parameterID{};
		ShaderGraphValueType valueType = ShaderGraphValueType::Float;
		MaterialParameterValue value{};
		Vector2 position{};
	};

	struct ShaderGraphLink {

		UUID id{};
		UUID outputNode{};
		uint32_t outputSlot = 0;
		UUID inputNode{};
		uint32_t inputSlot = 0;
	};

	struct ShaderGraphAsset {

		std::string name = "NewShaderGraph";
		ShaderGraphDomain domain = ShaderGraphDomain::Surface;
		ShaderGraphSurfaceMode surfaceMode = ShaderGraphSurfaceMode::Opaque;
		std::vector<ShaderGraphParameter> parameters;
		std::vector<ShaderGraphNode> nodes;
		std::vector<ShaderGraphLink> links;
		UUID outputNode{};

		// コンパイル生成物をAssetDatabaseのGUIDで追跡する
		AssetID generatedMaterial{};
		AssetID generatedOpaqueShader{};
		AssetID generatedTransparentShader{};
	};

	// 新規3Dグラフを標準PBRパラメータ付きで生成する
	ShaderGraphAsset CreateDefaultSurfaceShaderGraph(std::string_view name);

	// ノードUIとコンパイラが共有する固定ピン情報
	std::string_view GetShaderGraphNodeName(ShaderGraphNodeKind kind);
	uint32_t GetShaderGraphInputCount(ShaderGraphNodeKind kind);
	uint32_t GetShaderGraphOutputCount(ShaderGraphNodeKind kind);
	std::string_view GetShaderGraphInputName(ShaderGraphNodeKind kind, uint32_t slot);
	std::string_view GetShaderGraphOutputName(ShaderGraphNodeKind kind, uint32_t slot);

	// JSON変換
	bool FromJson(const nlohmann::json& data, ShaderGraphAsset& outAsset);
	nlohmann::json ToJson(const ShaderGraphAsset& asset);
} // Engine
