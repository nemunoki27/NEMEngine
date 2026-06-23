#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	ゲームプレイの構造変更コールバック
	//	Entity生成とPrefabとSceneとSetParent、構造変更はWorldCommandBuffer経由で遅延適用する
	//	生成系は空Entityを即時予約してハンドルを返しコンポーネントと名前とparentはflushで適用する
	//============================================================================
	namespace {

		// コールバック中に対象worldを解決する、parentが有効ならそのworld無効なら現在のactive world
		ECSWorld* ResolveTargetWorld(ManagedNativeEntity parent) {

			if (ECSWorld* fromParent = ResolveWorld(parent)) {
				return fromParent;
			}
			const SystemContext* context = ManagedScriptRuntime::GetCurrentContext();
			return context ? context->world : nullptr;
		}
	}

	ManagedNativeEntity ManagedScriptRuntime::ResolveEntityRefCallback([[maybe_unused]] uint64_t sourceAsset, uint64_t localFileId) {

		// localFileIDはEdit/Playをまたいで安定するため、これで現在のworldのentityを引く
		// sourceAssetは将来のマルチシーン絞り込み用で現状は未使用
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || localFileId == 0) {
			return MakeNullNativeEntity();
		}

		UUID id{};
		id.value = localFileId;
		const Entity entity = SceneObjectUtility::FindByLocalFileID(*world, id);
		if (!world->IsAlive(entity)) {
			return MakeNullNativeEntity();
		}
		return MakeNativeEntity(*world, entity);
	}

	namespace {

		// ManagedLinePointをエンジンのLinePointへ変換する
		LinePoint ToLinePoint(const ManagedLinePoint& src) {

			LinePoint point{};
			point.position = Vector3(src.position.x, src.position.y, src.position.z);
			point.color = Color4(src.color.r, src.color.g, src.color.b, src.color.a);
			point.thickness = src.thickness;
			return point;
		}
	}

	void ManagedScriptRuntime::LineSetPointsCallback(ManagedNativeEntity entity,
		const ManagedLinePoint* points, int32_t count, int32_t loop) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponent<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return;
		}

		// count0はクリア扱い、点列を丸ごと差し替える
		line->points.clear();
		if (points != nullptr && count > 0) {

			line->points.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				line->points.emplace_back(ToLinePoint(points[i]));
			}
		}
		line->loop = (loop != 0);
	}

	void ManagedScriptRuntime::LineAddPointCallback(ManagedNativeEntity entity, ManagedLinePoint point) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponent<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return;
		}
		line->points.emplace_back(ToLinePoint(point));
	}

	void ManagedScriptRuntime::LineDrawImmediateCallback(const ManagedLinePoint* points,
		int32_t count, int32_t loop, int32_t is2D, uint64_t materialID) {

		if (points == nullptr || count < 2) {
			return;
		}

		// 即時バッファへ積むため一旦エンジン型へ変換する
		std::vector<LinePoint> converted;
		converted.reserve(static_cast<size_t>(count));
		for (int32_t i = 0; i < count; ++i) {
			converted.emplace_back(ToLinePoint(points[i]));
		}
		LineImmediateBuffer::GetInstance().AddPolyline(converted.data(), static_cast<uint32_t>(count),
			true, loop != 0, is2D != 0, AssetID{ materialID });
	}

	void ManagedScriptRuntime::LineDrawSphereImmediateCallback(ManagedVector3 center, float radius,
		ManagedColor4 color, int32_t division, float thickness, uint64_t materialID) {

		const uint32_t safeDivision = division < 3 ? 3u : static_cast<uint32_t>(division);
		LineImmediateBuffer::GetInstance().AddSphere(
			Vector3(center.x, center.y, center.z),
			radius, Color4(color.r, color.g, color.b, color.a),
			safeDivision, thickness, AssetID{ materialID });
	}

	void ManagedScriptRuntime::LineDrawShapeCallback(const ManagedLineShape* shape) {

		if (shape == nullptr) {
			return;
		}

		const Color4 color(shape->color.r, shape->color.g, shape->color.b, shape->color.a);
		const Vector3 a(shape->a.x, shape->a.y, shape->a.z);
		const Vector3 b(shape->b.x, shape->b.y, shape->b.z);
		const Quaternion rotation(shape->rotation.x, shape->rotation.y, shape->rotation.z, shape->rotation.w);
		const uint32_t division = shape->division < 3 ? 3u : static_cast<uint32_t>(shape->division);

		// 形状種別ごとに線分リストへ展開する
		std::vector<LinePoint> segments;
		switch (static_cast<ManagedLineShapeKind>(shape->shapeType)) {
		case ManagedLineShapeKind::Circle2D:
			LineShapeBuilder::BuildCircle2D(Vector2(a.x, a.y), shape->radius, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Rect2D:
			LineShapeBuilder::BuildRect2D(Vector2(a.x, a.y), Vector2(b.x, b.y), rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Hemisphere:
			LineShapeBuilder::BuildHemisphere(a, shape->radius, rotation, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::AABB:
			LineShapeBuilder::BuildAABB(a, b, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::OBB:
			LineShapeBuilder::BuildOBB(a, b, rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Cone:
			LineShapeBuilder::BuildCone(a, shape->radius, shape->radius2, shape->height, rotation, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Arrow:
			LineShapeBuilder::BuildArrow(a, shape->height, rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Axis:
			LineShapeBuilder::BuildAxis(a, rotation, shape->height, shape->thickness, segments);
			break;
		default:
			return;
		}

		if (segments.size() < 2) {
			return;
		}
		// 形状は2点ずつ独立した線分リストなのでconnected=falseで積む
		LineImmediateBuffer::GetInstance().AddPolyline(segments.data(), static_cast<uint32_t>(segments.size()),
			false, false, shape->is2D != 0, AssetID{ shape->materialID });
	}

	ManagedNativeEntity ManagedScriptRuntime::CreateEntityCallback(const char* name, ManagedNativeEntity parent) {

		ECSWorld* world = ResolveTargetWorld(parent);
		if (!world) {
			return MakeNullNativeEntity();
		}
		// 空Entityを即時予約する、emptyArchetypeへの行追加のみでコンポーネント追加つまりarchetype移行はflushへ
		const Entity reserved = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueCreateEntity(reserved, name ? name : "", parentEntity);
		return MakeNativeEntity(*world, reserved);
	}

	ManagedNativeEntity ManagedScriptRuntime::InstantiatePrefabCallback(uint64_t prefabAssetId,
		ManagedVector3 position, ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent) {

		ECSWorld* world = ResolveTargetWorld(parent);
		if (!world || prefabAssetId == 0) {
			return MakeNullNativeEntity();
		}
		// ルートEntityを即時予約しPrefabSystemにはreservedRootを渡して実体化させる、遅延でも実rootを返す
		const Entity reservedRoot = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueInstantiatePrefab(reservedRoot, UUID{ prefabAssetId },
			Vector3(position.x, position.y, position.z),
			Quaternion(rotation.x, rotation.y, rotation.z, rotation.w),
			useTransform != 0, parentEntity);
		return MakeNativeEntity(*world, reservedRoot);
	}

	uint64_t ManagedScriptRuntime::LoadSceneAdditiveCallback(uint64_t sceneAssetId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneAssetId == 0) {
			return 0;
		}
		// instance IDを先行採番してC#のSceneHandleと一致させ、load自体はflushへ回す
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneAdditive(instanceID, UUID{ sceneAssetId });
		return instanceID.value;
	}

	uint64_t ManagedScriptRuntime::LoadSceneSingleCallback(uint64_t sceneAssetId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneAssetId == 0) {
			return 0;
		}
		// 単一ロード、新sceneをactiveにし旧sceneを全てアンロードする処理はflushで行う
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, UUID{ sceneAssetId });
		return instanceID.value;
	}

	void ManagedScriptRuntime::UnloadSceneCallback(uint64_t sceneInstanceId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceId == 0) {
			return;
		}
		world->GetCommandBuffer().EnqueueUnloadScene(UUID{ sceneInstanceId });
	}

	int32_t ManagedScriptRuntime::IsSceneInstanceAliveCallback(uint64_t sceneInstanceId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceId == 0) {
			return 0;
		}
		const WorldCommandServices& services = world->GetCommandServices();
		if (!services.sceneInstances) {
			return 0;
		}
		return services.sceneInstances->Find(UUID{ sceneInstanceId }) != nullptr ? 1 : 0;
	}

	void ManagedScriptRuntime::SetParentKeepWorldCallback(ManagedNativeEntity child, ManagedNativeEntity parent, int32_t worldPositionStays) {

		ECSWorld* world = ResolveWorld(child);
		if (!world) {
			return;
		}
		const Entity childEntity = ResolveEntity(child);
		const Entity parentEntity = ResolveEntity(parent);
		world->GetCommandBuffer().EnqueueSetParent(childEntity, parentEntity, worldPositionStays != 0);
	}

	//============================================================================
	//	AudioSourceのゲームプレイメソッド
	//	実際の音声制御はAudioSourceSystemがruntimePlayRequestを消費して行い1フレーム遅延する
	//============================================================================
	namespace {

		// 対象entityのAudioSourceComponentを取得する、無効entityやcomponent無しはnullptr
		AudioSourceComponent* ResolveAudioSource(ManagedNativeEntity entity) {
			ECSWorld* world = ResolveWorld(entity);
			if (!world) {
				return nullptr;
			}
			const Entity resolved = ResolveEntity(entity);
			return world->IsAlive(resolved) ? world->TryGetComponent<AudioSourceComponent>(resolved) : nullptr;
		}
	}

	void ManagedScriptRuntime::AudioPlayCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->runtimePlayRequest = 1;
		}
	}

	void ManagedScriptRuntime::AudioPauseCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->runtimePlayRequest = 2;
		}
	}

	void ManagedScriptRuntime::AudioStopCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->runtimePlayRequest = 3;
		}
	}

	int32_t ManagedScriptRuntime::AudioIsPlayingCallback(ManagedNativeEntity entity) {
		const AudioSourceComponent* audio = ResolveAudioSource(entity);
		// pause中は再生中扱いにしない
		return (audio && audio->runtimePlaying && !audio->runtimePaused) ? 1 : 0;
	}

} // Engine
