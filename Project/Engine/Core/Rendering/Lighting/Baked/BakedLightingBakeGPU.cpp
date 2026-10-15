#include "BakedLightingBakeState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>

// c++
#include <algorithm>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace {

	using namespace Engine;
	// 空のライトBufferも有効な参照を持たせる
	template<typename T>
	void Upload(GraphicsCore& graphics, MeshStructuredHandle<T>& buffer, const std::vector<T>& values, const wchar_t* name) {

		const T empty{};
		buffer.Create(graphics.GetDXObject().GetDevice(), graphics.GetBufferUploadService(), graphics.GetSRVDescriptor(),
			values.empty() ? std::span<const T>(&empty, 1) : std::span<const T>(values), name);
	}
	// RootのBuffer参照を既存のBinderへ渡す
	template<typename T>
	ComputeBindItem ReadBinding(const char* name, const MeshStructuredHandle<T>& buffer) {

		return {name, ComputeBindValueType::SRV, buffer.buffer->GetResource()->GetGPUVirtualAddress(), buffer.srvGPUHandle};
	}
}

bool Engine::InitializeBakedLightingGPU(GraphicsCore& graphics, BakedLightingBakeState& state) {

	auto& platform = graphics.GetDXObject();
	if (!platform.GetFeatureController().GetSupport().SupportsInlineRayTracingPath()) {
		throw std::runtime_error("このGPUではハードウェア照明ベイクを使用できません");
	}
	state.materialResolver.ResetPending();
	std::vector<BakedLightingMaterialGPU> materials;
	for (const auto& source : state.input.preparation.materials) {
		const auto converted = state.materialResolver.BuildPrimitiveSubMeshData(graphics, *state.database,
			source.material, &source.instance, source.uvMatrix);
		BakedLightingMaterialGPU material;
		material.baseColor = converted.color;
		material.emissive = converted.emissiveColor;
		material.baseColorTexture = converted.baseColorTextureIndex;
		material.emissiveTexture = converted.emissiveTextureIndex;
		material.opacityTexture = converted.opacityTextureIndex;
		material.masked = source.surfaceMode == MaterialSurfaceMode::Masked ? 1u : 0u;
		material.metallic = converted.metallic;
		material.roughness = converted.roughness;
		material.alphaCutoff = source.alphaCutoff;
		material.metallicTexture = converted.metallicTextureIndex;
		material.metallicRoughnessTexture = converted.metallicRoughnessTextureIndex;
		material.roughnessTexture = converted.roughnessTextureIndex;
		material.normalTexture = converted.normalTextureIndex;
		const auto* opacity = source.instance.Find(MaterialParameterIDs::Opacity);
		if (!opacity) opacity = source.material.parameters.Find(MaterialParameterIDs::Opacity);
		if (opacity) {
			if (const auto* value = std::get_if<float>(&opacity->value)) material.opacity = std::clamp(*value, 0.0f, 1.0f);
		}
		materials.push_back(material);
	}
	bool skyPending = false;
	if (state.input.skyboxTexture) {
		const auto source = RuntimeTextureResolver::ResolveBindless(graphics, state.database.get(), state.input.skyboxTexture,
			TextureColorSpace::Linear);
		skyPending = source.retry;
		state.skyboxIndex = source.srvIndex;
		state.skyOwnership = source.ownership;
	}
	if (state.materialResolver.HasPendingTextures() || skyPending) return false;
	// 作業BufferとASの概算容量を確保前に確認する
	const auto& prepared = state.input.preparation;
	const uint64_t required = uint64_t(prepared.data.triangles.size()) * 384u +
		uint64_t(prepared.texels.size()) * 64u + uint64_t(prepared.data.reflectionPixels.size()) * 32u +
		uint64_t(prepared.data.probes.size()) * 768u + 16u * 1024u * 1024u;
	const auto adapter = DxUtils::FindDeviceAdapter(platform.GetDevice());
	DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
	if (required > 512u * 1024u * 1024u || (adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0,
		DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) && memory.Budget > 0 &&
		double(required) + double(memory.CurrentUsage) > double(memory.Budget) * 0.9)) {
		throw std::runtime_error("照明ベイクに必要なGPUメモリが不足しています Atlasまたは対象を減らしてください");
	}
	state.materialResolver.CommitTextures();
	ComputePipelineDesc description{};
	description.compute.file = "Builtin/Lighting/Baked/bakedLightingTrace.CS.hlsl";
	description.compute.shader = BuiltinAssets::Shaders::BakedLightingTrace;
	description.compute.entry = "main";
	description.compute.profile = "cs_6_6";
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	description.staticSamplers.push_back(sampler);
	state.pipeline = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
		platform.GetDxShaderCompiler(), description);
	if (!state.pipeline) throw std::runtime_error("照明ベイクのShaderを生成できません");
	state.retirement = &platform.GetResourceRetirement();
	state.constants.Init(*state.retirement, platform.GetDevice());
	state.lightCounts.Init(*state.retirement, platform.GetDevice());
	auto& data = state.input.preparation.data;
	std::vector<Vector3> positions;
	positions.reserve(data.triangles.size() * 3);
	for (const auto& triangle : data.triangles) {
		for (const auto& vertex : triangle.vertices) positions.push_back(vertex.position);
	}
	// 描画中のLODやCameraの範囲に依存しないASを作る
	Upload(graphics, state.triangles, data.triangles, L"BakeTriangles");
	Upload(graphics, state.positions, positions, L"BakePositions");
	std::vector<uint32_t> indices(positions.size());
	std::iota(indices.begin(), indices.end(), 0u);
	Upload(graphics, state.indices, indices, L"BakeIndices");
	Upload(graphics, state.materials, materials, L"BakeMaterials");
	Upload(graphics, state.texels, state.input.preparation.texels, L"BakeTexels");
	Upload(graphics, state.probes, data.probes, L"BakeProbes");
	Upload(graphics, state.captures, data.captures, L"BakeCaptures");
	Upload(graphics, state.directionalLights, state.input.directionalLights, L"BakeDirectionalLights");
	Upload(graphics, state.pointLights, state.input.pointLights, L"BakePointLights");
	Upload(graphics, state.spotLights, state.input.spotLights, L"BakeSpotLights");
	Upload(graphics, state.rectLights, state.input.rectLights, L"BakeRectLights");
	state.output.Init(platform.GetDevice(), &graphics.GetSRVDescriptor());
	state.output.EnsureCapacity(static_cast<uint32_t>(state.input.preparation.texels.size() + data.reflectionPixels.size()));
	state.probeOutput.Init(platform.GetDevice(), &graphics.GetSRVDescriptor());
	state.probeOutput.EnsureCapacity(static_cast<uint32_t>(data.probes.size()));
	RaytracingBLASGeometryInput geometry;
	geometry.vertexAddress = state.positions.buffer->GetResource()->GetGPUVirtualAddress();
	geometry.vertexCount = static_cast<uint32_t>(positions.size());
	geometry.vertexStride = sizeof(Vector3);
	geometry.indexAddress = state.indices.buffer->GetResource()->GetGPUVirtualAddress();
	geometry.indexCount = static_cast<uint32_t>(indices.size());
	geometry.flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
	state.blas.SetRetirementQueue(*state.retirement);
	state.tlas.SetRetirementQueue(*state.retirement);
	state.blas.Build(platform.GetDevice(), platform.GetDxCommand()->GetCommandList(), {{&geometry, 1}, false});
	state.tlas.Build(platform.GetDevice(), platform.GetDxCommand()->GetCommandList(),
		{{Matrix4x4::Identity(), state.blas.GetResource(), 0, 0, 0xFF}}, false);
	return true;
}

void Engine::DispatchBakedLightingGPU(GraphicsCore& graphics, BakedLightingBakeState& state) {

	auto& data = state.input.preparation.data;
	const std::array<uint32_t, 3> counts = {static_cast<uint32_t>(state.input.preparation.texels.size()),
		static_cast<uint32_t>(data.probes.size()), static_cast<uint32_t>(data.reflectionPixels.size())};
	while (state.stage < counts.size() && counts[state.stage] == 0u) ++state.stage;
	auto& platform = graphics.GetDXObject();
	auto* dxCommand = platform.GetDxCommand();
	auto* command = dxCommand->GetCommandList();
	if (state.stage == counts.size()) {
		// 提出済みFrameのFenceで読み戻しを判定する
		DxUtils::CreateReadbackBufferResource(platform.GetDevice(), state.readback,
			size_t(state.input.preparation.texels.size() + data.reflectionPixels.size()) * sizeof(Vector4));
		DxUtils::CreateReadbackBufferResource(platform.GetDevice(), state.probeReadback, data.probes.size() * sizeof(BakedLightingProbe));
		state.output.Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
		state.probeOutput.Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
		command->CopyBufferRegion(state.readback.Get(), 0, state.output.GetResource(), 0,
			size_t(state.input.preparation.texels.size() + data.reflectionPixels.size()) * sizeof(Vector4));
		command->CopyBufferRegion(state.probeReadback.Get(), 0, state.probeOutput.GetResource(), 0,
			data.probes.size() * sizeof(BakedLightingProbe));
		state.copyFrame = GraphicsFrameState::GetFrameSerial();
		state.copySlot = GraphicsFrameState::GetCurrentIndex();
		state.waitingReadback = true;
		return;
	}
	BakedLightingBakeConstants constants;
	constants.stage = state.stage;
	constants.firstTask = state.firstTask;
	constants.taskCount = std::min(state.settings.texelsPerFrame, counts[state.stage] - state.firstTask);
	constants.sampleStart = state.sampleStart;
	constants.samplesPerTask = std::min(4u, state.settings.sampleCount - state.sampleStart);
	constants.bounceCount = state.settings.bounceCount;
	constants.normalBias = state.settings.normalBias;
	constants.skyboxIndex = state.skyboxIndex;
	constants.skyColor = state.input.skyboxColor;
	constants.skyIntensity = state.input.skyIntensity;
	constants.lightmapCount = counts[0];
	constants.captureSize = state.settings.captureSize;
	constants.captureCount = static_cast<uint32_t>(data.captures.size());
	const Vector3 extent = data.nodes.front().maximum - data.nodes.front().minimum;
	constants.maximumDistance = std::max(Vector3::Length(extent) * 2.0f, 100.0f);
	state.constants.Upload(constants);
	LightCountsGPU lightCounts;
	lightCounts.directionalCount = static_cast<uint32_t>(state.input.directionalLights.size());
	lightCounts.pointCount = static_cast<uint32_t>(state.input.pointLights.size());
	lightCounts.spotCount = static_cast<uint32_t>(state.input.spotLights.size());
	lightCounts.rectCount = static_cast<uint32_t>(state.input.rectLights.size());
	state.lightCounts.Upload(lightCounts);
	dxCommand->SetDescriptorHeaps({graphics.GetSRVDescriptor().GetDescriptorHeap()});
	state.output.Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	state.probeOutput.Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	command->SetComputeRootSignature(state.pipeline->GetRootSignature());
	command->SetPipelineState(state.pipeline->GetComputePipeline());
	state.pipeline->BindGlobalDescriptorTablesCompute(command, graphics.GetSRVDescriptor().GetGPUHandle(0));
	const std::vector<ComputeBindItem> bindings = {
		{"BakedLightingBakeConstants", ComputeBindValueType::CBV, state.constants.GetGPUAddress()},
		{"LightCounts", ComputeBindValueType::CBV, state.lightCounts.GetGPUAddress()},
		ReadBinding("gBakeTriangles", state.triangles), ReadBinding("gBakeTexels", state.texels),
		ReadBinding("gBakeMaterials", state.materials), ReadBinding("gBakeProbes", state.probes),
		ReadBinding("gBakeCaptures", state.captures), ReadBinding("gDirectionalLights", state.directionalLights),
		ReadBinding("gPointLights", state.pointLights), ReadBinding("gSpotLights", state.spotLights), ReadBinding("gRectLights", state.rectLights),
		{"gBakeTLAS", ComputeBindValueType::AccelStruct, state.tlas.GetResource()->GetGPUVirtualAddress()},
		{"gBakeOutput", ComputeBindValueType::UAV, state.output.GetGPUAddress(), state.output.GetUAVGPUHandle()},
		{"gBakeProbeOutput", ComputeBindValueType::UAV, state.probeOutput.GetGPUAddress(), state.probeOutput.GetUAVGPUHandle()},
	};
	ComputeRootBinder{*state.pipeline}.Bind(command, bindings);
	command->Dispatch((constants.taskCount + 63u) / 64u, 1, 1);
	dxCommand->UAVBarrier(state.output.GetResource());
	dxCommand->UAVBarrier(state.probeOutput.GetResource());
	state.firstTask += constants.taskCount;
	if (state.firstTask == counts[state.stage]) {
		state.firstTask = 0;
		state.sampleStart += constants.samplesPerTask;
		if (state.sampleStart == state.settings.sampleCount) {
			state.sampleStart = 0;
			++state.stage;
		}
	}
}

bool Engine::ReadBakedLightingGPU(GraphicsCore& graphics, BakedLightingBakeState& state) {

	if (GraphicsFrameState::GetFrameSerial() == state.copyFrame) return false;
	auto& platform = graphics.GetDXObject();
	if (state.copyFence == UINT64_MAX) state.copyFence = platform.GetDxCommand()->GetFrameFenceValue(state.copySlot);
	if (platform.GetCommandQueue()->GetCompletedFenceValue() < state.copyFence) return false;
	auto& data = state.input.preparation.data;
	const size_t lightmapCount = state.input.preparation.texels.size();
	D3D12_RANGE readRange{0, (lightmapCount + data.reflectionPixels.size()) * sizeof(Vector4)};
	void* mapped = nullptr;
	HRESULT result = state.readback->Map(0, &readRange, &mapped);
	if (!DxDREDDiagnostics::CheckHRESULT(platform.GetDevice(), result, "BakedLighting::Readback")) throw std::runtime_error("ベイク結果を読み戻せません");
	data.lightmaps.resize(lightmapCount);
	std::memcpy(data.lightmaps.data(), mapped, lightmapCount * sizeof(Vector4));
	if (!data.reflectionPixels.empty()) std::memcpy(data.reflectionPixels.data(), static_cast<Vector4*>(mapped) + lightmapCount,
		data.reflectionPixels.size() * sizeof(Vector4));
	D3D12_RANGE empty{0, 0};
	state.readback->Unmap(0, &empty);
	for (auto& value : data.lightmaps) value /= float(state.settings.sampleCount);
	for (auto& value : data.reflectionPixels) value /= float(state.settings.sampleCount);
	readRange.End = data.probes.size() * sizeof(BakedLightingProbe);
	result = state.probeReadback->Map(0, &readRange, &mapped);
	if (!DxDREDDiagnostics::CheckHRESULT(platform.GetDevice(), result, "BakedLighting::ProbeReadback")) throw std::runtime_error("Probeを読み戻せません");
	std::memcpy(data.probes.data(), mapped, readRange.End);
	state.probeReadback->Unmap(0, &empty);
	for (auto& probe : data.probes) {
		float backfaces = 0.0f;
		for (uint32_t coefficient = 0; coefficient < 9; ++coefficient) {
			const float convolution = coefficient == 0 ? 1.0f : coefficient < 4 ? 2.0f / 3.0f : 0.25f;
			probe.irradiance[coefficient] *= 12.5663706144f * convolution / float(state.settings.sampleCount);
		}
		for (auto& visibility : probe.visibility) {
			backfaces += visibility.w;
			if (visibility.z > 0.0f) {
				const float count = visibility.z;
				visibility.x /= count;
				visibility.y = std::max(visibility.y / count - visibility.x * visibility.x, 0.0f);
				visibility.w /= count;
			} else {
				visibility.x = std::max(Vector3::Length(data.nodes.front().maximum - data.nodes.front().minimum) * 2.0f, 100.0f);
			}
		}
		// 壁の内部に置かれたProbeは補間から外す
		if (backfaces / float(state.settings.sampleCount) > 0.5f) probe.radius = 0.0f;
	}
	return true;
}
