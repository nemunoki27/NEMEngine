#include "GPUBufferLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
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
		registry.BeginFrame();
		valid &= registry.Find(desc.name) == nullptr && registry.ResizeTransient(context, desc, 8, 4) == replacement &&
			registry.Find(desc.name) == replacement;
		registry.Clear();
		fixture.Drain();
		return valid && fixture.IsEmpty();
	}

	bool CheckRenderPathReplacement(ID3D12Device* device) {

		TargetCreationFixture fixture(device);
		Engine::RenderTargetCreationContext context{device, fixture.targets, fixture.depths, fixture.shaders};
		Engine::RenderPathResources resources;
		resources.Resize(context, 4, 4);
		const auto original = CaptureResources(resources);
		auto* pyramid = &resources.GetDepthPyramid();
		resources.GetDepthPyramid().MarkBuilt(37);
		bool valid = resources.IsValid();
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
		resources.Resize(context, 8, 4);
		valid &= resources.IsValid() && CaptureResources(resources) != original &&
			resources.GetSceneMain()->GetWidth() == 8 && resources.GetSceneFinal()->GetWidth() == 8 &&
			resources.GetDepthPyramid().GetWidth() == 8 && &resources.GetDepthPyramid() == pyramid &&
			!resources.GetDepthPyramid().IsBuiltForFrame(37) && fixture.retirement.GetPendingCount() > 0;
		resources.Destroy();
		fixture.Drain();
		return valid && fixture.IsEmpty();
	}
}

bool NEMTests::CheckRenderTargetTransactions(ID3D12Device* device) {

	return CheckRegistryReplacement(device) && CheckRenderPathReplacement(device);
}
