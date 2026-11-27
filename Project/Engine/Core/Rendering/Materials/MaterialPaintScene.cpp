#include "MaterialPaintScene.h"
#include "MaterialPaintPreview.h"
#include <Engine/Core/Rendering/Decals/DecalGraphProgram.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Components/Rendering/MaterialPaintComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <bit>

namespace {
	using namespace Engine;
	using Word = std::array<uint32_t, 4>;

	struct PreviewReceiver {
		Entity entity;
		MaterialPaintComponent component;
	};
	std::weak_ptr<const ECSWorldLifetime> previewWorld;
	std::vector<PreviewReceiver> previews;
	uint64_t previewRevision = 0;

	Word FloatWord(float x, float y, float z, float w) {
		return std::bit_cast<Word>(std::array{x, y, z, w});
	}

	void AppendMatrix(std::vector<Word>& words, const Matrix4x4& matrix) {
		for (const auto& row : matrix.m) words.push_back(FloatWord(row[0], row[1], row[2], row[3]));
	}
} // namespace

void Engine::SetMaterialPaintPreview(const ECSWorld& world, Entity entity, const MaterialPaintComponent& component) {

	const auto lifetime = world.GetLifetime();
	if (previewWorld.lock() != lifetime) {
		previews.clear();
		previewWorld = lifetime;
	}
	++previewRevision;
	const auto found = std::ranges::find(previews, entity, &PreviewReceiver::entity);
	if (found != previews.end())
		found->component = component;
	else
		previews.push_back({entity, component});
}

void Engine::ClearMaterialPaintPreviews(const ECSWorld& world) {

	if (previewWorld.lock() == world.GetLifetime()) {
		previews.clear();
		previewWorld.reset();
	}
}

void Engine::MaterialPaintScene::Prepare(GraphicsCore& graphics, ECSWorld* world, const AssetDatabase* database,
                                         RenderAssetLibrary& assets, PaintMaterialSample sample, const SystemContext* system) {

	if (!initialized_) {
		auto& dx = graphics.GetDXObject();
		sceneBuffer_.Init(dx.GetDevice(), &graphics.GetSRVDescriptor());
		geometryBuffer_.Init(dx.GetDevice(), &graphics.GetSRVDescriptor(), &graphics.GetBufferUploadService());
		owners_.Connect(dx.GetResourceRetirement());
		initialized_ = true;
	}
	if (database_ != database) {
		database_ = database;
		materials_.clear();
		atlasIDs_.clear();
		geometry_.clear();
	}
	struct Receiver {
		Entity entity;
		const MaterialPaintComponent* paint;
		const TransformComponent* transform;
	};
	std::vector<Receiver> receivers;
	std::vector<AssetID> ids;
	const bool showPreviews = world && !previews.empty() && previewWorld.lock() == world->GetLifetime();
	if (world && database && !sample.material) {
		auto append = [&](Entity entity, const MaterialPaintComponent& paint, const TransformComponent& transform) {
			if (!paint.mask || !paint.atlas || paint.layers.empty() ||
			    !MaterialPaintGeometryMatches(*database, *world, entity, paint))
				return;
			receivers.push_back({entity, &paint, &transform});
			ids.push_back(paint.atlas);
		};
		world->ForEach<MaterialPaintComponent, TransformComponent>(
		    [&](Entity entity, const MaterialPaintComponent& paint, const TransformComponent& transform) {
			    if (showPreviews && std::ranges::find(previews, entity, &PreviewReceiver::entity) != previews.end())
				    return;
			    append(entity, paint, transform);
		    });
		if (showPreviews) {
			for (const auto& preview : previews) {
				if (const auto* transform = world->TryGetComponent<TransformComponent>(preview.entity)) {
					append(preview.entity, preview.component, *transform);
				}
			}
		}
	}
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	if (geometry_.empty() || ids != atlasIDs_) {
		atlasIDs_ = ids;
		atlases_.clear();
		ranges_.clear();
		geometry_.assign(1, {});
		for (const auto id : ids) {
			const auto atlas = LoadMaterialPaintAtlas(*database, id);
			if (!atlas || atlas->triangles.empty()) continue;
			atlases_.push_back(atlas);
			auto& range = ranges_[id];
			range.range[0] = static_cast<uint32_t>(geometry_.size());
			range.range[1] = static_cast<uint32_t>(atlas->triangles.size());
			for (const auto& tri : atlas->triangles) {
				const auto& a = tri.positions[0];
				const auto b = tri.positions[1] - a, c = tri.positions[2] - a;
				const auto u = tri.uv[1] - tri.uv[0], v = tri.uv[2] - tri.uv[0];
				geometry_.push_back(FloatWord(a.x, a.y, a.z, tri.uv[0].x));
				geometry_.push_back(FloatWord(b.x, b.y, b.z, tri.uv[0].y));
				auto edge = FloatWord(c.x, c.y, c.z, 0);
				edge[3] = tri.subMesh;
				geometry_.push_back(edge);
				geometry_.push_back(FloatWord(u.x, u.y, v.x, v.y));
				range.subMeshCount = (std::max)(range.subMeshCount, tri.subMesh + 1);
			}
			range.range[2] = static_cast<uint32_t>(geometry_.size());
			range.range[3] = static_cast<uint32_t>(atlas->meshletTriangles.size());
			for (size_t i = 0; i < atlas->meshletTriangles.size(); ++i) {
				if (i % 4 == 0) geometry_.emplace_back();
				geometry_.back()[i % 4] = atlas->meshletTriangles[i];
			}
		}
		geometryBuffer_.MarkFullUpdate(static_cast<uint32_t>(geometry_.size()));
	}
	geometryBuffer_.UploadCurrentFrame(geometry_);
	std::sort(receivers.begin(), receivers.end(),
	          [](const Receiver& a, const Receiver& b) { return a.entity.index < b.entity.index; });
	auto& owners = owners_[GraphicsFrameState::GetCurrentIndex()];
	if (ownershipFrame_ != GraphicsFrameState::GetFrameSerial()) {
		ownershipFrame_ = GraphicsFrameState::GetFrameSerial();
		owners.clear();
	}
	std::array<std::unordered_map<AssetID, uint32_t>, 3> textures;
	const auto* fallback = graphics.GetBuiltinTextureLibrary().GetErrorTexture();
	auto texture = [&](AssetID id, bool srgb = false, bool normal = false) {
		if (!id) return UINT32_MAX;
		auto& cache = textures[normal ? 2 : srgb ? 1 : 0];
		auto [entry, inserted] = cache.try_emplace(id, UINT32_MAX);
		if (inserted) {
			auto result = RuntimeTextureResolver::ResolveBindless(
			    graphics, database, id, srgb ? TextureColorSpace::SRGB : TextureColorSpace::Linear, normal);
			if (!result.retry && (!fallback || result.srvIndex != fallback->srvIndex)) {
				entry->second = result.srvIndex;
				if (result.ownership) owners.emplace_back(std::move(result.ownership));
			}
		}
		return entry->second;
	};
	const bool sampleVisible = sample.material && world && world->IsAlive(sample.subject);
	std::vector<Word> words(1 + receivers.size() + (sampleVisible ? 1 : 0));
	std::vector<Word> programWords;
	std::vector<size_t> graphOffsets;
	std::unordered_map<const MaterialAsset*, Word> graphRanges;
	const auto appendProgram = [&](const MaterialAsset* material, const MaterialParameterSet* overrides) {
		if (!material || !material->paintGraph) return Word{};
		const auto existing = graphRanges.find(material);
		if (!overrides && existing != graphRanges.end()) return existing->second;
		Word range{};
		range[2] = static_cast<uint32_t>(programWords.size());
		range[3] = static_cast<uint32_t>(material->paintGraph->instructions.size());
		for (const auto& command : material->paintGraph->instructions) {
			if (command.op == DecalGraphOp::UV || command.op == DecalGraphOp::TextureSample || command.op == DecalGraphOp::NormalUnpack)
				range[0] = 1;
			const auto* parameter = overrides && command.parameter ? overrides->Find(command.parameter) : nullptr;
			if (!parameter && command.parameter) parameter = material->parameters.Find(command.parameter);
			const Vector4 value = parameter ? DecalGraphValue(*parameter) : command.value;
			auto literal = FloatWord(value.x, value.y, value.z, value.w);
			if (command.op == DecalGraphOp::Texture) {
				const auto* id = parameter ? std::get_if<AssetID>(&parameter->value) : nullptr;
				const bool srgb = command.semantic == MaterialParameterSemantic::BaseColorTexture ||
					command.semantic == MaterialParameterSemantic::EmissiveTexture;
				literal[0] = texture(id ? *id : command.texture, srgb, command.semantic == MaterialParameterSemantic::NormalTexture);
			}
			programWords.push_back({static_cast<uint32_t>(command.op), command.inputs, 0, 0});
			programWords.push_back(literal);
		}
		if (!overrides) graphRanges.emplace(material, range);
		return range;
	};
	const auto appendLayer = [&](const PaintMaterialAsset& m, uint32_t channel, float heightContrast, const MaterialAsset* material) {
		words.push_back({texture(m.baseColorTexture, true), texture(m.normalTexture, false, true),
		                 texture(m.roughnessTexture), texture(m.metallicTexture)});
		words.push_back({texture(m.occlusionTexture), texture(m.heightTexture), m.channels, channel});
		words.push_back(FloatWord(m.baseColor.r, m.baseColor.g, m.baseColor.b, m.baseColor.a));
		words.push_back(FloatWord(m.normalStrength, m.roughness, m.metallic, m.occlusion));
		words.push_back(FloatWord(m.tiling, m.height, heightContrast, 0));
		words.push_back(FloatWord(m.emissiveColor.r, m.emissiveColor.g, m.emissiveColor.b, m.emissiveIntensity));
		words.push_back({texture(m.emissiveTexture, true), texture(m.metallicRoughnessTexture), 0, 0});
		if (material && material->paintGraph) {
			graphOffsets.push_back(words.size() - 1);
			words.back() = appendProgram(material, nullptr);
		}
	};
	uint32_t count = 0;
	materialRevision_ = showPreviews ? previewRevision : 0;
	for (const auto& receiver : receivers) {
		const auto& paint = *receiver.paint;
		const auto found = ranges_.find(paint.atlas);
		if (found == ranges_.end()) continue;
		const uint32_t mask = texture(paint.mask);
		if (mask == UINT32_MAX) continue;
		Matrix4x4 inverse, reverse;
		const auto& transform = receiver.transform->worldMatrix;
		if (!Matrix4x4::TryInverse(transform, inverse)) continue;
		const auto projection = inverse * paint.projectionFromLocal;
		if (!Matrix4x4::TryInverse(projection, reverse)) continue;
		const auto start = static_cast<uint32_t>(words.size());
		words[++count] = {receiver.entity.index, receiver.entity.generation, start, 0};
		words.push_back(found->second.range);
		words.emplace_back();
		AppendMatrix(words, projection);
		AppendMatrix(words, reverse);
		uint32_t layerCount = 0;
		for (const auto& layer : paint.layers) {
			if (!layer.visible || !layer.material || layer.channel >= 4 || layerCount >= 4) continue;
			auto& cached = materials_[layer.material];
			const auto revision = database->GetContentRevision(layer.material);
			if (cached.revision != revision) {
				const auto path = database->ResolveFullPath(layer.material);
				const auto* meta = database->Find(layer.material);
				cached.surface = meta && meta->type == AssetType::Material;
				cached.libraryRevision = UINT64_MAX;
				cached.valid = cached.surface || LoadPaintMaterialAsset(path, cached.asset);
				cached.revision = revision;
			}
			const auto* material = cached.surface ? assets.LoadMaterial(layer.material) : nullptr;
			if (cached.surface && cached.libraryRevision != *assets.GetMaterialRevision()) {
				cached.valid = material && MaterialRenderPolicy::GetMaterialPaintUnsupportedReason(*material).empty();
				if (cached.valid) cached.asset = MakePaintMaterialSample(*material);
				cached.libraryRevision = *assets.GetMaterialRevision();
			}
			if (!cached.valid) continue;
			appendLayer(cached.asset, layer.channel,
				layer.heightBlend ? layer.heightContrast : 0.0f, material);
			++layerCount;
		}
		Algorithm::HashCombine(materialRevision_, receiver.entity.index);
		Algorithm::HashCombine(materialRevision_, receiver.entity.generation);
		Algorithm::HashCombine(materialRevision_, mask);
		Algorithm::HashCombine(materialRevision_, std::hash<AssetID>{}(paint.atlas));
		for (size_t i = start + 10; i < words.size(); ++i) {
			for (const auto value : words[i]) Algorithm::HashCombine(materialRevision_, value);
		}
		const uint32_t surfaces = found->second.subMeshCount;
		words[start + 1] = {mask, static_cast<uint32_t>(words.size()), surfaces, layerCount};
		const auto* mesh = world->TryGetComponent<MeshRendererComponent>(receiver.entity);
		const auto* primitive = world->TryGetComponent<PrimitiveRendererComponent>(receiver.entity);
		const auto subMeshes = GetMeshSubMeshes(std::as_const(*world), receiver.entity);
		for (uint32_t i = 0; i < surfaces; ++i) {
			MeshSubMeshRenderState state{};
			Matrix4x4 toLocal = inverse;
			bool valid = true;
			if (mesh && i < subMeshes.size()) {
				state = MeshDrawPathCommon::ResolveSubMeshRenderState(*mesh, subMeshes[i]);
				valid = Matrix4x4::TryInverse(MeshSubMeshRuntime::BuildRenderLocalMatrix(subMeshes[i]) * transform,
				                              toLocal);
			} else if (mesh || primitive) {
				state.material = mesh ? mesh->material : primitive->material;
				state.surfaceMode = (mesh ? mesh->queue : primitive->queue) == RenderPhase::Opaque
				                        ? MaterialSurfaceMode::Opaque
				                        : MaterialSurfaceMode::Transparent;
			}
			const auto* material = valid ? MaterialRenderPolicy::ResolveMaterialPaintSurface(*database, assets, state,
				mesh ? DefaultMaterialSlot::Mesh : DefaultMaterialSlot::Primitive) : nullptr;
			const auto height = i < paint.baseHeightTextures.size() ? texture(paint.baseHeightTextures[i]) : UINT32_MAX;
			Word surface{height, material ? 1u : 0u, 0, 0};
			if (material && material->paintGraph) {
				const auto* overrides = mesh && i < subMeshes.size() ? &subMeshes[i].materialInstance : primitive ? &primitive->materialInstance : nullptr;
				const auto range = appendProgram(material, overrides);
				surface[2] = range[2];
				surface[3] = range[3];
				graphOffsets.push_back(words.size());
			}
			for (const auto value : surface) Algorithm::HashCombine(materialRevision_, value);
			words.push_back(surface);
			AppendMatrix(words, toLocal);
		}
	}
	if (sampleVisible) {
		const uint32_t start = static_cast<uint32_t>(words.size());
		words[++count] = {sample.subject.index, sample.subject.generation, start, 0};
		// 全面を塗った見本にはUVアトラスとマスクが不要
		words.emplace_back();
		words.push_back({UINT32_MAX, 0, 0, 1});
		AppendMatrix(words, Matrix4x4::Identity());
		AppendMatrix(words, Matrix4x4::Identity());
		appendLayer(*sample.material, 0, 0.0f, nullptr);
	}
	const auto base = static_cast<uint32_t>(words.size());
	for (size_t at : graphOffsets) words[at][2] += base;
	for (const auto& word : programWords) {
		for (uint32_t value : word) Algorithm::HashCombine(materialRevision_, value);
	}
	words.insert(words.end(), programWords.begin(), programWords.end());
	words[0] = {count, (showPreviews || sampleVisible) ? 1u : 0u, static_cast<uint32_t>(words.size()), 0};
	if (!programWords.empty()) words.push_back(system ? FloatWord(system->time, system->deltaTime, system->smoothDeltaTime, 0) : Word{});
	sceneBuffer_.Upload(words);
}

uint64_t Engine::MaterialPaintScene::Publish(RenderBufferRegistry& registry) const {

	registry.Register({.alias = "gMaterialPaintScene",
	                   .resource = sceneBuffer_.GetResource(),
	                   .gpuAddress = sceneBuffer_.GetGPUAddress(),
	                   .srvGPUHandle = sceneBuffer_.GetGPUHandle(),
	                   .stride = sizeof(Word)});
	registry.Register({.alias = "gMaterialPaintAtlas",
	                   .resource = geometryBuffer_.GetResource(),
	                   .gpuAddress = geometryBuffer_.GetGPUAddress(),
	                   .srvGPUHandle = geometryBuffer_.GetGPUHandle(),
	                   .stride = sizeof(Word)});
	return materialRevision_;
}
