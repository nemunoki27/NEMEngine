#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphImporter class
	//	SceneHeader.passOrderからRenderPath Graphを作成するクラス
	//============================================================================
	class RenderPathGraphImporter {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// SceneHeaderからGraphDocumentを作成する
		bool Import(const SceneHeader& sceneHeader, const std::string& scenePath, GraphDocument& outDocument) const;
	};
} // Engine
