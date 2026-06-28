#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Externals/nlohmann/json_fwd.hpp>

// c++
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace Engine {

	//============================================================================
	//	RenderComponentTypes
	//============================================================================

	// 描画フェーズ
	enum class RenderPhase : uint8_t {

		Opaque,
		Transparent,
		PostProcessMaskedUI,
		ScreenUI,
		EditorOverlay,
		Count
	};
	// Countは実描画フェーズではなく番兵として扱う
	static constexpr size_t kRenderPhaseCount = static_cast<size_t>(RenderPhase::Count);

	// ブレンドモード
	enum BlendMode {

		Normal,   // 通常αブレンド
		Add,      // 加算
		Subtract, // 減算
		Multiply, // 乗算
		Screen,   // スクリーン
	};
	static constexpr const uint32_t kBlendModeCount = static_cast<uint32_t>(BlendMode::Screen) + 1;

	// マテリアルのパラメーター値
	struct MaterialParameterValue {

		std::variant<float, Vector2, Vector3, Vector4, Color4, AssetID, int32_t, uint32_t, bool> value;
	};

	// ライン1点の情報、頂点ごとに太さと色を持てる
	struct LinePoint {

		Vector3 position = Vector3::AnyInit(0.0f);
		Color4 color = Color4::White();
		// ライン半幅
		float thickness = 1.0f;
	};

	// 単一パラメータ値のjson変換でサブメッシュ側のparameterOverridesでも共用する
	bool ParseMaterialParameterValue(const nlohmann::json& data, MaterialParameterValue& outValue);
	nlohmann::json SerializeMaterialParameterValue(const MaterialParameterValue& parameter);

	// parameterOverridesマップのjson入出力、Mesh/Sprite/Text等の個別マテリアルで共用する
	void ReadMaterialParameterOverrides(const nlohmann::json& in,
		std::unordered_map<std::string, MaterialParameterValue>& outOverrides);
	nlohmann::json WriteMaterialParameterOverrides(
		const std::unordered_map<std::string, MaterialParameterValue>& overrides);

	// RenderPhaseの文字列変換、JSON保存やデバッグ表示に使う
	std::string_view ToString(RenderPhase phase);
	bool TryParseRenderPhase(std::string_view value, RenderPhase& outPhase);
	RenderPhase RenderPhaseFromString(std::string_view value, RenderPhase fallback = RenderPhase::Opaque);

} // Engine
