#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphNodeRegistry.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphNodeFactory class
	//	RenderPath用Node定義とNode生成を行うクラス
	//============================================================================
	class RenderPathGraphNodeFactory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// RenderPath用Node定義を登録する
		static void RegisterDefinitions(GraphNodeRegistry& registry);
		// ScenePassTypeからNode種別へ変換する
		static const char* ToNodeType(ScenePassType type);
		// Node種別からScenePassTypeへ変換する
		static ScenePassType ToPassType(const std::string& nodeType);
		// ScenePassに対応するNodeか判定する
		static bool IsScenePassNode(const std::string& nodeType);
		// ScenePassDescからNodeを作成する
		static GraphNode CreatePassNode(GraphDocument& document, const ScenePassDesc& pass,
			uint32_t passIndex, const nlohmann::json& rawPass, const ImVec2& position);
	};
} // Engine
