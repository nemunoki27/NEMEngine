#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphCompiler class
	//	RenderPath GraphからScenePassDescを生成するクラス
	//============================================================================
	class RenderPathGraphCompiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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
		// Node一つをScenePassDescへ変換する
		bool CompileNode(const GraphNode& node, const SceneHeader& baseHeader,
			ScenePassDesc& outPass, std::string* error) const;
	};
} // Engine
