#include "ParticleGPUSourceResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/ParticleMeshSourcePreparation.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <utility>

using namespace Engine;

namespace {

	// 平行移動だけでは面積と辺長の分布を再計算しない
	bool SameWeights(std::span<const ParticleSourceSubMeshGPU> previous, std::span<const ParticleSourceSubMeshGPU> current) {

		if (previous.size() != current.size()) return false;
		for (size_t i = 0; i < current.size(); ++i) {
			if (previous[i].enabled != current[i].enabled) return false;
			for (uint32_t row = 0; row < 3; ++row)
				for (uint32_t column = 0; column < 3; ++column)
					if (previous[i].world.m[row][column] != current[i].world.m[row][column]) return false;
		}
		return true;
	}
}

bool ParticleGPUSourceResources::PrepareMesh(GraphicsCore& graphics, ECSWorld& world, Entity owner,
	const ParticleGPUSimulationStep& step, RenderAssetLibrary& assets, PipelineStateCache& pipelines, const ParticleGPUFrameInputs& inputs) {

	if (!inputs.meshes) { diagnostic_ = "Mesh発生元の描画入力がありません"; return false; }
	const auto& source = step.definition->settings->source;
	const auto reference = ResolveParticleMeshSourceReference(world, owner, source);
	const auto* mesh = inputs.meshes->FindMeshResource(reference.mesh);
	if (!mesh || !mesh->IsValid()) { diagnostic_ = "発生元Meshを読み込み中です"; return false; }
	const bool changed = mesh_ != reference.mesh || renderer_ != reference.renderer || generation_ != mesh->reloadGeneration;
	if (changed) {
		sourceGeneration_ = (sourceGeneration_ + 1) & 0xffffu;
		sampling_.reset();
		previousTransforms_.clear();
		previousReady_ = false;
		mesh_ = reference.mesh;
		renderer_ = reference.renderer;
		generation_ = mesh->reloadGeneration;
	}
	settings_.flags |= sourceGeneration_ << 16;
	Retain(mesh->ownership);
	Matrix4x4 root = reference.renderer.IsValid() ? RenderItemExtract::GetWorldMatrix(world, reference.renderer) : step.emitterWorld;
	const auto edited = GetMeshSubMeshes(std::as_const(world), reference.renderer);
	std::vector<ParticleSourceSubMeshGPU> transforms(mesh->subMeshes.size());
	for (size_t i = 0; i < transforms.size(); ++i) {
		auto& transform = transforms[i];
		const auto edit = std::find_if(edited.begin(), edited.end(), [&](const auto& value) { return value.sourceSubMeshIndex == i; });
		transform.world = edit == edited.end() ? root : MeshSubMeshRuntime::BuildRenderLocalMatrix(*edit) * root;
		transform.normal = Matrix4x4::Transpose(Matrix4x4::Inverse(transform.world));
		transform.previousWorld = !changed && i < previousTransforms_.size() ? previousTransforms_[i].world : transform.world;
		transform.enabled = source.subMeshes.empty() || std::find(source.subMeshes.begin(), source.subMeshes.end(), i) != source.subMeshes.end();
	}
	const bool weightsChanged = !SameWeights(previousTransforms_, transforms);
	subMeshes_.Upload(transforms);
	bindings_[3] = subMeshes_.GetGPUHandle();
	bindings_[0] = mesh->vertexSRV.srvGPUHandle;
	bindings_[1] = mesh->indexSRV.srvGPUHandle;
	bindings_[2] = mesh->vertexSubMeshIndexSRV.srvGPUHandle;
	bindings_[5] = mesh->vertexColorSRV.srvGPUHandle;
	currentVertices_ = mesh->vertexSRV.buffer->GetResource();
	currentVertexOffset_ = 0;
	currentSkinned_ = false;
	SkinnedVertexSource skinning;
	uint64_t pose = 0, buffer = 0;
	if (mesh->isSkinned) {
		if (!reference.renderer.IsValid() || !inputs.meshes->FindSkinnedVertexSource(&world, reference.renderer, reference.mesh, skinning)) {
			diagnostic_ = "発生元MeshのSkinningを準備中です";
			return false;
		}
		bindings_[0] = descriptors_->GetGPUHandle(skinning.srvIndex);
		settings_.vertexOffset = skinning.vertexOffset;
		currentVertices_ = skinning.resource;
		currentVertexOffset_ = uint64_t(skinning.vertexOffset) * sizeof(MeshVertex);
		currentSkinned_ = true;
		pose = skinning.poseGeneration;
		buffer = skinning.bufferGeneration;
	}
	const auto& lod = mesh->lods[0];
	ParticleMeshSamplingInput input;
	input.vertices = currentVertices_->GetGPUVirtualAddress() + currentVertexOffset_;
	input.indices = mesh->indexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	input.vertexSubMeshes = mesh->vertexSubMeshIndexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	input.subMeshes = subMeshes_.GetGPUAddress();
	input.vertexCount = mesh->vertexCount;
	input.indexOffset = lod.indexOffset;
	input.indexCount = lod.indexCount ? lod.indexCount : mesh->indexCount;
	input.subMeshCount = static_cast<uint32_t>(transforms.size());
	input.kind = source.kind;
	const uint32_t count = source.kind == ParticleSourceKind::MeshVertex ? mesh->vertexCount :
		(source.kind == ParticleSourceKind::MeshEdge ? input.indexCount : input.indexCount / 3);
	const bool rebuild = !sampling_ || count != samplingCount_ || source.kind != samplingKind_;
	if (rebuild) {
		auto candidate = std::make_unique<ParticleMeshSampling>();
		candidate->Init(graphics.GetDXObject().GetDevice(), *descriptors_, input);
		sampling_ = std::move(candidate);
		samplingCount_ = count;
		samplingKind_ = source.kind;
	}
	if (rebuild || weightsChanged || pose != pose_ || buffer != buffer_) {
		const auto* prepare = pipelines.GetORCreate(graphics.GetDXObject(), assets, BuiltinAssets::Pipelines::ParticleMeshSampling,
			PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
		const auto* scan = pipelines.GetORCreate(graphics.GetDXObject(), assets, BuiltinAssets::Pipelines::InclusiveScan,
			PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
		if (!prepare || !scan) { diagnostic_ = "Mesh発生分布のShaderを準備できません"; return false; }
		sampling_->Record(graphics.GetDXObject().GetDevice(), *graphics.GetDXObject().GetDxCommand(), *retirement_, *prepare, *scan, input, constants_);
	}
	bindings_[4] = sampling_->GetDistribution().GetSRVGPUHandle();
	settings_.candidateCount = count;
	settings_.vertexCount = mesh->vertexCount;
	settings_.indexOffset = input.indexOffset;
	settings_.imageSize.w = mesh->hasVertexColors ? 1.0f : 0.0f;
	if (mesh->hasVertexColors) settings_.flags |= 16u;
	const bool keepPrevious = source.attachToSurface || source.inheritVelocity != 0;
	if (keepPrevious) {
		previousVertices_.EnsureCapacity(mesh->vertexCount);
		previousVertices_.Transition(*graphics.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		bindings_[6] = previousVertices_.GetSRVGPUHandle();
		if (previousReady_) settings_.flags |= 64u;
	} else currentVertices_ = nullptr;
	previousTransforms_ = std::move(transforms);
	pose_ = pose;
	buffer_ = buffer;
	return true;
}
