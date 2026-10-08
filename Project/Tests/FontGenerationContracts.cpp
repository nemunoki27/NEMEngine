#include "TestContracts.h"
#include "TestFixtures.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Importer/Font/MSDFAtlasGeneration.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFAtlasDocument.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFFontGenerator.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <fstream>
#include <iostream>
#include <vector>

// msdf-atlas-gen
#if defined(NEM_USE_MSDF_ATLAS_GEN)
#include <msdf-atlas-gen/msdf-atlas-gen.h>
// DirectXTex
#include <DirectXTex.h>
// Windows
#include <windows.h>
#endif

namespace {

#if defined(NEM_USE_MSDF_ATLAS_GEN)

	// 循環と深すぎるincludeを拒否し、通常の再利用を維持する
	bool CheckCharsetIncludes(const std::filesystem::path& root) {

		using namespace Engine;
		if (!StorageFileUtility::WriteBytes(root / "cycle.txt", "\"A\" @include \"cycle.txt\"") ||
			!StorageFileUtility::WriteBytes(root / "first.txt", "@include \"second.txt\"") ||
			!StorageFileUtility::WriteBytes(root / "second.txt", "@include \"first.txt\"")) {
			return false;
		}
		for (const char* file : {"cycle.txt", "first.txt"}) {
			msdf_atlas::Charset charset;
			if (charset.load((root / file).string().c_str())) {
				return false;
			}
		}
		// 同じ実ファイルへの別表記も循環として扱う
		std::filesystem::create_directory(root / "alias");
		if (!StorageFileUtility::WriteBytes(root / "cycle.txt", "@include \"alias/../cycle.txt\"")) {
			return false;
		}
		msdf_atlas::Charset alias;
		if (alias.load((root / "cycle.txt").string().c_str())) {
			return false;
		}
		// 64段までは許容し、65段を拒否する
		for (size_t index = 0; index <= 64; ++index) {
			const auto contents = index == 64 ? "\"A\"" : "@include \"level" + std::to_string(index + 1) + ".txt\"";
			if (!StorageFileUtility::WriteBytes(root / ("level" + std::to_string(index) + ".txt"), contents)) {
				return false;
			}
		}
		msdf_atlas::Charset deep;
		if (deep.load((root / "level0.txt").string().c_str()) ||
			!StorageFileUtility::WriteBytes(root / "level63.txt", "\"A\"")) {
			return false;
		}
		msdf_atlas::Charset boundary;
		if (!boundary.load((root / "level0.txt").string().c_str()) || boundary.size() != 1) {
			return false;
		}
		// 別枝から同じ文字集合を使う場合は拒否しない
		if (!StorageFileUtility::WriteBytes(root / "leaf.txt", "\"AV\"") ||
			!StorageFileUtility::WriteBytes(root / "repeated.txt", "@include \"leaf.txt\" @include \"leaf.txt\"")) {
			return false;
		}
		msdf_atlas::Charset repeated;
		return repeated.load((root / "repeated.txt").string().c_str()) && repeated.size() == 2;
	}

	// 配置情報を従来のライブラリ出力と照合する
	bool CheckAtlasDocument(const std::filesystem::path& source, const std::filesystem::path& root) {

		msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
		if (!freetype) {
			return false;
		}
		Engine::ScopedCleanup freetypeCleanup([freetype]() noexcept { msdfgen::deinitializeFreetype(freetype); });
		msdfgen::FontHandle* font = msdfgen::loadFont(freetype, source.string().c_str());
		if (!font) {
			return false;
		}
		Engine::ScopedCleanup fontCleanup([font]() noexcept { msdfgen::destroyFont(font); });
		msdf_atlas::Charset charset;
		charset.add('A');
		charset.add('V');
		charset.add(' ');
		std::vector<msdf_atlas::GlyphGeometry> glyphs;
		msdf_atlas::FontGeometry geometry(&glyphs);
		if (geometry.loadCharset(font, 1.0, charset, false, true) != 3) {
			return false;
		}
		geometry.setName("Fixture");
		msdf_atlas::TightAtlasPacker packer;
		packer.setDimensions(256, 256);
		packer.setScale(48);
		packer.setPixelRange(msdfgen::Range(8));
		if (packer.pack(glyphs.data(), static_cast<int>(glyphs.size())) != 0) {
			return false;
		}
		msdf_atlas::JsonAtlasMetrics metrics{};
		metrics.distanceRange = packer.getPixelRange();
		metrics.size = packer.getScale();
		packer.getDimensions(metrics.width, metrics.height);
		metrics.yDirection = msdfgen::Y_DOWNWARD;
		const auto reference = root / "reference.json";
		nlohmann::json expected;
		if (!msdf_atlas::exportJSON(&geometry, 1, msdf_atlas::ImageType::MSDF, metrics, reference.string().c_str(), true) ||
			!Engine::JsonFile::TryLoad(reference, expected)) {
			return false;
		}
		const auto actual = Engine::MSDFAtlasDocument::Build(geometry, packer);
		if (actual != expected) {
			std::cerr << "Font atlas document differs: " << nlohmann::json::diff(expected, actual).dump() << '\n';
			return false;
		}
		return true;
	}

	// 読込失敗時に部分結果を公開せず、失敗後も再生成できる
	bool CheckGenerationFailure(const std::filesystem::path& source, const std::filesystem::path& root) {

		using namespace Engine;
		const auto charset = root / "charset.txt";
		MSDFAtlasGeneration::Result output{{{"retained", 17}}, "previous"};
		const auto previous = output.document;
		for (const std::string contents : {"", "\"A\" @include \"missing.txt\"", "\"A\" @include \"charset.txt\"", "\"A", "0x10ffff"}) {
			std::ofstream(charset, std::ios::trunc) << contents;
			if (MSDFAtlasGeneration::Generate(source, charset, output) || output.document != previous ||
				output.atlasPNG != "previous") {
				return false;
			}
		}
		std::ofstream(charset, std::ios::trunc) << "\"AV?\"";
		if (MSDFAtlasGeneration::Generate(root / "missing.ttf", charset, output) || output.document != previous ||
			output.atlasPNG != "previous" || !MSDFAtlasGeneration::Generate(source, charset, output)) {
			return false;
		}
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		ScopedCleanup cleanup([initialized]() noexcept {
			if (SUCCEEDED(initialized)) {
				CoUninitialize();
			}
		});
		DirectX::TexMetadata metadata;
		DirectX::ScratchImage image;
		return output.document["glyphs"].size() == 3 &&
			   SUCCEEDED(DirectX::LoadFromWICMemory(
				   output.atlasPNG.data(), output.atlasPNG.size(), DirectX::WIC_FLAGS_NONE, &metadata, image)) &&
			   metadata.width == 2048 && metadata.height == 2048;
	}

	// 再生成の成功と失敗でFontとAtlasとmetaの対応を維持する
	bool CheckFontPublication(const std::filesystem::path& source) {

		using namespace Engine;
		NEMTests::TestDirectory directory("FontPublication", RuntimePaths::GetGameAssetsRoot());
		const auto root = directory.GetPath();
		const auto copied = root / "Fixture.ttf";
		std::filesystem::copy_file(source, copied);
		AssetDatabase database;
		database.Init();
		const auto created = MSDFFontGenerator::EnsureGenerated(database, copied, false);
		if (!created.success) {
			return false;
		}
		const auto fontPath = root / "Fixture_msdf.font.json";
		const auto atlasPath = root / "Fixture_msdf.png";
		nlohmann::json document;
		if (!JsonFile::TryLoad(fontPath, document) || document["atlasTexture"] != ToString(created.atlasAssetID) ||
			database.FindDependencies(created.fontAssetID) != std::vector<AssetID>{created.atlasAssetID} ||
			!database.UpdateImporterSettings(created.atlasAssetID, {{"maxSize", 512}}, 1)) {
			return false;
		}

		// 書込を失敗させる前に異なる旧画像を用意する
		const auto atlasRevision = StorageFileUtility::FileRevision(atlasPath);
		const auto atlasBytes = StorageFileUtility::ReadVerifiedBytes(atlasPath, atlasRevision);
		if (!StorageFileUtility::WriteBytes(atlasPath, atlasBytes + "retained")) {
			return false;
		}
		document["name"] = "Previous";
		if (!JsonFile::SaveCanonical(fontPath, document, 2)) {
			return false;
		}
		const std::array paths{fontPath, atlasPath, std::filesystem::path(fontPath.string() + ".meta"),
			std::filesystem::path(atlasPath.string() + ".meta")};
		std::array<std::string, 4> revisions;
		for (size_t index = 0; index < paths.size(); ++index) {
			revisions[index] = StorageFileUtility::FileRevision(paths[index]);
		}
		const auto contentRevision = database.GetContentRevision();
		{
			NEMTests::TestFileReadLock lock(fontPath);
			const auto failed = MSDFFontGenerator::EnsureGenerated(database, copied, true);
			if (failed.success || failed.message.empty() || database.GetContentRevision() != contentRevision) {
				return false;
			}
		}
		for (size_t index = 0; index < paths.size(); ++index) {
			if (StorageFileUtility::FileRevision(paths[index]) != revisions[index]) {
				return false;
			}
		}
		const auto retry = MSDFFontGenerator::EnsureGenerated(database, copied, true);
		if (!retry.success || retry.fontAssetID != created.fontAssetID || retry.atlasAssetID != created.atlasAssetID ||
			database.Find(created.atlasAssetID)->importerSettings["maxSize"] != 512 ||
			StorageFileUtility::ReadVerifiedBytes(atlasPath, StorageFileUtility::FileRevision(atlasPath)) != atlasBytes) {
			return false;
		}

		// 既存の未登録生成物をGUID付きで登録し直す
		AssetDatabase unindexed;
		unindexed.Init();
		const auto registered = MSDFFontGenerator::EnsureGenerated(unindexed, copied, false);
		if (!registered.success || registered.fontAssetID != created.fontAssetID ||
			registered.atlasAssetID != created.atlasAssetID) {
			return false;
		}
		const auto blocked = root / "Blocked.ttf";
		std::filesystem::copy_file(source, blocked);
		std::filesystem::create_directory(root / "Blocked_msdf.font.json");
		const auto rejected = MSDFFontGenerator::EnsureGenerated(database, blocked, true);
		return !rejected.success && !std::filesystem::exists(root / "Blocked_msdf.png") &&
			   !std::filesystem::exists(root / "Blocked_msdf.font.json.meta") && database.Find(created.fontAssetID);
	}
#endif
}

bool NEMTests::TestFontGenerationContracts() {

#if defined(NEM_USE_MSDF_ATLAS_GEN)
	const auto source = Engine::RuntimePaths::GetGameAssetsRoot() / "Fonts" / "Cp_period" / "cp_period.ttf";
	TestDirectory directory("FontGeneration");
	return RunTest("FontCharsetIncludes", [&] { return CheckCharsetIncludes(directory.GetPath()); }) &&
		   RunTest("FontAtlasDocument", [&] { return CheckAtlasDocument(source, directory.GetPath()); }) &&
		   RunTest("FontGenerationFailure", [&] { return CheckGenerationFailure(source, directory.GetPath()); }) &&
		   RunTest("FontPublication", [&] { return CheckFontPublication(source); });
#else
	Engine::MSDFAtlasGeneration::Result output{{{"retained", 17}}, "previous"};
	return !Engine::MSDFAtlasGeneration::Generate({}, {}, output) && output.document["retained"] == 17 &&
		   output.atlasPNG == "previous";
#endif
}
