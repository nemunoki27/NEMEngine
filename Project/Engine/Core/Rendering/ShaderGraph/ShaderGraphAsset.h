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

	// ノードが評価されるシェーダーステージ
	enum class ShaderGraphStage : uint8_t {

		Any,
		Vertex,
		Fragment,
		Compute,
		RayClosestHit,
	};

	// 演算精度
	enum class ShaderGraphPrecision : uint8_t {

		Inherit,
		Float,
		Half,
	};

	// Materialパラメータの更新単位
	enum class ShaderGraphParameterScope : uint8_t {

		PerMaterial,
		PerInstance,
		Global,
	};

	// 生成PSを適用するRenderer
	enum class ShaderGraphTarget : uint8_t {

		Mesh,
		Primitive3D,
		Sprite,
		Text,
		Primitive2D,
		Particle,
		Trail,
	};

	enum class ShaderGraphValueType : uint8_t {

		Invalid = 0,
		Float,
		Float2,
		Float3,
		Float4,
		Color,
		Texture2D,
		SamplerState,
		Boolean,
		Integer,
		Matrix4,
	};

	// Keywordの種類
	enum class ShaderGraphKeywordType : uint8_t {

		Boolean,
		Enum,
	};

	// Custom Functionのソース形式
	enum class ShaderGraphCustomFunctionSource : uint8_t {

		Inline,
		File,
	};

	enum class ShaderGraphNodeKind : uint8_t {

		SurfaceOutput,
		UnlitOutput,
		PostProcessOutput,
		Parameter,
		Constant,
		UV,
		WorldNormal,
		WorldPosition,
		Time,
		Add,
		Subtract,
		Multiply,
		Divide,
		Power,
		Lerp,
		OneMinus,
		Saturate,
		Sine,
		Remap,
		TilingAndOffset,
		PolarCoordinates,
		Split,
		Combine,
		TextureSample,
		SamplerState,
		NormalUnpack,
		SceneColor,
		SceneDepth,
		SceneNormal,
		ScenePosition,
		SceneMaterial,
		SceneEmissive,
		SceneFlags,
		VertexColor,
		ViewDirection,
		ScreenPosition,
		ObjectPosition,
		ObjectNormal,
		ObjectTangent,
		Absolute,
		Cosine,
		Floor,
		Fraction,
		SquareRoot,
		Negate,
		Normalize,
		Length,
		Minimum,
		Maximum,
		Dot,
		Cross,
		Distance,
		Clamp,
		Step,
		Smoothstep,
		Branch,
		Reflect,
		Fresnel,
		Rotate,
		SimpleNoise,
		Voronoi,
		SubGraph,
		CustomFunction,
		Keyword,
		VertexOutput,
	};

	struct ShaderGraphPort {

		UUID id{};
		std::string name;
		ShaderGraphValueType type = ShaderGraphValueType::Float;
		MaterialParameterValue defaultValue{};
	};

	struct ShaderGraphParameter {

		UUID id{};
		std::string name;
		ShaderGraphValueType type = ShaderGraphValueType::Float;
		MaterialParameterSemantic semantic = MaterialParameterSemantic::None;
		MaterialParameterValue defaultValue{};
		ShaderGraphPrecision precision = ShaderGraphPrecision::Inherit;
		ShaderGraphParameterScope scope = ShaderGraphParameterScope::PerMaterial;
		bool exposed = true;
		std::string referenceName;
	};

	struct ShaderGraphKeyword {

		UUID id{};
		std::string name;
		std::string referenceName;
		ShaderGraphKeywordType type = ShaderGraphKeywordType::Boolean;
		std::vector<std::string> entries;
		uint32_t defaultIndex = 0;
		// Material定数から実行中に切り替える
		bool runtimeToggle = false;
	};

	struct ShaderGraphNode {

		UUID id{};
		UUID groupID{};
		ShaderGraphNodeKind kind = ShaderGraphNodeKind::Constant;
		UUID parameterID{};
		ShaderGraphValueType valueType = ShaderGraphValueType::Float;
		MaterialParameterValue value{};
		Vector2 position{};
		ShaderGraphStage stage = ShaderGraphStage::Any;
		ShaderGraphPrecision precision = ShaderGraphPrecision::Inherit;
		std::vector<ShaderGraphPort> inputPorts;
		std::vector<ShaderGraphPort> outputPorts;
		AssetID subGraph{};
		UUID keywordID{};
		ShaderGraphCustomFunctionSource customFunctionSource =
			ShaderGraphCustomFunctionSource::Inline;
		std::string functionName;
		// File形式のHLSLをAssetDatabaseの依存関係へ接続する
		AssetID functionFileAsset{};
		std::string functionFile;
		std::string functionBody;
		PipelineStaticSamplerSettings sampler{};
		bool previewExpanded = true;
	};

	struct ShaderGraphLink {

		UUID id{};
		UUID outputNode{};
		uint32_t outputSlot = 0;
		UUID inputNode{};
		uint32_t inputSlot = 0;
	};

	struct ShaderGraphGroup {

		UUID id{};
		std::string name = "Group";
		Vector2 position{};
		Vector2 size{ 320.0f, 180.0f };
	};

	// Surfaceの描画状態
	struct ShaderGraphRenderState {

		bool twoSided = false;
		bool depthWrite = true;
		bool depthTest = true;
		bool alphaClipping = false;
		bool castShadows = true;
		bool receiveShadows = true;
		BlendMode blendMode = BlendMode::Normal;
		D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID;
		D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK;
		bool frontCounterClockwise = false;
		bool depthClipEnable = true;
		D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		bool stencilEnable = false;
	};

	struct ShaderGraphAsset {

		std::string name = "NewShaderGraph";
		ShaderGraphDomain domain = ShaderGraphDomain::Surface;
		ShaderGraphSurfaceMode surfaceMode = ShaderGraphSurfaceMode::Opaque;
		ShaderGraphTarget target = ShaderGraphTarget::Mesh;
		ShaderGraphPrecision defaultPrecision = ShaderGraphPrecision::Float;
		ShaderGraphRenderState renderState{};
		std::vector<ShaderGraphParameter> parameters;
		std::vector<ShaderGraphKeyword> keywords;
		std::vector<ShaderGraphNode> nodes;
		std::vector<ShaderGraphGroup> groups;
		std::vector<ShaderGraphLink> links;
		UUID outputNode{};
		UUID vertexOutputNode{};
	};

	// Rendererに合わせた最小構成のSurfaceグラフを生成する
	ShaderGraphAsset CreateDefaultSurfaceShaderGraph(
		std::string_view name,
		ShaderGraphTarget target = ShaderGraphTarget::Mesh);
	// 入力カラーをそのまま出力するPostProcessグラフを生成
	ShaderGraphAsset CreateDefaultPostProcessShaderGraph(
		std::string_view name);
	bool IsShaderGraph3DTarget(ShaderGraphTarget target);
	bool SupportsShaderGraphVertexOutput(ShaderGraphTarget target);

	// ノードUIとコンパイラが共有する固定ピン情報
	std::string_view GetShaderGraphNodeName(ShaderGraphNodeKind kind);
	uint32_t GetShaderGraphInputCount(ShaderGraphNodeKind kind);
	uint32_t GetShaderGraphOutputCount(ShaderGraphNodeKind kind);
	std::string_view GetShaderGraphInputName(ShaderGraphNodeKind kind, uint32_t slot);
	std::string_view GetShaderGraphOutputName(ShaderGraphNodeKind kind, uint32_t slot);
	uint32_t GetShaderGraphInputCount(const ShaderGraphNode& node);
	uint32_t GetShaderGraphOutputCount(const ShaderGraphNode& node);
	std::string_view GetShaderGraphInputName(const ShaderGraphNode& node, uint32_t slot);
	std::string_view GetShaderGraphOutputName(const ShaderGraphNode& node, uint32_t slot);

	// JSON変換
	bool FromJson(const nlohmann::json& data, ShaderGraphAsset& outAsset);
	nlohmann::json ToJson(const ShaderGraphAsset& asset);
} // Engine
