#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderCameraHistory.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

namespace NEMTests {

	bool TestRenderCameraHistory() {

		using namespace Engine;
		ResolvedRenderView left{};
		left.valid = true;
		left.width = 640;
		left.height = 720;
		left.historyWorldRevision = 1;
		left.perspective.valid = true;
		left.perspective.sourceCamera = Entity{1, 1};
		ResolvedRenderView right = left;
		right.perspective.sourceCamera = Entity{2, 1};
		RenderCameraHistory history{};
		const auto update = [&](ResolvedRenderView& view, float position, uint64_t frame) {
			view.perspective.matrices.viewProjectionMatrix.m[3][0] = position;
			return history.Update(view, RenderCameraDomain::Perspective, frame).m[3][0];
		};

		// 描画順が変わっても別Cameraの履歴を参照しない
		if (left.GetHistoryKey() == right.GetHistoryKey() || update(left, 1.0f, 1) != 1.0f ||
			update(right, 10.0f, 1) != 10.0f || update(right, 20.0f, 2) != 10.0f || update(left, 2.0f, 2) != 1.0f ||
			update(left, 2.0f, 2) != 1.0f) {
			return false;
		}
		// サイズ変更は対象Cameraの履歴だけを初期化する
		left.width = 320;
		if (update(left, 3.0f, 3) != 3.0f || update(right, 30.0f, 3) != 20.0f) {
			return false;
		}
		const std::string original = right.GetHistoryKey();
		++right.perspective.sourceCamera.generation;
		if (right.GetHistoryKey() == original || update(right, 40.0f, 4) != 40.0f) {
			return false;
		}
		// World切替と出力先変更は旧履歴を引き継がない
		++right.historyWorldRevision;
		if (update(right, 50.0f, 5) != 50.0f) {
			return false;
		}
		right.targetTexture = AssetID{10, 20};
		if (update(right, 60.0f, 6) != 60.0f) {
			return false;
		}
		right.kind = RenderViewKind::Scene;
		if (update(right, 70.0f, 7) != 70.0f) {
			return false;
		}
		if (update(right, 80.0f, 9) != 80.0f) {
			return false;
		}
		history.Clear();
		if (update(right, 90.0f, 10) != 90.0f) {
			return false;
		}

		// 別Assetの編集ではCameraの実行計画を失効させない
		RenderAssetLibrary library{};
		RenderPassesAsset first{};
		first.guid = AssetID{1, 2};
		RenderPassesAsset second{};
		second.guid = AssetID{3, 4};
		library.RegisterPreviewRenderPasses(first);
		library.RegisterPreviewRenderPasses(second);
		const uint64_t secondRevision = library.GetRenderPassesRevision(second.guid);
		library.RegisterPreviewRenderPasses(first);
		library.InvalidateRenderPasses(first.guid);
		library.DiscardPreviewRenderPasses(first.guid);
		if (library.GetRenderPassesRevision(second.guid) != secondRevision) {
			return false;
		}
		library.Clear();
		if (library.GetRenderPassesRevision(second.guid) == secondRevision) {
			return false;
		}

		// 同じ内容でも旧形式のファイルは読み込まない
		TestDirectory directory("RenderPassesFormat");
		RenderFeatureProfileAsset profile{};
		const auto currentPath = directory.GetPath() / "settings.renderpasses.json";
		if (!RenderFeatureProfileSerializer::Save(currentPath, profile) ||
			!RenderFeatureProfileSerializer::Load(currentPath, profile)) {
			return false;
		}
		const auto saved = RenderFeatureProfileSerializer::ToJson(profile);
		// 保存できない場合は旧ファイルを維持して失敗を返す
		{
			TestFileReadLock lock(currentPath);
			nlohmann::json retained;
			RenderFeatureProfileAsset changed = profile;
			changed.name = "Changed";
			if (RenderFeatureProfileSerializer::Save(currentPath, changed) || !JsonFile::TryLoad(currentPath, retained) ||
				retained != saved) {
				return false;
			}
		}
		const auto blockedPath = directory.GetPath() / "blocked.renderpasses.json";
		std::filesystem::create_directory(blockedPath);
		if (RenderFeatureProfileSerializer::Save(blockedPath, profile)) {
			return false;
		}
		// 不正な設定を読んでも確定済みの設定を変更しない
		const auto invalidPath = directory.GetPath() / "invalid.renderpasses.json";
		const nlohmann::json invalid{{"name", "Changed"}, {"colorPipeline", {{"exposure", {{"manualEV100", "invalid"}}}}}};
		if (!JsonFile::Save(invalidPath, invalid) || RenderFeatureProfileSerializer::Load(invalidPath, profile) ||
			RenderFeatureProfileSerializer::ToJson(profile) != saved || Engine::FromJson(invalid, profile)) {
			return false;
		}
		// 日本語のファイル名も同じ形式として扱う
		const auto unicodePath = directory.GetPath() / std::filesystem::path(u8"設定.renderpasses.json");
		if (!RenderFeatureProfileSerializer::Save(unicodePath, profile) ||
			!RenderFeatureProfileSerializer::Load(unicodePath, profile)) {
			return false;
		}
		for (const char* suffix : {".renderextension.json", ".renderfeatures.json", ".volumeprofile.json"}) {

			const auto oldPath = directory.GetPath() / (std::string("settings") + suffix);
			std::filesystem::copy_file(currentPath, oldPath);
			if (RenderFeatureProfileSerializer::Load(oldPath, profile) ||
				RenderFeatureProfileSerializer::Save(oldPath, profile)) {
				return false;
			}
		}
		return true;
	}
} // NEMTests
