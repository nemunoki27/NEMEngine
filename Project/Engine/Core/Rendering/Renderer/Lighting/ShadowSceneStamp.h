#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine {

	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPipelineDeps;

	// 静止した影の入力を比較する世代とCamera
	struct ShadowSceneStamp {

		uint64_t renderRevision = 0;
		uint64_t materialRevision = 0;
		uint64_t pipelineRevision = 0;
		uint64_t textureRevision = 0;
		uint64_t meshRevision = 0;
		uint64_t geometryGeneration = 0;
		uint64_t worldRevision = 0;
		uint32_t cullingMask = 0xffffffffu;
		Matrix4x4 viewProjection = Matrix4x4::Identity();
		float nearClip = 0.0f;
		float farClip = 0.0f;
		bool meshShader = false;
		bool meshLOD = false;
		bool reusable = false;
		std::vector<uint64_t> skinningGenerations;

		bool operator==(const ShadowSceneStamp&) const = default;
	};

	// 共通のMaterial調査を入力世代が変わるまで保持する
	class ShadowSceneStampCache {
	public:
		ShadowSceneStampCache();
		~ShadowSceneStampCache();
		ShadowSceneStamp Build(GraphicsCore& graphicsCore, const SceneExecutionContext& context, const RenderPipelineDeps& deps);
		uint64_t GetRebuildCount() const;
	private:
		struct Implementation;
		std::unique_ptr<Implementation> implementation_;
	};

	// 時間依存の描画を除いて影の入力を集約する
	ShadowSceneStamp BuildShadowSceneStamp(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
		const RenderPipelineDeps& deps);
}
