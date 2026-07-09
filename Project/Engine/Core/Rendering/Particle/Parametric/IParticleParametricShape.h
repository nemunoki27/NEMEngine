#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// json
#include <json.hpp>

namespace Engine {

	// front
	struct ParticleRenderSettings;

	//============================================================================
	//	IParticleParametricShape class
	//	メッシュシェーダーで生成できる形状ごとの処理のインターフェース、状態を持たず共有される
	//============================================================================
	class IParticleParametricShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IParticleParametricShape() = default;
		virtual ~IParticleParametricShape() = default;

		// モジュールパラメータから開始/終了のshapeParamsを詰める
		virtual void PackShapeParams(const nlohmann::json& params, Vector4& start, Vector4& end) const = 0;
		// 開始/終了形状の編集UIを描画する、変更があればtrue
		virtual bool DrawImGui(nlohmann::json& params) const = 0;

		//--------- accessor -----------------------------------------------------

		// 専用MSパイプライン
		virtual AssetID GetPipeline() const = 0;
		// 円周の分割数
		virtual int32_t GetDivide(const ParticleRenderSettings& settings) const = 0;
	};
} // Engine
