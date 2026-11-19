#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <array>
#include <cstdint>
#include <vector>

namespace Engine {

	//============================================================================
	//	ShaderGraphNodeSettings structures
	//============================================================================
	enum class ShaderGraphMathBase : uint8_t { BaseE, Base2, Base10 };
	enum class ShaderGraphComparison : uint8_t { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual };
	enum class ShaderGraphMatrixAxis : uint8_t { Row, Column };
	enum class ShaderGraphNormalBlend : uint8_t { Default, Reoriented };
	enum class ShaderGraphNormalSpace : uint8_t { Tangent, World };
	enum class ShaderGraphColorSpace : uint8_t { RGB, Linear, HSV };
	enum class ShaderGraphCoordinateSpace : uint8_t { Object, World, View, Tangent, Clip };
	enum class ShaderGraphTransformType : uint8_t { Position, Direction, Normal };
	enum class ShaderGraphTransformationMatrix : uint8_t {

		ObjectToWorld, WorldToObject, WorldToView, ViewToWorld,
		WorldToClip, ClipToWorld, ViewToClip, ClipToView,
	};
	enum class ShaderGraphBlend : uint8_t {

		Burn, Darken, Difference, Dodge, Divide, Exclusion, HardLight, HardMix, Lighten,
		LinearBurn, LinearDodge, LinearLight, LinearLightAddSub, Multiply, Negation,
		Overlay, PinLight, Screen, SoftLight, Subtract, VividLight, Overwrite,
	};
	enum class ShaderGraphGradientMode : uint8_t { Blend, Fixed };
	enum class ShaderGraphDielectricMaterial : uint8_t { Common, RustedMetal, Water, Ice, Glass, Custom };
	enum class ShaderGraphMetalMaterial : uint8_t { Iron, Silver, Aluminium, Gold, Copper, Chromium, Nickel, Titanium, Cobalt, Platinum };

	// ノード固有の編集値
	struct ShaderGraphNodeSettings {

		ShaderGraphMathBase mathBase = ShaderGraphMathBase::BaseE;
		ShaderGraphComparison comparison = ShaderGraphComparison::Equal;
		ShaderGraphMatrixAxis matrixAxis = ShaderGraphMatrixAxis::Row;
		ShaderGraphNormalBlend normalBlend = ShaderGraphNormalBlend::Default;
		ShaderGraphNormalSpace normalSpace = ShaderGraphNormalSpace::Tangent;
		ShaderGraphColorSpace colorFrom = ShaderGraphColorSpace::RGB;
		ShaderGraphColorSpace colorTo = ShaderGraphColorSpace::HSV;
		ShaderGraphCoordinateSpace spaceFrom = ShaderGraphCoordinateSpace::World;
		ShaderGraphCoordinateSpace spaceTo = ShaderGraphCoordinateSpace::Object;
		ShaderGraphTransformType transformType = ShaderGraphTransformType::Position;
		ShaderGraphTransformationMatrix transformationMatrix = ShaderGraphTransformationMatrix::ObjectToWorld;
		ShaderGraphBlend blend = ShaderGraphBlend::Overlay;
		ShaderGraphGradientMode gradientMode = ShaderGraphGradientMode::Blend;
		ShaderGraphDielectricMaterial dielectricMaterial = ShaderGraphDielectricMaterial::Common;
		ShaderGraphMetalMaterial metalMaterial = ShaderGraphMetalMaterial::Iron;
		uint32_t channelMask = 15;
		std::array<uint32_t, 4> swizzle{ 0, 1, 2, 3 };
		bool degrees = false;
		bool normalize = true;
		bool normalMap = false;
		std::array<Vector4, 4> matrixRows{
			Vector4{ 1, 0, 0, 0 }, Vector4{ 0, 1, 0, 0 },
			Vector4{ 0, 0, 1, 0 }, Vector4{ 0, 0, 0, 1 },
		};
		std::vector<Vector4> gradientColors;
		std::vector<Vector2> gradientAlphas;
	};

	// ノード固有値を読み込む
	void ReadShaderGraphNodeSettings(const nlohmann::json& data, ShaderGraphNodeSettings& settings);
	// ノード固有値を保存する
	nlohmann::json WriteShaderGraphNodeSettings(const ShaderGraphNodeSettings& settings);
} // Engine
