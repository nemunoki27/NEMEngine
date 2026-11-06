#include "GPUBufferLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/AntialiasingPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/BlitToViewPass.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

// c++
#include <array>
#include <stdexcept>
#include <vector>

namespace {

	// GPUへ提出しない生成FixtureのDescriptorを回収する
	struct TargetCreationFixture {

		Engine::RTVDescriptor targets;
		Engine::DSVDescriptor depths;
		Engine::SRVDescriptor shaders;
		Engine::GraphicsResourceRetirement retirement;
		uint64_t serial = 0;

		explicit TargetCreationFixture(ID3D12Device* device) {

			targets.Init(device, {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
			depths.Init(device, {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
			shaders.Init(device, {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE});
			targets.SetRetirementQueue(retirement);
			depths.SetRetirementQueue(retirement);
			shaders.SetRetirementQueue(retirement);
		}
		~TargetCreationFixture() {

			Drain();
		}
		void Drain() {

			retirement.Seal(++serial);
			retirement.Collect(serial);
		}
		bool IsEmpty() const {

			return targets.GetUseDescriptorCount() == 0 && depths.GetUseDescriptorCount() == 0 &&
				shaders.GetUseDescriptorCount() == 0 && retirement.GetPendingCount() == 0;
		}
	};
	// 指定数だけ空きを残して生成を途中で失敗させる
	struct DescriptorOccupancy {

		Engine::BaseDescriptor& descriptor;
		std::vector<uint32_t> indices;

		DescriptorOccupancy(Engine::BaseDescriptor& target, uint32_t remaining) : descriptor(target) {

			uint32_t available = target.GetMaxDescriptorCount() - target.GetUseDescriptorCount();
			indices.reserve(available);
			for (uint32_t index = remaining; index < available; ++index) {
				indices.push_back(descriptor.Allocate());
			}
		}
		~DescriptorOccupancy() {

			for (uint32_t index : indices) {
				descriptor.Free(index);
			}
		}
	};

	std::array<ID3D12Resource*, 19> CaptureResources(Engine::RenderPathResources& resources) {

		std::array<ID3D12Resource*, 19> result{};
		for (uint32_t index = 0; index < static_cast<uint32_t>(Engine::GBufferAttachment::Count); ++index) {
			result[index] = resources.GetSceneMain()->GetColorTexture(index)->GetResource();
		}
		result[7] = resources.GetSceneMain()->GetDepthTexture()->GetResource();
		result[8] = resources.GetSceneFinal()->GetColorTexture(0)->GetResource();
		result[9] = resources.GetSceneColorOpaque()->GetColorTexture(0)->GetResource();
		result[10] = resources.GetDepthPyramid().GetResource();
		auto captureOutline = [&](Engine::ScreenSpaceOutlineViewResources& outline, uint32_t offset) {

			result[offset] = outline.GetMask()->GetColorTexture(0)->GetResource();
			result[offset + 1] = outline.GetProjectedCoverageMask()->GetColorTexture(0)->GetResource();
			result[offset + 2] = outline.GetHorizontalDilatedMask()->GetColorTexture(0)->GetResource();
			result[offset + 3] = outline.GetDilatedMask()->GetColorTexture(0)->GetResource();
		};
		captureOutline(resources.GetRuntimeScreenSpaceOutline(), 11);
		captureOutline(resources.GetEditorSelectionScreenSpaceOutline(), 15);
		return result;
	}

	bool CheckRegistryReplacement(ID3D12Device* device) {

		TargetCreationFixture fixture(device);
		Engine::RenderTargetCreationContext context{device, fixture.targets, fixture.depths, fixture.shaders};
		Engine::RenderTargetRegistry registry;
		Engine::SceneRenderTargetDesc desc{};
		desc.name = "Transactional";
		desc.withDepth = true;
		desc.colors.emplace_back();
		desc.colors.back().name = "OriginalColor";
		auto* original = registry.ResizeTransient(context, desc, 4, 4);
		bool valid = original && registry.FindColorByName("OriginalColor") == original->GetColorTexture(0);
		desc.colors.back().name = "ReplacementColor";
		{
			DescriptorOccupancy occupied(fixture.depths, 0);
			bool rejected = false;
			try {
				registry.ResizeTransient(context, desc, 8, 4);
			} catch (const std::length_error&) {
				rejected = true;
			}
			// 部分生成後も旧サイズと名前の対応を維持する
			valid &= rejected && registry.Find(desc.name) == original && original->GetWidth() == 4 &&
				registry.FindColorByName("OriginalColor") == original->GetColorTexture(0) &&
				registry.FindColorByName("ReplacementColor") == nullptr;
		}
		fixture.Drain();
		auto* replacement = registry.ResizeTransient(context, desc, 8, 4);
		valid &= replacement != original && replacement->GetWidth() == 8 &&
			registry.FindColorByName("OriginalColor") == nullptr &&
			registry.FindColorByName("ReplacementColor") == replacement->GetColorTexture(0);
		Engine::SceneRenderTargetDesc resolvedDesc = desc;
		resolvedDesc.name = "Resolved";
		auto* resolved = registry.ResizeTransient(context, resolvedDesc, 16, 8);
		registry.Register("SceneMain", replacement, {}, "SceneDepth");
		registry.Register("ResolvedDepth", resolved, {}, "SceneDepth");
		valid &= registry.FindDepthByName("SceneDepth") == resolved->GetDepthTexture();
		registry.BeginFrame();
		valid &= registry.Find(desc.name) == nullptr && registry.ResizeTransient(context, desc, 8, 4) == replacement &&
			registry.Find(desc.name) == replacement;
		// 同じ実寸法と形式なら別名やサイズ指定方式で資源を作り直さない
		desc.sizeMode = Engine::SceneRenderTargetSizeMode::Fixed;
		desc.fixedWidth = 8;
		desc.fixedHeight = 4;
		desc.colors.front().name = "RenamedColor";
		valid &= registry.ResizeTransient(context, desc, 16, 8) == replacement &&
			registry.FindColorByName("RenamedColor") == replacement->GetColorTexture(0) &&
			registry.FindColorByName("ReplacementColor") == nullptr;
		registry.Clear();
		fixture.Drain();
		return valid && fixture.IsEmpty();
	}

	bool CheckRegistryEviction(ID3D12Device* device) {
		TargetCreationFixture fixture(device);
		Engine::RenderTargetCreationContext context{device, fixture.targets, fixture.depths, fixture.shaders};
		Engine::RenderTargetRegistry registry;
		Engine::SceneRenderTargetDesc active{};
		active.name = "Active";
		active.colors.emplace_back();
		active.colors.back().name = "ActiveColor";
		Engine::SceneRenderTargetDesc stale = active;
		stale.name = "Stale";
		stale.colors.back().name = "StaleColor";
		bool valid = registry.ResizeTransient(context, active, 4, 4) != nullptr &&
			registry.ResizeTransient(context, stale, 4, 4) != nullptr;
		valid &= fixture.targets.GetUseDescriptorCount() == 2 && fixture.shaders.GetUseDescriptorCount() == 2;
		for (uint64_t frame = 0; frame < Engine::kGraphicsResourceReuseFrames; ++frame) {
			Engine::GraphicsFrameState::BeginFrame(0);
			registry.BeginFrame();
			valid &= registry.ResizeTransient(context, active, 4, 4) != nullptr;
			registry.EndFrame();
		}
		fixture.Drain();
		valid &= fixture.targets.GetUseDescriptorCount() == 1 && fixture.shaders.GetUseDescriptorCount() == 1 &&
			registry.FindColorByName("StaleColor") == nullptr &&
			registry.FindColorByName("ActiveColor") != nullptr;
		// 長期間非表示だったViewも再使用の判定を済ませてから回収する
		auto* resumed = registry.Find(active.name);
		for (uint64_t frame = 0; frame <= Engine::kGraphicsResourceReuseFrames; ++frame) {
			Engine::GraphicsFrameState::BeginFrame(0);
		}
		registry.BeginFrame();
		valid &= registry.ResizeTransient(context, active, 4, 4) == resumed;
		registry.EndFrame();
		registry.Clear();
		fixture.Drain();
		return valid && fixture.IsEmpty();
	}

	bool CheckRenderPathReplacement(ID3D12Device* device) {

		TargetCreationFixture fixture(device);
		Engine::RenderTargetCreationContext context{device, fixture.targets, fixture.depths, fixture.shaders};
		Engine::RenderPathResources resources;
		resources.Resize(context, 4, 4);
		// 必要になるまでScene入力とOutlineの描画先を確保しない
		bool valid = resources.IsValid() && !resources.GetRuntimeScreenSpaceOutline().IsValid() &&
			!resources.GetEditorSelectionScreenSpaceOutline().IsValid() &&
			!resources.GetSceneColorOpaque() && !resources.GetDepthPyramid().IsValid();
		resources.Resize(context, 4, 4);
		valid &= !resources.GetSceneColorOpaque() && !resources.GetDepthPyramid().IsValid();
		{
			DescriptorOccupancy occupied(fixture.targets, 0);
			bool rejected = false;
			try {
				resources.PrepareSceneColorOpaque(context);
			} catch (const std::length_error&) {
				rejected = true;
			}
			valid &= rejected && resources.IsValid() && !resources.GetSceneColorOpaque();
		}
		fixture.Drain();
		auto* opaque = resources.PrepareSceneColorOpaque(context);
		resources.PrepareDepthPyramid(context);
		valid &= opaque && opaque->IsValid() && resources.GetDepthPyramid().IsValid();
		const auto descriptors = fixture.shaders.GetUseDescriptorCount();
		valid &= resources.PrepareSceneColorOpaque(context) == opaque;
		resources.PrepareDepthPyramid(context);
		valid &= fixture.shaders.GetUseDescriptorCount() == descriptors;
		resources.PrepareScreenSpaceOutline(context, false);
		resources.PrepareScreenSpaceOutline(context, true);
		const auto original = CaptureResources(resources);
		auto* pyramid = &resources.GetDepthPyramid();
		resources.GetDepthPyramid().MarkBuilt(37);
		valid &= resources.IsValid();
		{
			// 最後のEditor Outlineまで生成してから不足させる
			DescriptorOccupancy occupied(fixture.targets, 16);
			bool rejected = false;
			try {
				resources.Resize(context, 8, 4);
			} catch (const std::length_error&) {
				rejected = true;
			}
			valid &= rejected && CaptureResources(resources) == original && resources.IsValid() &&
				resources.GetSceneMain()->GetWidth() == 4 && resources.GetDepthPyramid().IsBuiltForFrame(37);
		}
		fixture.Drain();
		// サイズも旧値のままなら同じ指定で再生成しない
		resources.Resize(context, 4, 4);
		valid &= CaptureResources(resources) == original;
		// 色精度だけの変更で深度や履歴用の描画先を再生成しない
		resources.Resize(context, 4, 4, DXGI_FORMAT_R16G16B16A16_FLOAT);
		const auto precisionChanged = CaptureResources(resources);
		valid &= precisionChanged[0] != original[0];
		for (size_t index = 1; index < original.size(); ++index) {
			valid &= precisionChanged[index] == original[index];
		}
		resources.Resize(context, 8, 4);
		valid &= resources.IsValid() && CaptureResources(resources) != original &&
			resources.GetSceneMain()->GetWidth() == 8 && resources.GetSceneFinal()->GetWidth() == 8 &&
			resources.GetDepthPyramid().GetWidth() == 8 && &resources.GetDepthPyramid() == pyramid &&
			!resources.GetDepthPyramid().IsBuiltForFrame(37) && fixture.retirement.GetPendingCount() > 0;
		resources.Destroy();
		fixture.Drain();
		return valid && fixture.IsEmpty();
	}

	bool CheckAntialiasingTargets(ID3D12Device* device) {

		using namespace Engine;
		TargetCreationFixture fixture(device);
		GraphicsFeaturePreferences preferences;
		preferences.antialiasing = AntialiasingMethod::FXAA;
		SceneExecutionContext context;
		RenderTargetRegistry registry;
		context.targetRegistry = &registry;
		if (context.antialiasingSource || AntialiasingPass::CanRenderToView(preferences, context)) return false;
		bool valid = true;
		for (DXGI_FORMAT format : {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R8G8B8A8_UNORM}) {
			D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;
			for (bool createUAV : {false, true}) {
				if (createUAV && ((support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) == 0 ||
					(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0)) continue;
				MultiRenderTarget surface;
				MultiRenderTargetCreateDesc desc;
				desc.width = 17;
				desc.height = 13;
				desc.colors.push_back({.name = "View", .format = format, .createUAV = createUAV});
				surface.Create(device, &fixture.targets, &fixture.depths, &fixture.shaders, desc);
				context.defaultSurface = &surface;
				registry.Register("View", &surface, {"View"}, std::nullopt);
				const bool supported = createUAV && format != DXGI_FORMAT_R8G8B8A8_UNORM;
				valid &= AntialiasingPass::CanRenderToView(preferences, context) == supported;
				context.useViewportRect = true;
				context.viewportX = 3;
				context.viewportY = 2;
				context.viewportWidth = 7;
				context.viewportHeight = 5;
				valid &= AntialiasingPass::CanRenderToView(preferences, context) == supported;
				preferences.antialiasing = AntialiasingMethod::None;
				valid &= !AntialiasingPass::CanRenderToView(preferences, context);
				preferences.antialiasing = AntialiasingMethod::FXAA;
				preferences.fxaa.strength = 0.0f;
				valid &= !AntialiasingPass::CanRenderToView(preferences, context);
				preferences.fxaa.strength = 1.0f;
				preferences.fxaa.subpixelStrength = 0.0f;
				valid &= !AntialiasingPass::CanRenderToView(preferences, context);
				preferences.fxaa.subpixelStrength = 1.0f;
				// 次のCameraが描画できなくても古い入力を持ち越さない
				context.antialiasingSource = &surface;
				RenderPipelineDeps deps;
				GraphicsCore graphics;
				BlitToViewPass(deps).Execute(graphics, {}, context);
				valid &= context.antialiasingSource == nullptr;
				context.useViewportRect = false;
				registry.Clear();
				valid &= !AntialiasingPass::CanRenderToView(preferences, context);
			}
		}
		fixture.Drain();
		return valid && fixture.IsEmpty();
	}
}

bool NEMTests::CheckRenderTargetTransactions(ID3D12Device* device) {

	return CheckRegistryReplacement(device) && CheckRegistryEviction(device) &&
		CheckRenderPathReplacement(device) && CheckAntialiasingTargets(device);
}
