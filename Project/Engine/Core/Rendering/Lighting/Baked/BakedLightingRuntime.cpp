#include "BakedLightingRuntime.h"
#include "BakedLightingStorage.h"
#include "BakedLightingInput.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace {

	using namespace Engine;
	// 未使用のBufferも安全な1要素を持たせる
	template<typename T>
	void UploadBuffer(GraphicsCore& graphics, MeshStructuredHandle<T>& buffer, const std::vector<T>& values) {

		const T empty{};
		buffer.Create(graphics.GetDXObject().GetDevice(), graphics.GetBufferUploadService(), graphics.GetSRVDescriptor(),
			values.empty() ? std::span<const T>(&empty, 1) : std::span<const T>(values), L"BakedLighting");
	}
	// 同じ所有世代のBufferを登録する
	template<typename T>
	void RegisterBuffer(RenderBufferRegistry& registry, const char* name, const MeshStructuredHandle<T>& buffer) {

		registry.Register({.alias = name, .resource = buffer.buffer->GetResource(),
			.gpuAddress = buffer.buffer->GetResource()->GetGPUVirtualAddress(), .srvGPUHandle = buffer.srvGPUHandle});
	}
}

Engine::BakedLightingRuntime::~BakedLightingRuntime() = default;

void Engine::BakedLightingRuntime::Upload(GraphicsCore& graphics, Generation& generation) {

	auto& data = *generation.data;
	UploadBuffer(graphics, generation.triangles, data.triangles);
	UploadBuffer(graphics, generation.nodes, data.nodes);
	UploadBuffer(graphics, generation.order, data.triangleOrder);
	UploadBuffer(graphics, generation.lightmaps, data.lightmaps);
	UploadBuffer(graphics, generation.probes, data.probes);
	UploadBuffer(graphics, generation.captures, data.captures);
	UploadBuffer(graphics, generation.reflectionPixels, data.reflectionPixels);
	generation.triangleCount = static_cast<uint32_t>(data.triangles.size());
	generation.probeCount = static_cast<uint32_t>(data.probes.size());
	generation.captureCount = static_cast<uint32_t>(data.captures.size());
	// 転送後は照合に使うCPUデータだけ残す
	std::vector<BakedLightingTriangle>().swap(data.triangles);
	std::vector<BakedLightingNode>().swap(data.nodes);
	std::vector<uint32_t>().swap(data.triangleOrder);
	std::vector<Vector4>().swap(data.lightmaps);
	std::vector<BakedLightingProbe>().swap(data.probes);
	std::vector<BakedReflectionCapture>().swap(data.captures);
	std::vector<Vector4>().swap(data.reflectionPixels);
}

Engine::BakedLightingRuntime::Generation::WorldState& Engine::BakedLightingRuntime::ResolveWorldState(
	Generation& generation, const SceneExecutionContext& context) {

	std::erase_if(generation.worlds, [](const auto& state) { return state.lifetime.expired(); });
	for (auto& state : generation.worlds) {
		if (state.lifetime.lock() == context.world->GetLifetime() && state.sceneInstance == context.sceneInstance->instanceID) return state;
	}
	generation.worlds.emplace_back();
	auto& state = generation.worlds.back();
	state.lifetime = context.world->GetLifetime();
	state.sceneInstance = context.sceneInstance->instanceID;
	return state;
}

bool Engine::BakedLightingRuntime::IsCurrent(Generation& generation, const SceneExecutionContext& context) {

	if (!context.world || !context.sceneInstance || !generation.data || generation.data->sceneAsset != context.sceneInstance->sceneAsset) return false;
	auto& state = ResolveWorldState(generation, context);
	auto& world = *context.world;
	if (state.renderRevision == world.GetRenderDataRevision() && state.transformRevision == world.GetRenderTransformRevision() &&
		state.lightRevision == world.GetLightDataRevision()) return state.current;
	state.renderRevision = world.GetRenderDataRevision();
	state.transformRevision = world.GetRenderTransformRevision();
	state.lightRevision = world.GetLightDataRevision();
	// 同じView間では保存値の比較とID対応を繰り返さない
	state.current = BakedLightingInputBuilder::IsCurrent(*generation.data, world, state.sceneInstance);
	state.receiverEntities.assign(generation.data->receivers.size(), UINT32_MAX);
	if (state.current) {
		std::unordered_map<UUID, uint32_t> entities;
		world.ForEach<SceneObjectComponent>([&](Entity entity, const SceneObjectComponent& object) {
			if (object.sceneInstanceID == state.sceneInstance) entities[object.localFileID] = entity.index + 1u;
		});
		for (size_t index = 0; index < generation.data->receivers.size(); ++index) {
			state.receiverEntities[index] = entities.at(generation.data->receivers[index].localFileID);
		}
	}
	return state.current;
}

void Engine::BakedLightingRuntime::Register(GraphicsCore& graphics, Generation& generation, SceneExecutionContext& context,
	const Constants& constants) {

	constants_.Upload(constants);
	context.bufferRegistry.Register({.alias = "BakedLightingConstants", .resource = nullptr, .gpuAddress = constants_.GetGPUAddress()});
	RegisterBuffer(context.bufferRegistry, "gBakedTriangles", generation.triangles);
	RegisterBuffer(context.bufferRegistry, "gBakedNodes", generation.nodes);
	RegisterBuffer(context.bufferRegistry, "gBakedOrder", generation.order);
	RegisterBuffer(context.bufferRegistry, "gBakedLightmaps", generation.lightmaps);
	RegisterBuffer(context.bufferRegistry, "gBakedProbes", generation.probes);
	RegisterBuffer(context.bufferRegistry, "gBakedCaptures", generation.captures);
	RegisterBuffer(context.bufferRegistry, "gBakedReflectionPixels", generation.reflectionPixels);
	// 保存IDを現在WorldのEntity番号へ対応付ける
	const uint32_t empty = UINT32_MAX;
	const auto receivers = constants.receiverCount ? std::span<const uint32_t>(ResolveWorldState(generation, context).receiverEntities) :
		std::span<const uint32_t>(&empty, 1);
	const auto allocation = receiverAllocator_.AllocateAndUploadBytes(
		graphics.GetDXObject().GetResourceRetirement(), graphics.GetDXObject().GetDevice(),
		{reinterpret_cast<const uint8_t*>(receivers.data()), receivers.size_bytes()});
	context.bufferRegistry.Register({.alias = "gBakedReceiverEntities", .resource = nullptr, .gpuAddress = allocation.gpuAddress});
}

void Engine::BakedLightingRuntime::Update(GraphicsCore& graphics, SceneExecutionContext& context) {

	constants_.Init(graphics.GetDXObject().GetResourceRetirement(), graphics.GetDXObject().GetDevice());
	if (!empty_) {
		empty_ = std::make_unique<Generation>();
		empty_->data = std::make_shared<BakedLightingData>();
		Upload(graphics, *empty_);
	}
	Generation* active = empty_.get();
	Constants constants;
	const auto* scene = context.sceneInstance;
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	// 使われなくなったSceneのGPU資源を回収する
	std::erase_if(retired_, [](const auto& generation) {
		return !generation->loading.valid() || generation->loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	});
	std::erase_if(generations_, [serial](const auto& entry) {
		return HasExpiredGraphicsResource(entry.second->lastUsed, serial) && (!entry.second->loading.valid() ||
			entry.second->loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
	});
	if (scene && scene->header.bakedLightingEnabled && scene->header.bakedLighting && context.assetDatabase) {
		const AssetID id = scene->header.bakedLighting;
		auto& generation = generations_[id];
		auto& database = *context.assetDatabase;
		bool changed = generation && generation->databaseLifetime.expired();
		if (generation && generation->contentRevision != database.GetContentRevision()) {
			// 無関係なAssetの保存ではGPU資源を作り直さない
			for (const auto& [asset, revision] : generation->assetRevisions) {
				changed |= !database.Find(asset) || database.GetContentRevision(asset) != revision;
			}
			generation->contentRevision = database.GetContentRevision();
		}
		if (changed) {
			// 読込中のWorkerを待たず別の所有へ移す
			retired_.push_back(std::move(generation));
		}
		if (!generation) {
			generation = std::make_unique<Generation>();
			generation->databaseLifetime = database.GetCacheLifetime();
			generation->contentRevision = database.GetContentRevision();
			try {
				const auto path = database.ResolveFullPath(id);
				const auto document = JsonFile::Load(path, false);
				BakedLightingData input;
				input.inputRevisions = document.at("inputRevisions");
				BakedLightingInputBuilder::ValidateAssets(input, database, false);
				const auto dataPath = database.ResolveFullPath(ParseAssetID(document, "bakedLightingData"));
				generation->assetRevisions.emplace_back(id, database.GetContentRevision(id));
				const AssetID binary = ParseAssetID(document, "bakedLightingData");
				generation->assetRevisions.emplace_back(binary, database.GetContentRevision(binary));
				std::vector<std::pair<std::filesystem::path, std::string>> inputs;
				for (const auto& asset : document.at("inputRevisions").at("assets")) {
					const AssetID source = ParseAssetID(asset, "assetId");
					inputs.emplace_back(database.ResolveFullPath(source), asset.at("hash").get<std::string>());
					generation->assetRevisions.emplace_back(source, database.GetContentRevision(source));
				}
				generation->loading = std::async(std::launch::async, [path, dataPath, inputs = std::move(inputs)]() {
					for (const auto& [input, hash] : inputs) {
						if (ContentHash::FileSHA256(input) != hash) throw std::runtime_error("ベイク後に入力Assetが変更されています 再ベイクしてください");
					}
					return BakedLightingStorage::Load(path, dataPath);
				});
			} catch (const std::exception& error) {
				generation->failed = true;
				Logger::Output(LogType::Engine, spdlog::level::warn, "[BakedLighting] 読込できません 内容={}", error.what());
			}
		}
		generation->lastUsed = serial;
		if (generation->loading.valid() && generation->loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			try {
				generation->data = std::make_shared<BakedLightingData>(generation->loading.get());
				Upload(graphics, *generation);
			} catch (const std::exception& error) {
				generation->failed = true;
				Logger::Output(LogType::Engine, spdlog::level::warn, "[BakedLighting] 読込できません 内容={}", error.what());
			}
		}
		if (!generation->failed && generation->data && IsCurrent(*generation, context)) {
			active = generation.get();
			const auto& data = *active->data;
			constants.triangleCount = active->triangleCount;
			constants.atlasSize = data.atlasSize;
			constants.probeCount = active->probeCount;
			constants.captureCount = active->captureCount;
			constants.probeMinimum = data.probeMinimum;
			constants.probeSpacing = data.probeSpacing;
			std::copy(data.probeDimensions.begin(), data.probeDimensions.end(), constants.probeDimensions);
			constants.surfaceTolerance = data.surfaceTolerance;
			constants.receiverCount = static_cast<uint32_t>(data.receivers.size());
		}
	}
	Register(graphics, *active, context, constants);
}
