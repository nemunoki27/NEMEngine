#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderCameraHistory.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <cstring>

namespace {

	bool TestRenderViewReferenceSize() {

		using namespace Engine;
		const auto sameMatrix = [](const Matrix4x4& lhs, const Matrix4x4& rhs) {
			return std::memcmp(lhs.m, rhs.m, sizeof(lhs.m)) == 0;
		};
		const auto sameCameras = [&](const ResolvedRenderView& lhs, const ResolvedRenderView& rhs) {
			return sameMatrix(lhs.orthographic.matrices.viewProjectionMatrix, rhs.orthographic.matrices.viewProjectionMatrix) &&
				sameMatrix(lhs.perspective.matrices.viewProjectionMatrix, rhs.perspective.matrices.viewProjectionMatrix) &&
				sameMatrix(lhs.screen.matrices.viewProjectionMatrix, rhs.screen.matrices.viewProjectionMatrix);
		};
		ECSWorld world;
		RenderViewRequest request{};
		request.kind = RenderViewKind::Scene;
		request.sourceKind = RenderViewSourceKind::ManualCamera;
		request.width = 1280;
		request.height = 720;
		const auto full = RenderViewResolver::Resolve(request, world);
		request.referenceWidth = request.width;
		request.referenceHeight = request.height;
		// 端数を含む実解像度でも2D/ScreenUIの座標範囲と3D画角を維持する
		request.width = 641;
		request.height = 359;
		const auto reduced = RenderViewResolver::Resolve(request, world);
		if (!full.valid || !reduced.valid || reduced.width != 641 || reduced.height != 359 ||
			reduced.outputWidth != 641 || reduced.outputHeight != 359 ||
			reduced.GetReferenceWidth() != 1280 || reduced.GetReferenceHeight() != 720 ||
			reduced.aspectRatio != full.aspectRatio || !sameCameras(full, reduced)) return false;
		request.enabled = false;
		if (RenderViewResolver::Resolve(request, world).valid) return false;
		request.enabled = true;
		const Entity camera = world.CreateEntity();
		world.AddComponent<TransformComponent>(camera);
		world.AddComponent<PerspectiveCameraComponent>(camera);
		const Entity camera2D = world.CreateEntity();
		world.AddComponent<TransformComponent>(camera2D);
		world.AddComponent<OrthographicCameraComponent>(camera2D);
		request.kind = RenderViewKind::Game;
		request.sourceKind = RenderViewSourceKind::WorldCamera;
		request.width = 1280;
		request.height = 720;
		const auto fullWorld = RenderViewResolver::Resolve(request, world);
		request.width = 640;
		request.height = 360;
		const auto reducedWorld = RenderViewResolver::Resolve(request, world);
		if (!sameCameras(fullWorld, reducedWorld)) return false;
		world.GetComponent<PerspectiveCameraComponent>(camera).common.viewportWidth = 0.5f;
		const Entity rightCamera = world.CreateEntity();
		world.AddComponent<TransformComponent>(rightCamera);
		auto& rightSettings = world.AddComponent<PerspectiveCameraComponent>(rightCamera);
		rightSettings.common.viewportX = 0.5f;
		rightSettings.common.viewportWidth = 0.5f;
		request.width = 1280;
		request.height = 720;
		const auto fullSplit = RenderViewResolver::ResolveGameCameraViews(request, world);
		request.width = 640;
		request.height = 360;
		const auto reducedSplit = RenderViewResolver::ResolveGameCameraViews(request, world);
		if (fullSplit.size() != 2 || reducedSplit.size() != 2) return false;
		for (size_t index = 0; index < fullSplit.size(); ++index) {
			if (!sameCameras(fullSplit[index], reducedSplit[index]) ||
				reducedSplit[index].width != 320 || reducedSplit[index].height != 360 ||
				reducedSplit[index].referenceWidth != 640 || reducedSplit[index].referenceHeight != 720 ||
				reducedSplit[index].outputX * 2 != fullSplit[index].outputX) return false;
		}
		// RenderTextureはView表示サイズに依存せずAsset指定の出力と投影に戻す
		auto fullTexture = fullSplit.back();
		auto reducedTexture = reducedSplit.back();
		fullTexture.targetTexture = reducedTexture.targetTexture = AssetID{43, 47};
		RenderViewResolver::ResolveRenderTextureOutput(fullTexture, world, 512, 512);
		RenderViewResolver::ResolveRenderTextureOutput(reducedTexture, world, 512, 512);
		if (!sameCameras(fullTexture, reducedTexture) || reducedTexture.width != 256 || reducedTexture.height != 512 ||
			reducedTexture.referenceWidth != 256 || reducedTexture.referenceHeight != 512 || reducedTexture.outputX != 256) return false;
		RenderViewResolver::ResolveRenderTextureOutput(reducedTexture, world, 0, 512);
		return !reducedTexture.valid;
	}
}

namespace NEMTests {

	bool TestRenderCameraHistory() {

		using namespace Engine;
		if (!TestRenderViewReferenceSize()) return false;
		ResolvedRenderView left{};
		left.valid = true;
		left.width = 640;
		left.height = 720;
		left.historyWorldRevision = 1;
		left.perspective.valid = true;
		left.perspective.sourceCamera = Entity{1, 1};
		// 編集画面も描画と同じCameraの設定を選ぶ
		left.perspective.renderPasses = AssetID{101, 102};
		left.orthographic.valid = true;
		left.orthographic.renderPasses = AssetID{103, 104};
		if (left.FindOutputCamera() != &left.perspective) {
			return false;
		}
		left.perspective.valid = false;
		if (left.FindOutputCamera() != &left.orthographic) {
			return false;
		}
		left.orthographic.valid = false;
		if (left.FindOutputCamera()) {
			return false;
		}
		left.perspective.valid = true;
		ResolvedRenderView right = left;
		right.perspective.sourceCamera = Entity{2, 1};
		RenderCameraHistory history{};
		const auto update = [&](ResolvedRenderView& view, float position, uint64_t frame) {
			view.perspective.matrices.unjitteredViewProjectionMatrix.m[3][0] = position;
			view.perspective.matrices.viewProjectionMatrix.m[3][0] = position + 0.25f;
			return history.Update(view, RenderCameraDomain::Perspective, frame).m[3][0];
		};

		// 描画順が変わってもCameraごとにJitterを除いた履歴を保持する
		if (left.GetHistoryKey() == right.GetHistoryKey() || update(left, 1.0f, 1) != 1.0f ||
			update(right, 10.0f, 1) != 10.0f || update(right, 20.0f, 2) != 10.0f || update(left, 2.0f, 2) != 1.0f ||
			update(left, 2.0f, 2) != 1.0f) {
			return false;
		}
		// 同じフレームの出力解像度Overlayは履歴と内部解像度を上書きしない
		ResolvedRenderView overlay = right;
		overlay.width = 1280;
		overlay.height = 1440;
		if (update(overlay, 200.0f, 2) != 10.0f) {
			return false;
		}
		// サイズ変更は対象Cameraの履歴だけを初期化する
		left.width = 320;
		if (update(left, 3.0f, 3) != 3.0f || update(right, 30.0f, 3) != 20.0f) {
			return false;
		}
		// 実解像度が同じでも論理サイズの変更では古い投影を引き継がない
		left.referenceWidth = 1280;
		left.referenceHeight = 720;
		if (update(left, 4.0f, 4) != 4.0f) return false;
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
