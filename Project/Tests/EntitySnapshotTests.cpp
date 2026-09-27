#include "EntitySnapshotTests.h"
#include "EntitySnapshotPlacementTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotDuplicator.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotBatchDuplicator.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <stdexcept>

namespace {

	bool TestCapturedReferenceTargets() {

		using namespace Engine;
		ECSWorld world;
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		const Entity ambiguous = SceneAuthoring::CreateGameObject(world, "Ambiguous");
		const AssetID firstAsset{ 1, 2 }, secondAsset{ 3, 4 };
		const std::vector<Entity> entities{ first, second, ambiguous };
		for (size_t index = 0; index < entities.size(); ++index) {
			auto& membership = world.GetComponent<SceneObjectComponent>(entities[index]);
			membership.sceneInstanceID = Engine::UUID{ index + 1 };
			membership.sourceAsset = index == 0 ? firstAsset : secondAsset;
			membership.localFileID = Engine::UUID{ index == 0 ? 100ull : 200ull };
		}
		std::vector<EntityTreeSnapshot> sources(2);
		EntitySnapshotUtility::CaptureSubtree(world, first, sources[0]);
		EntitySnapshotUtility::CaptureSubtree(world, second, sources[1]);
		sources[0].entities[0].components["Script"] = nlohmann::json::array({ { { "serializedFields", {
			{ "reference", { { "kind", "Scene" }, { "sourceAsset", ToString(secondAsset) },
				{ "localFileId", ToString(Engine::UUID{ 200 }) } } }
		} } } });
		EntitySnapshotUtility::CaptureReferenceTargets(world, sources[0]);
		auto result = EntitySnapshotBatchDuplicator::Build(sources);
		// 複製範囲外にも同じAssetと番号があれば勝手に補わない
		if (result[0].entities[0].components["Script"][0]["serializedFields"]["reference"]["localFileId"] !=
			ToString(Engine::UUID{ 200 }) || world.IsAlive(SceneObjectUtility::FindByLocalFileID(world, Engine::UUID{ 200 }))) {
			return false;
		}
		world.DestroyEntity(ambiguous);
		world.FlushPendingDestroyEntities();
		EntitySnapshotUtility::CaptureReferenceTargets(world, sources[0]);
		result = EntitySnapshotBatchDuplicator::Build(sources);
		const auto& replacement = result[1].entities[0].components["SceneObject"]["localFileId"];
		if (result[0].entities[0].components["Script"][0]["serializedFields"]["reference"]["localFileId"] != replacement) {
			return false;
		}
		// コピー後に元を削除しても取得時の対応で複製する
		world.DestroyEntity(second);
		world.FlushPendingDestroyEntities();
		const auto repeated = EntitySnapshotBatchDuplicator::Build(result);
		return repeated[0].entities[0].components["Script"][0]["serializedFields"]["reference"]["localFileId"] ==
			repeated[1].entities[0].components["SceneObject"]["localFileId"];
	}

	bool TestBatchDuplication() {

		using namespace Engine;
		std::vector<EntityTreeSnapshot> sources(3);
		for (size_t index = 0; index < sources.size(); ++index) {
			auto& source = sources[index];
			source.rootStableUUID = Engine::UUID{ index + 1 };
			source.ownerSceneInstanceID = Engine::UUID{ index == 2 ? 20ull : 10ull };
			source.ownerSourceAsset = AssetID{ 1, 2 };
			SerializedEntitySnapshot entity;
			entity.stableUUID = source.rootStableUUID;
			entity.sceneInstanceID = source.ownerSceneInstanceID;
			entity.sourceAsset = source.ownerSourceAsset;
			entity.components = {
				{ "Name", { { "name", "Original" } } },
				{ "SceneObject", { { "localFileId", ToString(Engine::UUID{ index == 1 ? 200ull : 100ull }) } } },
				{ "CameraController", { { "follow", { { "target", ToString(Engine::UUID{ 200 }) } } } } },
				{ "Script", nlohmann::json::array({ { { "serializedFields", {
					{ "reference", { { "kind", "Scene" }, { "sourceAsset", ToString(source.ownerSourceAsset) },
						{ "localFileId", ToString(Engine::UUID{ 200 }) } } }
				} } } }) }
			};
			source.entities.emplace_back(std::move(entity));
		}
		const auto duplicates = EntitySnapshotBatchDuplicator::Build(sources);
		const auto& target = duplicates[1].entities[0].components["SceneObject"]["localFileId"];
		// 同じScene内の別階層だけを新しい対象へ接続する
		if (duplicates[0].entities[0].components["CameraController"]["follow"]["target"] != target ||
			duplicates[0].entities[0].components["Script"][0]["serializedFields"]["reference"]["localFileId"] != target ||
			duplicates[2].entities[0].components["CameraController"]["follow"]["target"] != ToString(Engine::UUID{ 200 }) ||
			duplicates[0].entities[0].components["Name"]["name"] != "Original") {
			return false;
		}
		sources.push_back(sources.front());
		try {
			EntitySnapshotBatchDuplicator::Build(sources);
			return false;
		} catch (const std::invalid_argument&) {
			return true;
		}
	}
}

bool NEMTests::TestEntitySnapshotDuplication() {

	if (!TestBatchDuplication() || !TestCapturedReferenceTargets() || !TestEntitySnapshotPlacement()) {
		return false;
	}
	using namespace Engine;
	const AssetID scene{ 1, 2 };
	const AssetID otherScene{ 3, 4 };
	const auto reference = [](AssetID asset, Engine::UUID localID) {
		return nlohmann::json{
			{ "kind", "Scene" }, { "sourceAsset", ToString(asset) }, { "localFileId", ToString(localID) }
		};
	};
	EntityTreeSnapshot source;
	source.rootStableUUID = Engine::UUID{ 1 };
	source.ownerSceneInstanceID = Engine::UUID{ 50 };
	source.ownerSourceAsset = scene;
	SerializedEntitySnapshot root;
	root.stableUUID = source.rootStableUUID;
	root.sceneInstanceID = source.ownerSceneInstanceID;
	root.sourceAsset = scene;
	root.components = {
		{ "SceneObject", { { "localFileId", ToString(Engine::UUID{ 101 }) } } },
		{ "Hierarchy", { { "parentLocalFileID", ToString(Engine::UUID{ 200 }) } } },
		{ "CameraController", {
			{ "follow", { { "target", ToString(Engine::UUID{ 102 }) } } },
			{ "lookAt", { { "target", ToString(Engine::UUID{ 200 }) } } }
		} },
		{ "Script", nlohmann::json::array({ {
			{ "serializedFields", {
				{ "inside", reference(scene, Engine::UUID{ 102 }) },
				{ "outside", reference(scene, Engine::UUID{ 200 }) },
				{ "otherAsset", reference(otherScene, Engine::UUID{ 102 }) },
				{ "component", { { "entity", reference(scene, Engine::UUID{ 102 }) }, { "scriptSlotId", "0000000000000040" } } },
				{ "list", nlohmann::json::array({ reference(scene, Engine::UUID{ 102 }) }) }
			} }
		} }) }
	};
	SerializedEntitySnapshot child;
	child.stableUUID = Engine::UUID{ 2 };
	child.sceneInstanceID = source.ownerSceneInstanceID;
	child.sourceAsset = scene;
	child.components = {
		{ "SceneObject", { { "localFileId", ToString(Engine::UUID{ 102 }) } } },
		{ "Hierarchy", { { "parentLocalFileID", ToString(Engine::UUID{ 101 }) } } },
		{ "JointAttachment", { { "skinnedEntityLocalFileID", ToString(Engine::UUID{ 101 }) } } }
	};
	source.entities = { root, child };
	// 壊れた参照も複製先で診断できる形に残す
	auto invalidAsset = reference(scene, Engine::UUID{ 102 });
	invalidAsset["sourceAsset"] = "invalid-guid";
	auto invalidKind = reference(scene, Engine::UUID{ 102 });
	invalidKind["kind"] = 7;
	source.entities[0].components["Script"][0]["serializedFields"]["invalidAsset"] = invalidAsset;
	source.entities[0].components["Script"][0]["serializedFields"]["invalidKind"] = invalidKind;
	EntityTreeSnapshot duplicate;
	EntitySnapshotDuplicator::Build(source, "Clone", duplicate);
	const auto& duplicateRoot = duplicate.entities.at(0).components;
	const auto& duplicateChild = duplicate.entities.at(1).components;
	const auto& rootID = duplicateRoot["SceneObject"]["localFileId"];
	const auto& childID = duplicateChild["SceneObject"]["localFileId"];
	const auto& fields = duplicateRoot["Script"][0]["serializedFields"];

	// 範囲内だけを変換し、別Assetの同じIDとScript個体番号を維持
	bool passed = duplicate.rootStableUUID != source.rootStableUUID &&
		duplicate.ownerSceneInstanceID == source.ownerSceneInstanceID && duplicate.ownerSourceAsset == scene &&
		rootID != root.components["SceneObject"]["localFileId"] &&
		childID != child.components["SceneObject"]["localFileId"] &&
		duplicateChild["Hierarchy"]["parentLocalFileID"] == rootID &&
		duplicateChild["JointAttachment"]["skinnedEntityLocalFileID"] == rootID &&
		duplicateRoot["Hierarchy"]["parentLocalFileID"] == "" && duplicateRoot["Name"]["name"] == "Clone" &&
		duplicateRoot["CameraController"]["follow"]["target"] == childID &&
		duplicateRoot["CameraController"]["lookAt"]["target"] == ToString(Engine::UUID{ 200 }) &&
		fields["inside"]["localFileId"] == childID && fields["component"]["entity"]["localFileId"] == childID &&
		fields["component"]["scriptSlotId"] == "0000000000000040" && fields["list"][0]["localFileId"] == childID &&
		fields["outside"] == reference(scene, Engine::UUID{ 200 }) &&
		fields["otherAsset"] == reference(otherScene, Engine::UUID{ 102 }) &&
		fields["invalidAsset"] == invalidAsset && fields["invalidKind"] == invalidKind;

	// 不正な候補で既に用意したRedo用Snapshotを壊さない
	const auto retainedRoot = duplicate.rootStableUUID;
	const auto retainedComponents = duplicateRoot;
	// 保存順が変わっても名前と親はルートのIDで特定する
	EntityTreeSnapshot reordered = source;
	std::swap(reordered.entities[0], reordered.entities[1]);
	EntityTreeSnapshot reorderedCopy;
	EntitySnapshotDuplicator::Build(reordered, "Reordered", reorderedCopy);
	EntitySnapshotDuplicator::ClearRootParentLink(reordered);
	passed = passed && reorderedCopy.entities[1].stableUUID == reorderedCopy.rootStableUUID &&
		reorderedCopy.entities[1].components["Name"]["name"] == "Reordered" &&
		!reorderedCopy.entities[0].components.contains("Name") &&
		reordered.entities[1].components["Hierarchy"]["parentLocalFileID"] == "" &&
		reordered.entities[0].components["Hierarchy"]["parentLocalFileID"] == ToString(Engine::UUID{ 101 });
	source.entities[1].components["SceneObject"]["localFileId"] = ToString(Engine::UUID{ 101 });
	bool rejected = false;
	try {
		EntitySnapshotDuplicator::Build(source, "Broken", duplicate);
	} catch (const std::invalid_argument&) {
		rejected = true;
	}
	return passed && rejected && duplicate.rootStableUUID == retainedRoot &&
		duplicate.entities[0].components == retainedComponents;
}
