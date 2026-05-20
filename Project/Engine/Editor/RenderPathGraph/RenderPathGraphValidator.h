#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Editor/Graph/GraphValidationTypes.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphValidator class
	//	RenderPath Graphの検証を行うクラス
	//============================================================================
	class RenderPathGraphValidator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Graphの接続とPass設定を検証する
		GraphValidationResult Validate(GraphDocument& document, const SceneHeader* sceneHeader) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// Linkの型と接続先を検証する
		void ValidateLinks(const GraphDocument& document, GraphValidationResult& result) const;
		// Flowの入口と接続順を検証する
		void ValidateFlow(const GraphDocument& document, GraphValidationResult& result) const;
		// Passごとのプロパティを検証する
		void ValidatePassProperties(const GraphDocument& document,
			const SceneHeader* sceneHeader, GraphValidationResult& result) const;
	};
} // Engine
