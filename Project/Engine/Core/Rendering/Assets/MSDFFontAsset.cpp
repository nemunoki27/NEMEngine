#include "MSDFFontAsset.h"

//============================================================================
//	MSDFFontAsset classMethods
//============================================================================

namespace {

	uint64_t MakeKerningKey(char32_t left, char32_t right) {
		return (uint64_t(uint32_t(left)) << 32) | uint64_t(uint32_t(right));
	}
}

const Engine::MSDFGlyph* Engine::MSDFFontAsset::FindGlyph(char32_t codepoint) const {

	auto it = glyphMap.find(codepoint);
	if (it != glyphMap.end()) {
		return &it->second;
	}
	auto fallback = glyphMap.find(U'?');
	if (fallback != glyphMap.end()) {
		return &fallback->second;
	}
	return nullptr;
}

float Engine::MSDFFontAsset::GetKerning(char32_t left, char32_t right) const {

	auto it = kerningMap.find(MakeKerningKey(left, right));
	if (it != kerningMap.end()) {
		return it->second;
	}
	return 0.0f;
}

bool Engine::FromJson(const nlohmann::json& data, MSDFFontAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = MSDFFontAsset{};
	outAsset.guid = ParseAssetID(data, "guid");
	outAsset.name = data.value("name", outAsset.name);
	outAsset.atlasTexture = ParseAssetID(data, "atlasTexture");

	if (!data.contains("atlas") || !data.contains("metrics") || !data.contains("glyphs")) {
		return false;
	}

	const auto& atlas = data["atlas"];
	outAsset.pxRange = atlas.value("distanceRange", atlas.value("pxRange", 8.0f));
	outAsset.atlasWidth = atlas.value("width", 0u);
	outAsset.atlasHeight = atlas.value("height", 0u);

	const auto& metrics = data["metrics"];
	outAsset.metrics.elementSize = metrics.value("elementSize", metrics.value("emSize", 0.0f));
	outAsset.metrics.lineHeight = metrics.value("lineHeight", 0.0f);
	outAsset.metrics.ascender = metrics.value("ascender", 0.0f);
	outAsset.metrics.descender = metrics.value("descender", 0.0f);

	outAsset.glyphMap.clear();
	for (const auto& glyphJson : data["glyphs"]) {

		MSDFGlyph glyph{};

		if (glyphJson.contains("unicode")) {
			glyph.codepoint = char32_t(glyphJson["unicode"].get<uint32_t>());
		} else if (glyphJson.contains("codepoint")) {
			glyph.codepoint = char32_t(glyphJson["codepoint"].get<uint32_t>());
		} else {
			continue;
		}

		glyph.advance = glyphJson.value("advance", 0.0f);

		if (glyphJson.contains("planeBounds") && !glyphJson["planeBounds"].is_null()) {
			const auto& pb = glyphJson["planeBounds"];
			glyph.planeBounds = MSDFPlaneBounds{
				.left = pb.value("left", 0.0f),
				.bottom = pb.value("bottom", 0.0f),
				.right = pb.value("right", 0.0f),
				.top = pb.value("top", 0.0f),
			};
		}

		if (glyphJson.contains("atlasBounds") && !glyphJson["atlasBounds"].is_null()) {
			const auto& ab = glyphJson["atlasBounds"];
			glyph.atlasBounds = MSDFAtlasBounds{
				.left = static_cast<int32_t>(std::lround(ab.value("left", 0.0f))),
				.bottom = static_cast<int32_t>(std::lround(ab.value("bottom", 0.0f))),
				.right = static_cast<int32_t>(std::lround(ab.value("right", 0.0f))),
				.top = static_cast<int32_t>(std::lround(ab.value("top", 0.0f))),
			};
		}

		outAsset.glyphMap[glyph.codepoint] = glyph;
	}

	outAsset.kerningMap.clear();
	if (data.contains("kerning") && data["kerning"].is_array()) {
		for (const auto& kerningJson : data["kerning"]) {
			char32_t u1 = char32_t(kerningJson.value("unicode1", 0u));
			char32_t u2 = char32_t(kerningJson.value("unicode2", 0u));
			float adv = kerningJson.value("advance", 0.0f);
			outAsset.kerningMap[MakeKerningKey(u1, u2)] = adv;
		}
	}

	return outAsset.atlasWidth > 0 && outAsset.atlasHeight > 0 && outAsset.pxRange > 0.0f;
}