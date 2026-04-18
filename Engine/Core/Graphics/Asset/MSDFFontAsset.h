#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/MathLib/Vector2.h>

// c++
#include <optional>
#include <unordered_map>
#include <string>

namespace Engine {

	//============================================================================
	//	MSDFFontAsset structures
	//============================================================================

	// グリフの平面上の境界
	struct MSDFPlaneBounds {

		float left = 0.0f;
		float bottom = 0.0f;
		float right = 0.0f;
		float top = 0.0f;
	};

	// グリフのアトラス上の境界
	struct MSDFAtlasBounds {
		int32_t left = 0;
		int32_t bottom = 0;
		int32_t right = 0;
		int32_t top = 0;
	};

	// グリフの情報
	struct MSDFGlyph {

		char32_t codepoint = U'?';
		float advance = 0.0f;

		std::optional<MSDFPlaneBounds> planeBounds;
		std::optional<MSDFAtlasBounds> atlasBounds;
	};

	// フォント全体のメトリクス情報
	struct MSDFMetrics {

		float elementSize = 0.0f;
		float lineHeight = 0.0f;
		float ascender = 0.0f;
		float descender = 0.0f;
	};

	// フォントアセットの情報
	struct MSDFFontAsset {

		// アセットID
		AssetID guid{};
		// フォントの名前
		std::string name = "UnnamedMSDFFont";

		// 描画に使用するアトラステクスチャID
		AssetID atlasTexture{};

		// フォントのサイズ
		float pxRange = 0.0f;
		uint32_t atlasWidth = 0;
		uint32_t atlasHeight = 0;

		// フォント全体のメトリクス情報
		MSDFMetrics metrics{};
		// グリフのマップ、文字コードポイント->グリフ情報
		std::unordered_map<char32_t, MSDFGlyph> glyphMap{};
		// カーニングのマップ、右文字コードポイント->カーニング値
		std::unordered_map<uint64_t, float> kerningMap{};

		// 文字コードポイントからグリフ情報を取得する
		const MSDFGlyph* FindGlyph(char32_t codepoint) const;
		float GetKerning(char32_t left, char32_t right) const;
		Vector2 GetAtlasSize() const { return Vector2(static_cast<float>(atlasWidth), static_cast<float>(atlasHeight)); }
	};

	// json変換
	bool FromJson(const nlohmann::json& data, MSDFFontAsset& outAsset);
} // Engine