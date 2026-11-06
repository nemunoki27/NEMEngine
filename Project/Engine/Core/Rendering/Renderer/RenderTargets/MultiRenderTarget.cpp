#include "MultiRenderTarget.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>

// c++
#include <array>
#include <stdexcept>

namespace {

	// 色アタッチメントの生成経路を揃える
	std::unique_ptr<Engine::RenderTexture2D> CreateColor(ID3D12Device* device,
		Engine::RTVDescriptor* targets, Engine::SRVDescriptor* shaders,
		uint32_t width, uint32_t height, const Engine::ColorAttachmentDesc& color) {

		auto texture = std::make_unique<Engine::RenderTexture2D>();
		Engine::RenderTextureCreateDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.format = color.format;
		desc.clearColor = color.clearColor;
		desc.createUAV = color.createUAV;
		desc.useOptimizedClearValue = color.useOptimizedClearValue;
		desc.debugName = std::wstring(color.name.begin(), color.name.end());
		texture->Create(device, targets, shaders, desc);
		return texture;
	}
}

//============================================================================
//	MultiRenderTarget classMethods
//============================================================================
Engine::MultiRenderTarget::~MultiRenderTarget() {

	Destroy();
}

void Engine::MultiRenderTarget::Create(ID3D12Device* device,
	RTVDescriptor* rtvDescriptor, DSVDescriptor* dsvDescriptor,
	SRVDescriptor* srvDescriptor, const MultiRenderTargetCreateDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "MultiRenderTargetの作成を開始します");

	// 完成するまでは現在の描画先を維持する
	if (desc.width == 0 || desc.height == 0 || desc.colors.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) {
		throw std::invalid_argument("MultiRenderTargetのサイズまたは色数が不正です");
	}
	std::vector<std::unique_ptr<RenderTexture2D>> colors;
	std::unique_ptr<DepthTexture2D> depth;

	// サイズ設定
	uint32_t colorCount = static_cast<uint32_t>(desc.colors.size());

	// 色レンダーテクスチャの作成
	colors.reserve(colorCount);
	for (size_t i = 0; i < colorCount; ++i) {

		Logger::Output(LogType::Engine, "RenderTexture2D番号: {}", i);

		colors.emplace_back(CreateColor(device, rtvDescriptor, srvDescriptor, desc.width, desc.height, desc.colors[i]));
	}
	// 深度レンダーテクスチャの作成
	if (desc.depth.has_value()) {

		depth = std::make_unique<DepthTexture2D>();
		depth->Create(dsvDescriptor, srvDescriptor, *desc.depth);
	}

	colors_.swap(colors);
	depth_.swap(depth);
	width_ = desc.width;
	height_ = desc.height;

	Logger::Output(LogType::Engine, "MultiRenderTargetを作成しました");
	Logger::EndSection(LogType::Engine);
}

void Engine::MultiRenderTarget::ReplaceColor(const RenderTargetCreationContext& context,
	size_t index, const ColorAttachmentDesc& desc) {

	if (index >= colors_.size()) {
		throw std::out_of_range("色アタッチメントの指定が範囲外です");
	}
	auto candidate = CreateColor(context.device, &context.targets, &context.shaders, width_, height_, desc);
	colors_[index].swap(candidate);
}

void Engine::MultiRenderTarget::Destroy() {

	for (auto& color : colors_) {

		if (color) {
			color->Destroy();
			// 子Textureの所有を解除する
			color.reset();
		}
	}
	colors_.clear();

	if (depth_) {

		depth_->Destroy();
		depth_.reset();
	}
	width_ = 0;
	height_ = 0;
}

void Engine::MultiRenderTarget::TransitionForRender(DxCommand& dxCommand) {

	// レンダーターゲットへ遷移
	std::array<RenderTexture2D*, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> textures{};
	for (size_t index = 0; index < colors_.size(); ++index) textures[index] = colors_[index].get();
	RenderTexture2D::TransitionAll(dxCommand, std::span{ textures.data(), colors_.size() }, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if (depth_) {

		depth_->Transition(dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}
}

void Engine::MultiRenderTarget::TransitionForShaderRead(DxCommand& dxCommand) {

	// 色と深度を読み取り状態へ遷移する
	std::array<RenderTexture2D*, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> textures{};
	for (size_t index = 0; index < colors_.size(); ++index) textures[index] = colors_[index].get();
	RenderTexture2D::TransitionAll(dxCommand, std::span{ textures.data(), colors_.size() }, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	if (depth_) {

		depth_->Transition(dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}

void Engine::MultiRenderTarget::Bind(DxCommand& dxCommand) const {

	// 色と深度の描画先を設定する
	std::array<RenderTarget, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
	for (size_t index = 0; index < colors_.size(); ++index) targets[index] = colors_[index]->GetRenderTarget();
	const std::span<const RenderTarget> renderTargets(targets.data(), colors_.size());
	if (depth_) {

		dxCommand.BindRenderTargets(renderTargets, depth_->GetDSVCPUHandle());
	} else {

		dxCommand.BindRenderTargets(renderTargets, std::nullopt);
	}
	dxCommand.SetViewportAndScissor(width_, height_);
}

void Engine::MultiRenderTarget::Clear(DxCommand& dxCommand, const MultiRenderTargetClearDesc& desc) const {

	auto* commandList = dxCommand.GetCommandList();
	if (desc.clearColor) {
		for (const auto& color : colors_) {

			const RenderTarget& renderTarget = color->GetRenderTarget();
			// クリアカラーを設定
			float clearColor[4] = { renderTarget.clearColor.r,renderTarget.clearColor.g,
				renderTarget.clearColor.b,renderTarget.clearColor.a };
			if (desc.clearColorValue.has_value()) {

				clearColor[0] = desc.clearColorValue->r;
				clearColor[1] = desc.clearColorValue->g;
				clearColor[2] = desc.clearColorValue->b;
				clearColor[3] = desc.clearColorValue->a;
			}
			// レンダーターゲットをクリア
			commandList->ClearRenderTargetView(renderTarget.rtvHandle, clearColor, 0, nullptr);
		}
	}
	if (depth_ && (desc.clearDepth || desc.clearStencil)) {

		D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
		if (desc.clearDepth) {
			flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_DEPTH);
		}
		if (desc.clearStencil) {
			flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_STENCIL);
		}
		// 深度クリア
		commandList->ClearDepthStencilView(depth_->GetDSVCPUHandle(), flags,
			desc.clearDepthValue, desc.clearStencilValue, 0, nullptr);
	}
}

std::vector<Engine::RenderTarget> Engine::MultiRenderTarget::BuildRenderTargets() const {

	// 色描画先のハンドルを集める
	std::vector<RenderTarget> result{};
	result.reserve(colors_.size());
	for (const auto& color : colors_) {

		result.emplace_back(color->GetRenderTarget());
	}
	return result;
}

Engine::RenderTexture2D* Engine::MultiRenderTarget::GetColorTexture(size_t index) {

	return index < colors_.size() ? colors_[index].get() : nullptr;
}

const Engine::RenderTexture2D* Engine::MultiRenderTarget::GetColorTexture(size_t index) const {

	return index < colors_.size() ? colors_[index].get() : nullptr;
}
