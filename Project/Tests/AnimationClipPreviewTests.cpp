#include "AnimationClipPreviewTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditSession.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUtility.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <array>
#include <cmath>
#include <fstream>

bool NEMTests::TestAnimationClipPreview() {

	using namespace Engine;
	RegisterBuiltinAnimationProperties();
	TestDirectory directory("ClipPreview", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Motion.animclip.json";
	AnimationClipAsset clip;
	clip.autoDuration = false;
	clip.duration = 1.0f;
	AnimationCurveTrack track;
	track.binding = {"Transform", "localPos", AnimationValueType::Vector3};
	track.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
	track.channels[0].AddKey(0.0f, 0.0f);
	track.channels[0].AddKey(1.0f, 10.0f);
	clip.curveTracks.push_back(track);
	if (!SaveAnimationClipAsset(path, clip)) {
		return false;
	}
	AssetDatabase database;
	if (!database.Init()) {
		return false;
	}
	const AssetID clipID = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::AnimationClip);
	if (!clipID) {
		return false;
	}
	ECSWorld world;
	const Entity entity = world.CreateEntity();
	world.AddComponent<TransformComponent>(entity);
	world.GetComponent<TransformComponent>(entity).localPos = {3.0f, 4.0f, 5.0f};
	// 巨大な添字が先頭SubMeshへ巻き戻らない
	world.AddComponent<MeshRendererComponent>(entity);
	const std::array<SubMeshMaterial, 1> subMeshes{SubMeshMaterial{.name = "NamedMesh"}};
	SetMeshSubMeshes(world, entity, subMeshes);
	AnimationCurveTrack labelTrack;
	labelTrack.binding = {"MeshRenderer", "subMeshes[0].localPos", AnimationValueType::Vector3};
	if (AnimationClipEditorUtility::BuildTrackLabel(labelTrack, &world, entity) != "NamedMesh.localPos") {
		return false;
	}
	labelTrack.binding.propertyPath = "subMeshes[4294967296].localPos";
	if (AnimationClipEditorUtility::BuildTrackLabel(labelTrack, &world, entity) != labelTrack.binding.propertyPath) {
		return false;
	}
	EditorToolContext context;
	context.toolContext.world = &world;
	context.toolContext.assetDatabase = &database;
	AnimationClipEditSession session;
	session.GetClipAssetID() = clipID;
	session.LoadClipFromSelectedAsset(context);
	if (!session.GetHasClip()) {
		return false;
	}
	session.SetPreviewTarget(context, world.GetUUID(entity));
	session.GetPreviewTime() = 0.5f;
	session.ApplyPreviewAtCurrentTime(context, true);
	if (world.GetComponent<TransformComponent>(entity).localPos != Vector3(5.0f, 4.0f, 5.0f)) {
		return false;
	}
	// ImGuiに依存せず指定した時間でプレビューを進める
	session.TogglePreviewPlayback(context);
	session.UpdatePreviewPlayback(context, 0.25f);
	if (session.GetPreviewTime() != 0.75f ||
		world.GetComponent<TransformComponent>(entity).localPos != Vector3(7.5f, 4.0f, 5.0f)) {
		return false;
	}
	context.toolContext.deltaTime = 0.1f;
	session.UpdatePreviewPlayback(context, 0.0f);
	if (std::abs(session.GetPreviewTime() - 0.85f) > 0.00001f) {
		return false;
	}
	session.TogglePreviewPlayback(context);
	// 読込失敗で未保存のClipとPreviewを失わない
	session.GetClip().name = "Draft";
	session.MarkClipDirty();
	std::ofstream(path) << "{";
	session.LoadClipFromSelectedAsset(context);
	if (!session.GetClipDirty() || session.GetClip().name != "Draft" || !session.GetPreviewActive()) {
		return false;
	}
	ECSWorld replacement;
	context.toolContext.world = &replacement;
	session.UpdateExternalEdits(context);
	if (session.GetPreviewActive() || world.GetComponent<TransformComponent>(entity).localPos != Vector3(3.0f, 4.0f, 5.0f)) {
		return false;
	}
	// 保存の成功後にdirtyを解除する
	session.SaveClipToSelectedAsset(context);
	AnimationClipAsset saved;
	if (session.GetClipDirty() || !LoadAnimationClipAsset(path, saved) || saved.name != "Draft") {
		return false;
	}
	// 所有Worldが先に破棄されても借用しない
	auto temporary = std::make_unique<ECSWorld>();
	const Entity temporaryEntity = temporary->CreateEntity();
	temporary->AddComponent<TransformComponent>(temporaryEntity);
	context.toolContext.world = temporary.get();
	session.SetPreviewTarget(context, temporary->GetUUID(temporaryEntity));
	session.ApplyPreviewAtCurrentTime(context, true);
	temporary.reset();
	session.EndPreviewAndRestore();
	return !session.GetPreviewActive();
}
