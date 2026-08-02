#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ShaderGraphIR structures
	//============================================================================
	enum class ShaderGraphDiagnosticSeverity : uint8_t {

		Info,
		Warning,
		Error,
	};

	struct ShaderGraphDiagnostic {

		ShaderGraphDiagnosticSeverity severity =
			ShaderGraphDiagnosticSeverity::Error;
		ShaderGraphStage stage = ShaderGraphStage::Any;
		UUID node{};
		uint32_t port = UINT32_MAX;
		std::string message;
	};

	struct ShaderGraphIRInput {

		UUID sourceNode{};
		uint32_t sourcePort = 0;
		uint32_t destinationPort = 0;
	};

	struct ShaderGraphIRInstruction {

		UUID sourceNode{};
		ShaderGraphNodeKind kind = ShaderGraphNodeKind::Constant;
		ShaderGraphStage stage = ShaderGraphStage::Any;
		ShaderGraphPrecision precision = ShaderGraphPrecision::Float;
		std::vector<ShaderGraphIRInput> inputs;
	};

	struct ShaderGraphIRModule {

		std::vector<ShaderGraphIRInstruction> vertexInstructions;
		std::vector<ShaderGraphIRInstruction> fragmentInstructions;
		std::vector<ShaderGraphIRInstruction> computeInstructions;
		std::vector<ShaderGraphDiagnostic> diagnostics;

		bool Succeeded() const;
	};

	//============================================================================
	//	ShaderGraphIRBuilder class
	//	グラフをステージ別の検証済み中間表現へ変換する
	//============================================================================
	class ShaderGraphIRBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphIRBuilder() = delete;
		~ShaderGraphIRBuilder() = delete;

		// グラフ全体を検証して中間表現を構築
		static ShaderGraphIRModule Build(const ShaderGraphAsset& graph);
	};
} // Engine
