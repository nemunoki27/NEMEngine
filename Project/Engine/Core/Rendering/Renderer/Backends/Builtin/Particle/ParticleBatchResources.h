#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <vector>
#include <cstdint>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ParticleInstanceData
	//	粒子ごとのワールド行列と色、StructuredBufferで読む
	//============================================================================
	struct ParticleInstanceData {

		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Color4 color = Color4::White();
		// xyがUVスケール、zwがUVオフセット
		Vector4 uvScaleOffset = Vector4(1.0f, 1.0f, 0.0f, 0.0f);
		// 形状アニメーション用のパラメータ、形状ごとに解釈が変わる
		Vector4 shapeParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		// 発光色と強さ、wが強さ
		Vector4 emissive = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		// xがアルファ棄却の閾値、yzwは予約
		Vector4 materialParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	};

	//============================================================================
	//	ParticleTrailVertex
	//	トレイルリボンの頂点、CPUで構築してStructuredBufferで読む
	//============================================================================
	struct ParticleTrailVertex {

		Vector3 position = Vector3::AnyInit(0.0f);
		float pad0 = 0.0f;
		Vector2 uv = Vector2::AnyInit(0.0f);
		Vector2 pad1 = Vector2::AnyInit(0.0f);
		Color4 color = Color4::White();
	};

	//============================================================================
	//	ParticleBatchResources class
	//	バッチ1つ分のインスタンスバッファを管理する
	//============================================================================
	class ParticleBatchResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleBatchResources() = default;
		~ParticleBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// インスタンス配列をアップロードする
		void UploadInstances(const std::vector<ParticleInstanceData>& instances);
		// トレイル頂点配列をアップロードする
		void UploadTrailVertices(const std::vector<ParticleTrailVertex>& vertices);

		//--------- accessor -----------------------------------------------------

		D3D12_GPU_VIRTUAL_ADDRESS GetInstancesGPUAddress() const { return instances_.GetGPUAddress(); }
		uint32_t GetInstanceCount() const { return instanceCount_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetTrailVerticesGPUAddress() const { return trailVertices_.GetGPUAddress(); }
		uint32_t GetTrailVertexCount() const { return trailVertexCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StructuredInstanceBuffer<ParticleInstanceData> instances_{ "gInstances" };
		uint32_t instanceCount_ = 0;
		StructuredInstanceBuffer<ParticleTrailVertex> trailVertices_{ "gTrailVertices" };
		uint32_t trailVertexCount_ = 0;
		bool initialized_ = false;
	};
} // Engine
