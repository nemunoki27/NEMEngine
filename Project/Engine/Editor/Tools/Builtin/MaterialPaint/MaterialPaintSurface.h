#pragma once

#include <Engine/Core/Rendering/DxObject/Buffers/DxReadbackBuffer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

#include <memory>
#include <span>
#include <vector>

namespace Engine {

	struct EditorPanelContext;

	struct MaterialPaintPositionImage {
		std::span<const uint8_t> pixels;
		uint32_t width, height, pitch, bytes;
		const Matrix4x4& matrix;
		bool Sample(float u, float v, Vector3& position) const;
		bool Visible(const Vector3& position, const Vector3& faceNormal) const;
		Vector3 Normal(float u, float v, const Vector3& center, Vector3 towardEye) const;
	};

	// SceneViewの可視表面をブラシの位置と遮蔽判定で共有する
	class MaterialPaintSurface {
	public:
		bool Update(const EditorPanelContext& context, const Matrix4x4& viewProjection);
		MaterialPaintPositionImage GetImage() const { return {ready_, width_, height_, pitch_, bytes_, readyMatrix_}; }
		void Clear();
	private:
		std::unique_ptr<DxReadbackBuffer<uint8_t>> buffer_;
		std::vector<uint8_t> ready_;
		uint32_t width_ = 0, height_ = 0, pitch_ = 0, bytes_ = 0;
		uint64_t fence_ = 0;
		uint64_t renderRevision_ = 0, transformRevision_ = 0;
		double nextCapture_ = 0;
		Matrix4x4 pendingMatrix_{}, readyMatrix_{};
		const void* source_ = nullptr;
	};
}
