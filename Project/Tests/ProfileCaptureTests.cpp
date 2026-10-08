#include "ProfileCaptureTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/ProfileCapture.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Profiling/ProfileInputSnapshot.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

bool NEMTests::TestProfileCapture() {

	using namespace Engine;
	TestDirectory directory("ProfileCapture", RuntimePaths::GetSavedPath("Tests"));
	ProfileCapture capture;
	if (capture.Start(0) || capture.Start(36001) || !capture.Start(2) || capture.Start(1)) { return false; }
	const nlohmann::json conditions{ { "width", 1280 }, { "meshShader", true } };
	// GPU結果が後から届いてもCPUフレームを取り違えない
	capture.Append(42, { { "conditions", conditions }, { "cpuMilliseconds", { 1.0f, 2.0f } } });
	capture.Append(43, { { "conditions", conditions }, { "cpuMilliseconds", { 3.0f, 4.0f } } });
	if (capture.IsRecording() || capture.GetSnapshot()["stopReason"] != "frame_limit") { return false; }
	capture.Stop();
	if (capture.GetSnapshot()["stopReason"] != "frame_limit") { return false; }
	capture.AttachGPU(42, { { { "viewID", "Game/CameraA" }, { "name", "Draw" }, { "milliseconds", 5.0f } } }, "complete");
	capture.AttachGPU(100, nlohmann::json::array(), "missing");
	const auto snapshot = capture.GetSnapshot();
	if (snapshot["frames"].size() != 2 || snapshot["frames"][0]["gpuStatus"] != "complete" ||
		snapshot["frames"][1]["gpuStatus"] != "pending") { return false; }
	const auto path = directory.GetPath() / "record.profile.json";
	nlohmann::json loaded;
	if (!capture.Save(path) || !ProfileCapture::Load(path, loaded) || loaded != snapshot ||
		!ProfileCapture::HasSameConditions(snapshot, loaded)) { return false; }
	loaded["frames"][1]["conditions"]["meshShader"] = false;
	if (ProfileCapture::HasSameConditions(snapshot, loaded)) { return false; }
	// 入力の取得失敗や不明なDriverは同条件と扱わない
	auto unavailable = snapshot;
	for (auto& frame : unavailable["frames"]) { frame["conditions"]["inputSnapshotComplete"] = false; }
	if (ProfileCapture::HasSameConditions(unavailable, unavailable)) { return false; }
	for (auto& frame : unavailable["frames"]) {
		frame["conditions"]["inputSnapshotComplete"] = true;
		frame["conditions"]["driverVersion"] = "unavailable";
	}
	if (ProfileCapture::HasSameConditions(unavailable, unavailable)) { return false; }
	// 破損した記録は以前の読み込み結果を置き換えない
	const auto previous = loaded;
	// 数値破損と不明な取得状態も確定結果を置き換えない
	auto invalid = snapshot;
	invalid["frames"][0]["gpuPasses"][0]["milliseconds"] = -1.0;
	if (!JsonFile::Save(path, invalid) || ProfileCapture::Load(path, loaded) || loaded != previous) { return false; }
	invalid = snapshot;
	invalid["stopReason"] = "unknown";
	if (!JsonFile::Save(path, invalid) || ProfileCapture::Load(path, loaded) || loaded != previous) { return false; }
	invalid = snapshot;
	invalid["frames"][0]["gpuStatus"] = "unknown";
	if (!JsonFile::Save(path, invalid) || ProfileCapture::Load(path, loaded) || loaded != previous) { return false; }
	invalid = snapshot;
	invalid["frames"][0]["cpuMilliseconds"] = { "invalid" };
	if (!JsonFile::Save(path, invalid) || ProfileCapture::Load(path, loaded) || loaded != previous) { return false; }
	if (!JsonFile::Save(path, { { "schemaVersion", 1 }, { "frames", { { { "frameID", "invalid" } } } } }) ||
		ProfileCapture::Load(path, loaded) || loaded != previous) { return false; }
	// Worldの取得はdirty通知や保存用補正を行わない
	ECSWorld world;
	const Entity entity = world.CreateEntity();
	world.AddComponent<TransformComponent>(entity);
	const uint64_t worldRevision = world.GetDataRevision();
	const auto firstInput = ProfileInputSnapshotBuilder::CaptureWorld(world, nullptr);
	const auto sameInput = ProfileInputSnapshotBuilder::CaptureWorld(world, nullptr);
	if (!firstInput.complete || firstInput.entityCount != 1 || firstInput.worldSHA256 != sameInput.worldSHA256 ||
		world.GetDataRevision() != worldRevision) { return false; }
	world.GetComponent<TransformComponent>(entity).localPos.x = 7.0f;
	const auto changedInput = ProfileInputSnapshotBuilder::CaptureWorld(world, nullptr);
	if (!changedInput.complete || firstInput.worldSHA256 == changedInput.worldSHA256) { return false; }
	// 内容の変更は全件走査せず索引の更新番号で検出する
	const AssetID noteID = AssetGUID::New();
	const auto notePath = directory.GetPath() / "input.json";
	if (!JsonFile::Save(notePath, nlohmann::json::object()) ||
		!AssetDatabase::WriteMetaFile(notePath.string() + ".meta", AssetMeta{ .guid = noteID, .type = AssetType::DefaultAsset,
			.importer = "DefaultAssetImporter" })) { return false; }
	AssetDatabase database;
	if (!database.RebuildMeta({ directory.GetPath() }) || !database.Find(noteID)) { return false; }
	ProfileInputSnapshot revisionSnapshot;
	revisionSnapshot.assetStructureRevision = database.GetStructureRevision();
	revisionSnapshot.assetContentRevision = database.GetContentRevision();
	database.NotifyContentChanged(AssetGUID::New());
	if (!ProfileInputSnapshotBuilder::HasSameAssetRevisions(database, revisionSnapshot)) { return false; }
	database.NotifyContentChanged(noteID);
	if (ProfileInputSnapshotBuilder::HasSameAssetRevisions(database, revisionSnapshot) ||
		database.GetContentRevision(noteID) != 1) { return false; }

	// 入力準備の時間は記録せず次の完全なframeから始める
	auto& profiler = FrameProfiler::GetInstance();
	profiler.SetEnabled(true);
	profiler.SetConditions(conditions.dump());
	profiler.GetCapture().Stop();
	if (!profiler.StartCapture(1)) { return false; }
	profiler.BeginFrame(0.016f, 1.0f);
	profiler.SkipCaptureFrame();
	profiler.AddSample(FrameProfiler::Category::Draw, 900.0f);
	profiler.BeginFrame(0.016f, 1.016f);
	if (!profiler.GetCapture().GetSnapshot()["frames"].empty()) { return false; }
	profiler.AddSample(FrameProfiler::Category::Draw, 2.0f);
	const uint64_t recordedFrame = profiler.GetFrameID();
	profiler.BeginFrame(0.016f, 1.032f);
	const auto& recorded = profiler.GetCapture().GetSnapshot()["frames"];
	if (profiler.IsCaptureRecording() || recorded.size() != 1 || recorded[0]["frameID"] != recordedFrame ||
		recorded[0]["cpuMilliseconds"][static_cast<size_t>(FrameProfiler::Category::Draw)] != 2.0f) { return false; }
	if (!profiler.StartCapture(2)) { return false; }
	profiler.SetEnabled(false);
	const bool stopped = !profiler.IsCaptureRecording() &&
		profiler.GetCapture().GetSnapshot()["stopReason"] == "profiling_disabled";
	profiler.SetEnabled(true);
	profiler.SetConditions("{}");
	return stopped;
}
