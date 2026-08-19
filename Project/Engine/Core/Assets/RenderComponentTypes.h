#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Externals/nlohmann/json_fwd.hpp>

// c++
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Engine {

	// GBufferの上位24bitへ格納する描画対象マスク
	inline constexpr uint32_t kRenderingLayerMaskBits =
		0x00ffffffu;

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

	// マテリアル表面の描画方式
	enum class MaterialSurfaceMode : uint8_t {

		Auto,
		Opaque,
		Masked,
		Transparent,
	};

	// ブレンドモード
	enum BlendMode {

		Normal,   // 通常αブレンド
		Add,      // 加算
		Subtract, // 減算
		Multiply, // 乗算
		Screen,   // スクリーン
		Premultiplied, // 乗算済みα
	};
	static constexpr const uint32_t kBlendModeCount = static_cast<uint32_t>(BlendMode::Premultiplied) + 1;

	// Renderer固有の疎なマテリアル値
	using MaterialInstanceParameters = MaterialParameterSet;

	// ライン1点の情報、頂点ごとに太さと色を持てる
	struct LinePoint {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 2;
		static constexpr bool kSerializable = false;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;

		Vector3 position = Vector3::AnyInit(0.0f);
		Color4 color = Color4::White();
		// ライン半幅
		float thickness = 1.0f;
	};

	// 単一パラメータ値のJSON変換
	bool ParseMaterialParameterValue(const nlohmann::json& data, MaterialParameterValue& outValue);
	nlohmann::json SerializeMaterialParameterValue(const MaterialParameterValue& parameter);

	// Renderer固有Material InstanceのJSON入出力
	void ReadMaterialInstance(const nlohmann::json& in,
		MaterialInstanceParameters& outOverrides);
	nlohmann::json WriteMaterialInstance(
		const MaterialInstanceParameters& overrides);

	// RenderPhaseの文字列変換、JSON保存やデバッグ表示に使う
	std::string_view ToString(RenderPhase phase);
	bool TryParseRenderPhase(std::string_view value, RenderPhase& outPhase);
	RenderPhase RenderPhaseFromString(std::string_view value, RenderPhase fallback = RenderPhase::Opaque);
	// 表面方式から描画フェーズを解決する
	RenderPhase ResolveMaterialRenderPhase(MaterialSurfaceMode surfaceMode,
		RenderPhase fallback = RenderPhase::Opaque);
	// 表面方式からブレンドモードを解決する
	BlendMode ResolveMaterialBlendMode(MaterialSurfaceMode surfaceMode,
		BlendMode fallback = BlendMode::Normal);

	// 各Rendererコンポーネントが共通で持つ描画フィールドのjson入出力、既定値は現在値を使う
	void ReadRenderCommonFields(const nlohmann::json& in,
		int32_t& layer, int32_t& order, bool& visible, BlendMode& blendMode, RenderPhase& queue);
	void WriteRenderCommonFields(nlohmann::json& out,
		int32_t layer, int32_t order, bool visible, BlendMode blendMode, RenderPhase queue);

} // Engine
