#include "MaterialPaintSurface.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <imgui.h>
#include <DirectXPackedVector.h>
#include <cmath>
#include <cstring>

namespace Engine {

	void MaterialPaintSurface::Clear() {

		buffer_.reset();
		ready_.clear();
		fence_ = 0;
		source_ = nullptr;
	}

	bool MaterialPaintSurface::Update(const EditorPanelContext& context, const Matrix4x4& matrix) {

		if (!context.graphicsCore || !context.renderPipeline) return false;
		auto* texture = context.renderPipeline->GetViewGBufferTexture(RenderViewKind::Scene, GBufferAttachment::Position);
		if (!texture || !texture->IsValid()) return false;
		const auto desc = texture->GetResource()->GetDesc();
		if (desc.Format != DXGI_FORMAT_R32G32B32A32_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
		auto& dx = context.graphicsCore->GetDXObject();
		if (source_ != texture->GetResource() || width_ != desc.Width || height_ != desc.Height) {
			Clear();
			source_ = texture->GetResource();
			width_ = static_cast<uint32_t>(desc.Width);
			height_ = desc.Height;
			bytes_ = desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16 : 8;
			pitch_ = (width_ * bytes_ + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
			buffer_ = std::make_unique<DxReadbackBuffer<uint8_t>>();
			buffer_->CreateBuffer(dx.GetResourceRetirement(), dx.GetDevice(), size_t(height_) * pitch_);
		}
		if (fence_ && dx.GetCommandQueue()->GetCompletedFenceValue() >= fence_) {
			const auto* pixels = &buffer_->GetReadbackData();
			ready_.assign(pixels, pixels + size_t(height_) * pitch_);
			readyMatrix_ = pendingMatrix_;
			fence_ = 0;
		}
		const bool sameCamera = !ready_.empty() && !std::memcmp(matrix.m, readyMatrix_.m, sizeof(matrix.m));
		const auto* world = context.GetWorld();
		if (!fence_ && (!sameCamera || world->GetRenderDataRevision() != renderRevision_ ||
			world->GetRenderTransformRevision() != transformRevision_ || ImGui::GetTime() >= nextCapture_)) {
			auto* cmd = dx.GetDxCommand();
			const auto previous = texture->GetCurrentState();
			texture->Transition(*cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
			D3D12_TEXTURE_COPY_LOCATION src{};
			src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			src.pResource = texture->GetResource();
			D3D12_TEXTURE_COPY_LOCATION dst{};
			dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dst.pResource = buffer_->GetResource();
			dst.PlacedFootprint.Footprint = {desc.Format, width_, height_, 1, pitch_};
			cmd->GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			texture->Transition(*cmd, previous);
			pendingMatrix_ = matrix;
			renderRevision_ = world->GetRenderDataRevision();
			transformRevision_ = world->GetRenderTransformRevision();
			nextCapture_ = ImGui::GetTime() + 0.2;
			fence_ = dx.GetCommandQueue()->GetLastSignaledFenceValue() + 1;
		}
		return sameCamera;
	}

	bool MaterialPaintPositionImage::Sample(float u, float v, Vector3& position) const {

		if (!(0 <= u && u < 1 && 0 <= v && v < 1) || pixels.empty()) return false;
		const auto* pixel = pixels.data() + size_t(v * height) * pitch + size_t(u * width) * bytes;
		float values[4];
		if (bytes == 16) std::memcpy(values, pixel, sizeof(values));
		else {
			uint16_t half[4];
			std::memcpy(half, pixel, sizeof(half));
			for (int i = 0; i < 4; ++i) values[i] = DirectX::PackedVector::XMConvertHalfToFloat(half[i]);
		}
		position = {values[0], values[1], values[2]};
		if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || values[3] <= 0) return false;
		if (position.x != 0 || position.y != 0 || position.z != 0) return true;
		const Vector3 projected = Vector3::Transform(position, matrix);
		return std::abs(0.5f + 0.5f * projected.x - u) * width < 1 &&
			std::abs(0.5f - 0.5f * projected.y - v) * height < 1;
	}

	bool MaterialPaintPositionImage::Visible(const Vector3& position, const Vector3& faceNormal) const {

		const auto ndc = Vector3::Transform(position, matrix);
		if (!(0 <= ndc.z && ndc.z <= 1)) return false;
		Vector3 surface;
		if (!Sample(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, surface)) return false;
		const float precision = bytes == 8 ? 0.0015f : 0.000002f;
		const float tolerance = 0.00015f + precision * (std::abs(surface.x) + std::abs(surface.y) + std::abs(surface.z));
		return std::abs(Vector3::Dot(surface - position, faceNormal)) <= tolerance;
	}

	Vector3 MaterialPaintPositionImage::Normal(float u, float v, const Vector3& center, Vector3 towardEye) const {

		Vector3 left, right, up, down;
		const bool l = Sample(u - 1.0f / width, v, left), r = Sample(u + 1.0f / width, v, right);
		const bool t = Sample(u, v - 1.0f / height, up), b = Sample(u, v + 1.0f / height, down);
		if ((!l && !r) || (!t && !b)) return towardEye;
		const Vector3 dx = l && (!r || (center - left).Length() < (right - center).Length()) ? center - left : right - center;
		const Vector3 dy = t && (!b || (center - up).Length() < (down - center).Length()) ? center - up : down - center;
		auto normal = Vector3::NormalizeOr(Vector3::Cross(dx, dy), towardEye);
		return Vector3::Dot(normal, towardEye) < 0 ? normal * -1.0f : normal;
	}
}
