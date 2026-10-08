#include "MSDFAtlasDocument.h"

//============================================================================
//	include
//============================================================================
// c++
#include <utility>

// msdf-atlas-gen
#if defined(NEM_USE_MSDF_ATLAS_GEN)
#include <msdf-atlas-gen/msdf-atlas-gen.h>

//============================================================================
//	MSDFAtlasDocument namespaceMethods
//============================================================================
nlohmann::json Engine::MSDFAtlasDocument::Build(
	const msdf_atlas::FontGeometry& font, const msdf_atlas::TightAtlasPacker& packer) {

	// Atlasの範囲とFontの基準寸法を保存する
	int width = 0, height = 0;
	packer.getDimensions(width, height);
	const auto range = packer.getPixelRange();
	const auto& metrics = font.getMetrics();
	nlohmann::json document = {
		{"atlas", {{"type", "msdf"}, {"distanceRange", range.upper - range.lower},
					  {"distanceRangeMiddle", 0.5 * (range.lower + range.upper)}, {"size", packer.getScale()}, {"width", width},
					  {"height", height}, {"yOrigin", "top"}}},
		{"metrics", {{"emSize", metrics.emSize}, {"lineHeight", metrics.lineHeight}, {"ascender", -metrics.ascenderY},
						{"descender", -metrics.descenderY}, {"underlineY", -metrics.underlineY},
						{"underlineThickness", metrics.underlineThickness}}},
		{"glyphs", nlohmann::json::array()}, {"kerning", nlohmann::json::array()}};
	if (const char* name = font.getName()) {
		document["name"] = name;
	}

	// 平面座標とAtlas座標を上原点へ揃える
	const bool unicode = font.getPreferredIdentifierType() == msdf_atlas::GlyphIdentifierType::UNICODE_CODEPOINT;
	for (const auto& glyph : font.getGlyphs()) {
		nlohmann::json data = {{"advance", glyph.getAdvance()}};
		if (unicode) {
			data["unicode"] = glyph.getCodepoint();
		} else {
			data["index"] = glyph.getIndex();
		}
		double left, bottom, right, top;
		glyph.getQuadPlaneBounds(left, bottom, right, top);
		if (left || bottom || right || top) {
			data["planeBounds"] = {{"left", left}, {"top", -top}, {"right", right}, {"bottom", -bottom}};
		}
		glyph.getQuadAtlasBounds(left, bottom, right, top);
		if (left || bottom || right || top) {
			data["atlasBounds"] = {{"left", left}, {"top", height - top}, {"right", right}, {"bottom", height - bottom}};
		}
		document["glyphs"].push_back(std::move(data));
	}

	// 保存済みの文字同士の字間だけを保持する
	for (const auto& [indices, advance] : font.getKerning()) {
		if (!unicode) {
			document["kerning"].push_back({{"index1", indices.first}, {"index2", indices.second}, {"advance", advance}});
			continue;
		}
		const auto* left = font.getGlyph(msdfgen::GlyphIndex(indices.first));
		const auto* right = font.getGlyph(msdfgen::GlyphIndex(indices.second));
		if (left && right && left->getCodepoint() && right->getCodepoint()) {
			document["kerning"].push_back(
				{{"unicode1", left->getCodepoint()}, {"unicode2", right->getCodepoint()}, {"advance", advance}});
		}
	}
	return document;
}
#endif
