#include "AtmosphereEnvironment.h"
#include "AtmosphereLighting.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/EnvironmentSpecularMap.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	constexpr auto kReadState = static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	struct AtmosphereParameters {

		Engine::Vector4 rayleigh{};
		Engine::Vector4 mie{};
		Engine::Vector4 ozone{};
		Engine::Vector4 ground{};
		Engine::Vector4 planet{};
		Engine::Vector4 sun{};
		Engine::Vector4 solar{};
	};
	struct AtmosphereConstants {

		AtmosphereParameters parameters{};
		std::array<uint32_t, 4> indices{};
		Engine::Vector4 cameraPositionDistance{};
		Engine::Matrix4x4 inverseViewProjection = Engine::Matrix4x4::Identity();
	};
	struct LookupTexture {

		ComPtr<ID3D12Resource> resource;
		Engine::SRVDescriptor* descriptor = nullptr;
		uint32_t srv = UINT32_MAX;
		uint32_t uav = UINT32_MAX;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		~LookupTexture() {

			if (srv != UINT32_MAX) descriptor->Retire(srv, resource);
			if (uav != UINT32_MAX) descriptor->Retire(uav, {});
		}
		void Create(Engine::GraphicsCore& graphics, uint32_t width, uint32_t height, bool cube) {

			if (resource) return;
			descriptor = &graphics.GetSRVDescriptor();
			auto* device = graphics.GetDXObject().GetDevice();
			auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height,
				cube ? 6 : 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
			const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
				state, nullptr, IID_PPV_ARGS(&resource));
			if (!Engine::DxDREDDiagnostics::CheckHRESULT(device, result, "AtmosphereEnvironment::Create")) {
				throw std::runtime_error("大気描画のテクスチャを生成できません");
			}
			resource->SetName(L"AtmosphereLookup");
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = desc.Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = cube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
			if (cube) srvDesc.TextureCube.MipLevels = 1;
			else srvDesc.Texture2D.MipLevels = 1;
			descriptor->CreateSRV(srv, resource.Get(), srvDesc);
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = cube ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
			if (cube) uavDesc.Texture2DArray.ArraySize = 6;
			descriptor->CreateUAV(uav, resource.Get(), uavDesc);
		}
	};

	AtmosphereParameters ResolveParameters(Engine::ECSWorld& world, Engine::Entity owner,
		const Engine::SkyAtmosphereComponent& source) {

		const auto sky = Engine::AtmosphereLighting::Sanitize(source);

		AtmosphereParameters result{};
		result.rayleigh = {sky.rayleighScattering.r, sky.rayleighScattering.g, sky.rayleighScattering.b, sky.rayleighScaleHeightKm};
		result.mie = {sky.mieScattering, sky.mieAbsorption, sky.mieScaleHeightKm, sky.mieAnisotropy};
		result.ozone = {sky.ozoneAbsorption.r, sky.ozoneAbsorption.g, sky.ozoneAbsorption.b, sky.ozoneCenterHeightKm};
		result.ground = {sky.groundAlbedo.r, sky.groundAlbedo.g, sky.groundAlbedo.b, sky.ozoneWidthKm};
		result.planet = {sky.planetRadiusKm, sky.atmosphereHeightKm, Engine::LightExtract::GetWorldPos(world, owner).y, 0};
		const Engine::Entity sun = Engine::AtmosphereLighting::ResolveSun(world, owner, sky);
		result.sun = {0, 1, 0, 0.004712389f};
		if (world.IsAlive(sun) && world.HasComponent<Engine::DirectionalLightComponent>(sun)) {
			const auto& light = world.GetComponent<Engine::DirectionalLightComponent>(sun);
			if (Engine::LightExtract::IsVisible(world, sun, light.enabled)) {
				auto direction = Engine::LightExtract::GetWorldDirection(light.direction,
					Engine::LightExtract::GetWorldMatrix(world, sun)) * -1.0f;
				if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) direction = {0, 1, 0};
				result.sun = {direction.x, direction.y, direction.z,
					(std::isfinite(light.shadowAngularRadius) ? std::clamp(light.shadowAngularRadius, 0.01f, 45.0f) : 0.27f) * 0.0174532925f};
				result.solar = {light.color.r * light.intensity, light.color.g * light.intensity,
					light.color.b * light.intensity, 0};
			}
		}
		for (float* channel : {&result.solar.x, &result.solar.y, &result.solar.z}) {
			*channel = std::isfinite(*channel) ? std::clamp(*channel, 0.0f, 100000.0f) : 0.0f;
		}
		if (!std::isfinite(result.planet.z)) result.planet.z = 0.0f;
		return result;
	}
}

struct Engine::AtmosphereEnvironment::State {

	struct View {

		std::string key;
		LookupTexture aerial;
		LookupTexture sky;
		AtmosphereConstants previous{};
		bool valid = false;
		uint64_t used = 0;
	};
	struct Environment {

		std::weak_ptr<const ECSWorldLifetime> lifetime;
		UUID entity{};
		AssetID source = AssetID::New();
		TextureOwnership ownership = std::make_shared<const uint8_t>(0);
		LookupTexture transmittance;
		LookupTexture multiple;
		LookupTexture sky;
		LookupTexture cube;
		LookupTexture background;
		AtmosphereConstants constants{};
		uint64_t revision = 0;
		uint64_t used = 0;
		std::vector<std::unique_ptr<View>> views;
	};
	std::vector<std::unique_ptr<Environment>> environments;
	std::array<std::unique_ptr<PipelineState>, 5> pipelines;
	ViewConstantBuffer<AtmosphereConstants> constants;
	PipelineBindingCache bindings;
	PipelineBindingCache::SlotID constantSlot;
	PipelineBindingCache::SlotID outputSlot;
	PipelineBindingCache::SlotID cubeSlot;

	State() {

		constantSlot = bindings.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
		outputSlot = bindings.AddSlotByRegister(ShaderBindingKind::UAV, 0, 0);
		cubeSlot = bindings.AddSlotByRegister(ShaderBindingKind::UAV, 1, 0);
	}
	bool Initialize(GraphicsCore& graphics) {

		constexpr AssetID shaders[] = {BuiltinAssets::Shaders::AtmosphereTransmittance, BuiltinAssets::Shaders::AtmosphereMultiScattering,
			BuiltinAssets::Shaders::AtmosphereSkyView, BuiltinAssets::Shaders::AtmosphereCubemap, BuiltinAssets::Shaders::AtmosphereAerial};
		constexpr const char* entries[] = {"TransmittanceMain", "MultiScatteringMain", "SkyViewMain", "CubemapMain", "AerialMain"};
		auto& platform = graphics.GetDXObject();
		for (size_t i = 0; i < pipelines.size(); ++i) {
			if (pipelines[i]) continue;
			ComputePipelineDesc desc{};
			desc.compute.file = "Builtin/Atmosphere/atmosphereLookup.CS.hlsl";
			desc.compute.entry = entries[i];
			desc.compute.shader = shaders[i];
			desc.compute.profile = "cs_6_0";
			D3D12_STATIC_SAMPLER_DESC sampler{};
			sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			desc.staticSamplers.push_back(sampler);
			pipelines[i] = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
				platform.GetDxShaderCompiler(), desc);
			if (!pipelines[i]) return false;
		}
		constants.Init(platform.GetResourceRetirement(), platform.GetDevice());
		return true;
	}
	void Dispatch(GraphicsCore& graphics, uint32_t kernel, LookupTexture& output,
		const AtmosphereConstants& data, uint32_t width, uint32_t height, uint32_t depth = 1) {

		auto* dxCommand = graphics.GetDXObject().GetDxCommand();
		if (output.state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			dxCommand->TransitionBarriers({output.resource.Get()}, output.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		auto* command = dxCommand->GetCommandList();
		auto& pipeline = *pipelines[kernel];
		constants.Upload(data);
		command->SetComputeRootSignature(pipeline.GetRootSignature());
		command->SetPipelineState(pipeline.GetComputePipeline());
		pipeline.BindGlobalDescriptorTablesCompute(command, graphics.GetSRVDescriptor().GetGPUHandle(0));
		bindings.Sync(pipeline);
		RootBindingCommand::SetComputeCBV(command, bindings.Get(constantSlot), constants.GetGPUAddress());
		RootBindingCommand::SetComputeUAV(command, bindings.Get(kernel == 3 ? cubeSlot : outputSlot), 0,
			graphics.GetSRVDescriptor().GetGPUHandle(output.uav));
		command->Dispatch((width + 7) / 8, (height + 7) / 8, depth);
		dxCommand->TransitionBarriers({output.resource.Get()}, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kReadState);
		output.state = kReadState;
	}
	Environment* Prepare(GraphicsCore& graphics, ECSWorld& world, uint32_t mask,
		const SkyAtmosphereComponent*& selected) {

		Entity owner{};
		selected = AtmosphereLighting::Find(world, mask, owner);
		if (!selected || !graphics.GetDXObject().GetDxCommand()->IsRecording() || !Initialize(graphics)) return nullptr;
		const auto lifetime = world.GetLifetime();
		const UUID id = world.GetUUID(owner);
		Environment* environment = nullptr;
		for (auto& entry : environments) {
			if (entry->lifetime.lock() == lifetime && entry->entity == id) environment = entry.get();
		}
		if (!environment) {
			auto entry = std::make_unique<Environment>();
			entry->lifetime = lifetime;
			entry->entity = id;
			entry->transmittance.Create(graphics, 256, 64, false);
			entry->multiple.Create(graphics, 32, 32, false);
			entry->sky.Create(graphics, 200, 100, false);
			entry->cube.Create(graphics, 128, 128, true);
			entry->background.Create(graphics, 128, 128, true);
			environment = entry.get();
			environments.push_back(std::move(entry));
		}
		auto& entry = *environment;
		entry.used = GraphicsFrameState::GetFrameSerial();
		const auto parameters = ResolveParameters(world, owner, *selected);
		const bool mediumChanged = entry.revision == 0 || std::memcmp(&parameters, &entry.constants.parameters,
			offsetof(AtmosphereParameters, sun)) != 0;
		const bool changed = entry.revision == 0 || std::memcmp(&parameters, &entry.constants.parameters, sizeof(parameters)) != 0;
		if (changed) {
			entry.constants.parameters = parameters;
			entry.constants.indices = {entry.transmittance.srv, entry.multiple.srv, entry.sky.srv, 0};
			if (mediumChanged) {
				Dispatch(graphics, 0, entry.transmittance, entry.constants, 256, 64);
				Dispatch(graphics, 1, entry.multiple, entry.constants, 32, 32);
			}
			Dispatch(graphics, 2, entry.sky, entry.constants, 200, 100);
			Dispatch(graphics, 3, entry.cube, entry.constants, 128, 128, 6);
			entry.constants.indices[3] = 1;
			Dispatch(graphics, 3, entry.background, entry.constants, 128, 128, 6);
			entry.constants.indices[3] = 0;
			++entry.revision;
		}
		return environment;
	}
};

//============================================================================
//	AtmosphereEnvironment classMethods
//============================================================================
Engine::AtmosphereEnvironment::AtmosphereEnvironment() : state_(std::make_unique<State>()) {}
Engine::AtmosphereEnvironment::~AtmosphereEnvironment() = default;

Engine::SceneSkyboxInfo Engine::AtmosphereEnvironment::Resolve(GraphicsCore& graphics, ECSWorld& world, uint32_t mask) {

	const SkyAtmosphereComponent* sky = nullptr;
	auto* entry = state_->Prepare(graphics, world, mask, sky);
	if (!entry) return {};
	auto& environment = graphics.GetEnvironmentSpecularMap();
	environment.Update(graphics, entry->source, entry->cube.srv, entry->ownership, entry->revision);
	SceneSkyboxInfo result{};
	result.found = true;
	result.cubemapIndex = entry->background.srv;
	result.cubemapAssetID = entry->source;
	result.physicalEnvironmentEnabled = true;
	result.iblIntensity = AtmosphereLighting::Sanitize(*sky).iblIntensity;
	result.specularIndex = environment.GetCubemapIndex();
	result.irradianceIndex = environment.GetIrradianceIndex();
	result.brdfIndex = environment.GetBRDFIndex();
	return result;
}

Engine::AtmosphereViewData Engine::AtmosphereEnvironment::PrepareView(GraphicsCore& graphics, ECSWorld& world,
	uint32_t mask, std::string_view viewKey, const Vector3& position, const Matrix4x4& inverseViewProjection) {

	const SkyAtmosphereComponent* sky = nullptr;
	auto* entry = state_->Prepare(graphics, world, mask, sky);
	if (!entry) return {};
	const auto settings = AtmosphereLighting::Sanitize(*sky);
	State::View* view = nullptr;
	for (auto& candidate : entry->views) if (candidate->key == viewKey) view = candidate.get();
	if (!view) {
		auto candidate = std::make_unique<State::View>();
		candidate->key = viewKey;
		candidate->aerial.Create(graphics, 32, 1024, false);
		candidate->sky.Create(graphics, 200, 100, false);
		view = candidate.get();
		entry->views.push_back(std::move(candidate));
	}
	view->used = GraphicsFrameState::GetFrameSerial();
	AtmosphereConstants data = entry->constants;
	data.cameraPositionDistance = {position.x, position.y, position.z, settings.aerialPerspectiveDistance};
	data.inverseViewProjection = inverseViewProjection;
	data.parameters.planet.w = std::max((position.y - data.parameters.planet.z) * 0.001f, 0.002f);
	if (!view->valid || std::memcmp(&view->previous, &data, sizeof(data)) != 0) {
		state_->Dispatch(graphics, 4, view->aerial, data, 32, 1024);
		if (!view->valid || std::memcmp(&view->previous.parameters, &data.parameters, sizeof(data.parameters)) != 0) {
			state_->Dispatch(graphics, 2, view->sky, data, 200, 100);
		}
		view->previous = data;
		view->valid = true;
	}
	AtmosphereViewData result{view->aerial.srv, settings.aerialPerspectiveDistance, settings.aerialPerspectiveStrength};
	const auto& sun = data.parameters.sun;
	const auto& solar = data.parameters.solar;
	result.sunDirection = {-sun.x, -sun.y, -sun.z};
	result.sunAvailable = solar.x > 0.0f || solar.y > 0.0f || solar.z > 0.0f;
	result.skyIndex = view->sky.srv;
	result.sunAngularRadius = sun.w;
	const Vector3 transmission = AtmosphereLighting::SunTransmittance(*sky, data.parameters.planet.w,
		Vector3(sun.x, sun.y, sun.z));
	const float solidAngle = 3.14159265f * sun.w * sun.w;
	result.solarLuminance = {solar.x * transmission.x / solidAngle,
		solar.y * transmission.y / solidAngle, solar.z * transmission.z / solidAngle};
	return result;
}

void Engine::AtmosphereEnvironment::CollectUnused() {

	const uint64_t frame = GraphicsFrameState::GetFrameSerial();
	for (auto& entry : state_->environments) {
		std::erase_if(entry->views, [frame](const auto& view) { return HasExpiredGraphicsResource(view->used, frame); });
	}
	std::erase_if(state_->environments, [frame](const auto& entry) {
		return entry->lifetime.expired() || HasExpiredGraphicsResource(entry->used, frame);
	});
}
