#include "ClothGPUSolver.h"

//============================================================================
//	include
//============================================================================
#include "ClothGPUEnvironment.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameUploadBufferAllocator.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

using namespace Engine;

namespace {

	constexpr float kDt = 1.0f / 120.0f;
	constexpr uint32_t kThreads = 128;

	enum class Pass : uint32_t { Initialize, Predict, Constraints, Colliders, Commit, Publish, SmallGrid };

	struct Seed {
		Vector3 rest;
		uint32_t pinned;
		Vector3 initial, previous;
	};
	struct Link {
		uint32_t a, b, kind;
		float length;
	};
	struct Range { uint32_t first, count; };
	struct FrameConstants {
		Matrix4x4 world, inverse;
		Vector3 windDirection;
		float windStrength;
		Vector3 compliance;
		float gravity;
		float drag, gustStrength, thickness;
		uint32_t windEnabled;
		uint32_t nx, ny, axis, groups;
		uint32_t fields, colliders, windMask, collisionMask;
	};
	struct PassConstants {
		Pass pass;
		uint32_t first, count, iteration;
		float phase;
	};
	static_assert(sizeof(Seed) == 40 && sizeof(Link) == 16 && sizeof(FrameConstants) == 208);

	// 同じ色には端点を共有しない制約だけをまとめる
	void OrderLinks(const ClothRuntime& runtime, std::vector<Link>& links, std::vector<Range>& ranges) {

		std::vector<uint32_t> used(runtime.rest.size());
		std::vector<std::vector<Link>> groups;
		for (const auto& c : runtime.constraints) {
			const uint32_t color = std::countr_one(used[c.a] | used[c.b]);
			const uint32_t mask = 1u << color;
			used[c.a] |= mask;
			used[c.b] |= mask;
			if (groups.size() <= color) groups.resize(color + 1);
			groups[color].push_back({c.a, c.b, c.kind, c.restLength});
		}
		links.reserve(runtime.constraints.size());
		for (const auto& group : groups) {
			ranges.push_back({static_cast<uint32_t>(links.size()), static_cast<uint32_t>(group.size())});
			links.insert(links.end(), group.begin(), group.end());
		}
	}
}

struct ClothGPUSolver::Buffers {

	DxImmutableStructuredBuffer<Seed> seeds;
	DxImmutableStructuredBuffer<Link> links;
	DxImmutableStructuredBuffer<Range> groups;
	std::vector<Range> ranges;
	StructuredRWBuffer<Vector3> positions{"ClothPositions"}, previous{"ClothPrevious"}, predicted{"ClothPredicted"};
	StructuredRWBuffer<float> lambda{"ClothLambda"};
	StructuredRWBuffer<MeshVertex> vertices{"ClothVertices"};
	FrameUploadBufferAllocator uploads{8192};

	void Init(GraphicsCore& graphics, const ClothRuntime& runtime) {

		auto* device = graphics.GetDXObject().GetDevice();
		auto& transfer = graphics.GetBufferUploadService();
		std::vector<Seed> initial(runtime.rest.size());
		for (size_t i = 0; i < initial.size(); ++i) {
			initial[i] = {runtime.rest[i], runtime.pinned[i], runtime.positions[i], runtime.previous[i]};
		}
		std::vector<Link> ordered;
		OrderLinks(runtime, ordered, ranges);
		seeds.Create(device, transfer, initial);
		links.Create(device, transfer, ordered);
		groups.Create(device, transfer, ranges);
		transfer.SubmitBatch();

		const auto allocate = [&](auto& buffer, size_t count) {
			buffer.Init(device, &graphics.GetSRVDescriptor());
			buffer.EnsureCapacity(static_cast<uint32_t>(count));
		};
		allocate(positions, initial.size());
		allocate(previous, initial.size());
		allocate(vertices, initial.size());
		const bool sharedGrid = initial.size() <= 512 && ordered.size() <= 3072;
		allocate(predicted, sharedGrid ? 1 : initial.size());
		allocate(lambda, sharedGrid ? 1 : ordered.size());
	}
};

ClothGPUSolver::ClothGPUSolver() = default;
ClothGPUSolver::~ClothGPUSolver() = default;

ID3D12Resource* ClothGPUSolver::GetVertexResource() const {

	return buffers_ ? buffers_->vertices.GetResource() : nullptr;
}

uint32_t ClothGPUSolver::GetVertexDescriptorIndex() const {

	return buffers_ ? buffers_->vertices.GetSRVIndex() : UINT32_MAX;
}

bool ClothGPUSolver::Update(GraphicsCore& graphics, RenderAssetLibrary& assets, PipelineStateCache& pipelines,
	const ClothRuntime& runtime, const ClothSettings& settings, const PrimitivePlaneParams& plane,
	const Matrix4x4& world, const Matrix4x4& inverse, uint32_t steps, double startTime, const ClothGPUEnvironment& environment) {

	if (runtime.rest.empty() || runtime.constraints.empty()) return false;
	if (buffers_ && !steps) return true;
	auto& platform = graphics.GetDXObject();
	const auto* pipeline = pipelines.GetORCreate(platform, assets, BuiltinAssets::Pipelines::ClothSolve,
		PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipeline || !pipeline->GetComputePipeline() || !pipeline->GetRootSignature()) return false;
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	const bool initialize = !buffers_;
	if (initialize) {
		auto buffers = std::make_unique<Buffers>();
		buffers->Init(graphics, runtime);
		buffers_ = std::move(buffers);
	}
	auto& b = *buffers_;
	auto& retirement = platform.GetResourceRetirement();
	auto* device = platform.GetDevice();
	const auto cb = [&](const auto& value) {
		return b.uploads.AllocateAndUpload(retirement, device, value).gpuAddress;
	};

	const uint32_t n = static_cast<uint32_t>(runtime.rest.size());
	FrameConstants frame{
		world, inverse, Vector3::NormalizeOr(settings.windDirection, {}), settings.windStrength,
		Vector3(settings.stretchCompliance, settings.shearCompliance, settings.bendCompliance) / (kDt * kDt),
		-9.81f * settings.gravityScale, std::exp(-settings.damping * kDt), settings.gustStrength, settings.thickness,
		static_cast<uint32_t>(settings.windEnabled), static_cast<uint32_t>(std::clamp(plane.divideX, 1, kMaxPrimitiveDivide)),
		static_cast<uint32_t>(std::clamp(plane.divideY, 1, kMaxPrimitiveDivide)), static_cast<uint32_t>(plane.axis),
		static_cast<uint32_t>(b.ranges.size()), static_cast<uint32_t>(environment.fields.size()),
		static_cast<uint32_t>(environment.colliders.size()), settings.receiveSceneWind ? settings.windCollisionMask : 0u, settings.collisionMask
	};
	list->SetComputeRootSignature(pipeline->GetRootSignature());
	list->SetPipelineState(pipeline->GetComputePipeline());
	const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
		RootBindingCommand::SetComputeSRV(list, pipeline->FindBindingByName(name, ShaderBindingKind::SRV), address);
	};
	const auto uav = [&](const char* name, auto& buffer) {
		buffer.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		RootBindingCommand::SetComputeUAV(list, pipeline->FindBindingByName(name, ShaderBindingKind::UAV),
			buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
	};
	const auto frameAddress = cb(frame);
	const auto inputs = environment.GetGPUAddresses(graphics, b.uploads);
	srv("gClothSeeds", b.seeds.GetResource()->GetGPUVirtualAddress());
	srv("gClothLinks", b.links.GetResource()->GetGPUVirtualAddress());
	srv("gClothGroups", b.groups.GetResource()->GetGPUVirtualAddress());
	srv("gClothFields", inputs.fields ? inputs.fields : frameAddress);
	srv("gClothSamples", inputs.samples ? inputs.samples : frameAddress);
	srv("gClothColliders", inputs.colliders ? inputs.colliders : frameAddress);
	uav("gClothPositions", b.positions);
	uav("gClothPrevious", b.previous);
	uav("gClothPredicted", b.predicted);
	uav("gClothLambda", b.lambda);
	uav("gClothVertices", b.vertices);
	RootBindingCommand::SetComputeCBV(list, pipeline->FindBindingByName("ClothFrame", ShaderBindingKind::CBV), frameAddress);
	const auto* passBinding = pipeline->FindBindingByName("ClothPass", ShaderBindingKind::CBV);
	PassConstants pass{};
	const auto dispatch = [&](Pass stage, uint32_t first, uint32_t count) {
		pass.pass = stage;
		pass.first = first;
		pass.count = count;
		RootBindingCommand::SetComputeCBV(list, passBinding, cb(pass));
		list->Dispatch((count + kThreads - 1) / kThreads, 1, 1);
		commands.UAVBarrierAll();
	};
	if (initialize) dispatch(Pass::Initialize, 0, n);
	for (uint32_t step = 0; step < steps; ++step) {
		pass.phase = static_cast<float>((startTime + (step + 1) / 120.0) * settings.gustFrequency * 2.0 * std::numbers::pi);
		if (n <= 512 && runtime.constraints.size() <= 3072) {
			dispatch(Pass::SmallGrid, 0, kThreads);
			continue;
		}
		dispatch(Pass::Predict, 0, n);
		for (pass.iteration = 0; pass.iteration < 6; ++pass.iteration) {
			for (const auto& range : b.ranges) dispatch(Pass::Constraints, range.first, range.count);
			if (frame.colliders && frame.collisionMask) dispatch(Pass::Colliders, 0, n);
		}
		dispatch(Pass::Commit, 0, n);
	}
	dispatch(Pass::Publish, 0, n);
	b.vertices.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	return true;
}
