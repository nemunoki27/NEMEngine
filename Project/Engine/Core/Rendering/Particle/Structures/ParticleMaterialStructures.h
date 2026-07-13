#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticleMaterial structures
	//============================================================================
	// パーティクル専用マテリアルパラメータのアニメーション方法
	enum class ParticleMaterialParameterMode :
		uint8_t {

		Constant,
		OverLifetime,
	};

	// float4で扱えるマテリアルパラメータ、シェーダー側ではslot番号で参照する
	struct ParticleMaterialAnimatedParameter {

		ParticleMaterialParameterMode mode = ParticleMaterialParameterMode::Constant;
		Vector4 constant = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		Vector4 start = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		Vector4 end = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		EasingType easingType = EasingType::EaseOutSine;
		CurveVector3 curve3{};
		CurveFloat curveW{};
		ParticleLoopSettings loop{};
		uint32_t componentCount = 1;
		bool useCurve = false;
	};

	// フェーズ単位で持つマテリアル上書き設定
	struct ParticlePhaseMaterialSettings {

		// baseColorTextureはParticleでは常に公開する
		AssetID baseColorTexture{};
		// reflectionで列挙したbaseColorTexture以外のテクスチャ上書き
		std::unordered_map<std::string, AssetID> textureOverrides{};
		// reflectionから追加された任意パラメータ
		std::unordered_map<std::string, ParticleMaterialAnimatedParameter> parameters{};
	};

	// json変換
	void to_json(nlohmann::json& out, const ParticleMaterialAnimatedParameter& value);
	void from_json(const nlohmann::json& in, ParticleMaterialAnimatedParameter& value);
	void to_json(nlohmann::json& out, const ParticlePhaseMaterialSettings& value);
	void from_json(const nlohmann::json& in, ParticlePhaseMaterialSettings& value);

	// 評価
	Vector4 EvaluateParticleMaterialParameter(const ParticleMaterialAnimatedParameter& parameter, float t);
} // Engine
