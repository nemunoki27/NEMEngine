#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <memory>
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class MultiRenderTarget;

	// 2D描画先の深度とステンシルをViewとフレーム単位で保持する
	class RendererDepthAttachments {
	public:
		// 描画先の寸法が変わったときだけ確保し、フレームの最初にクリアする
		DepthTexture2D* Prepare(GraphicsCore& graphics, const MultiRenderTarget* target, uint32_t domain, uint32_t width, uint32_t height);
		// Deviceを共有する描画先でも同じ遅延確保を使う
		DepthTexture2D* Prepare(DxCommand& commands, DSVDescriptor& depths, SRVDescriptor& shaders,
			const MultiRenderTarget* target, uint32_t domain, uint32_t width, uint32_t height);
		void Destroy();
		void Swap(RendererDepthAttachments& other) noexcept;
	private:
		struct Attachment {

			std::unique_ptr<DepthTexture2D> texture;
			uint64_t serial = UINT64_MAX;
		};
		std::array<std::unordered_map<const MultiRenderTarget*, std::array<Attachment, 2>>, kGraphicsFrameContextCount> frames_;
		GraphicsFrameResourceRegistration frameRegistration_;

		// 無効になったフレームの資源をGPU完了後の回収へ渡す
		void ReleaseInactiveFrames(uint32_t count);
	};
}
