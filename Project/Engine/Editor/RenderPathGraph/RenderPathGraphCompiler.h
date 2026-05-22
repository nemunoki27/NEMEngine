#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderPathGraphCompileResult structure
	//	Graphから作成したSceneHeader描画情報
	//============================================================================
	struct RenderPathGraphCompileResult {

		//--------- variables ----------------------------------------------------

		// Compile後のRenderTarget
		std::vector<SceneRenderTargetDesc> renderTargets;
		// Compile後のPass順
		std::vector<ScenePassDesc> passOrder;
	};

	//============================================================================
	//	RenderPathFrameCapturePass structure
	//	Runtime Captureで受け取るPass情報
	//============================================================================
	struct RenderPathFrameCapturePass {

		//--------- variables ----------------------------------------------------

		std::string name;
		std::string type;
		std::vector<std::string> reads;
		std::vector<std::string> writes;
		float cpuTimeMs = 0.0f;
		float gpuTimeMs = 0.0f;
	};

	//============================================================================
	//	RenderPathResourceLifetime structure
	//	Resource Lifetime表示用の情報
	//============================================================================
	struct RenderPathResourceLifetime {

		//--------- variables ----------------------------------------------------

		std::string resource;
		std::vector<std::string> passes;
	};

	//============================================================================
	//	RenderPathGraphCompiler class
	//	RenderPath GraphからSceneHeader描画情報を生成するクラス
	//============================================================================
	class RenderPathGraphCompiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// GraphをSceneHeader.passOrderへ変換する
		bool Compile(const GraphDocument& document, const SceneHeader& baseHeader,
			RenderPathGraphCompileResult& outResult, std::string* error = nullptr) const;
		// GraphをSceneHeader.passOrderへ変換する
		bool Compile(const GraphDocument& document, const SceneHeader& baseHeader,
			std::vector<ScenePassDesc>& outPassOrder, std::string* error = nullptr) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// Flow Linkを元に実行順を作成する
		std::vector<const GraphNode*> BuildExecutionOrder(const GraphDocument& document) const;
		// TemporaryTarget NodeからRenderTargetを作成する
		std::vector<SceneRenderTargetDesc> CompileRenderTargets(
			const GraphDocument& document, const SceneHeader& baseHeader) const;
		// Node一つをScenePassDescへ変換する
		bool CompileNode(const GraphDocument& document, const GraphNode& node, const SceneHeader& baseHeader,
			ScenePassDesc& outPass, std::string* error) const;
	};
} // Engine
