#pragma once

#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

namespace Engine {

	enum class ShaderGraphNodeKind : uint8_t;
	struct ShaderGraphAsset;
	struct ShaderGraphIRModule;
	struct ShaderGraphDiagnostic;

	// decalGraph.hlsliと共有する投影式の命令
	enum class DecalGraphOp : uint32_t {
		Literal, Texture, UV, WorldPosition, WorldNormal, TextureSample,
		Split, Combine, NormalUnpack, TilingAndOffset, Add, Subtract,
		Multiply, Divide, Power, Minimum, Maximum, OneMinus,
		Lerp, Saturate, Sine, Cosine, Absolute, Floor,
		Fraction, SquareRoot, Negate, Step, Smoothstep, Clamp,
		Branch, Ceiling, Round, Sign, Truncate, Tangent,
		Reciprocal, Exponential, Log, Modulo, Output, Time,
	};

	struct DecalGraphInstruction {
		DecalGraphOp op = DecalGraphOp::Literal;
		uint32_t inputs = 0;
		Vector4 value{};
		MaterialParameterID parameter{};
		AssetID texture{};
		MaterialParameterSemantic semantic = MaterialParameterSemantic::None;
	};

	struct DecalGraphProgram {
		static constexpr size_t kMaxInstructions = 64;
		std::vector<DecalGraphInstruction> instructions;
		std::vector<MaterialParameterID> exposed;
		uint32_t channels = 0;
	};

	bool SupportsDecalGraphNode(ShaderGraphNodeKind kind);
	bool CompileDecalGraph(const ShaderGraphAsset& graph, const ShaderGraphIRModule& ir,
		DecalGraphProgram& program, std::vector<ShaderGraphDiagnostic>& diagnostics);
	bool CompilePaintGraph(const ShaderGraphAsset& graph, const ShaderGraphIRModule& ir,
		DecalGraphProgram& program, std::vector<ShaderGraphDiagnostic>& diagnostics);
	bool ReadDecalGraph(const nlohmann::json& data, DecalGraphProgram& program);
	nlohmann::json WriteDecalGraph(const DecalGraphProgram& program);
	Vector4 DecalGraphValue(const MaterialParameterValue& parameter);
}
