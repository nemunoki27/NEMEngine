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
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialStructures.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <vector>
#include <cstdint>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ParticleCustomParameterLayout
	//	PSのStructuredBuffer reflectionから作る可変パラメータレイアウト
	//============================================================================
	struct ParticleCustomParameterLayout {

		uint32_t stride = 0;
		std::vector<ShaderConstantBufferVariable> variables{};
	};

	//============================================================================
	//	ParticleGeometryData
	//	VSとMSが読む粒子の形状データ
	//============================================================================
	struct ParticleGeometryData {

		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Color4 vertexColor = Color4::White();
		// 形状アニメーション用のパラメータ、形状ごとに解釈が変わる
		Vector4 shapeParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	};

	//============================================================================
	//	ParticleMaterialData
	//	PSだけが読む粒子のマテリアルデータ
	//============================================================================
	struct ParticleMaterialData {

		// 発光色と強さ、wが強さ
		Vector4 emissive = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		// xがアルファ棄却の閾値、yzwは予約
		Vector4 materialParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		// フェーズマテリアルの寿命アニメーション色
		Color4 materialColor = Color4::White();
		// カラーテクスチャのUV変換行列
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};

	// CPUでフェーズ分割とソートを行う粒子データ
	struct ParticleDrawInstanceData {

		ParticleGeometryData geometry{};
		ParticleMaterialData material{};
		std::vector<uint8_t> customParameters{};
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
		void UploadInstances(const std::vector<ParticleDrawInstanceData>& instances);
		// PSのreflectionレイアウトで構築した可変データを転送する
		void UploadCustomParameters(const std::vector<uint8_t>& data);
		// トレイル頂点配列をアップロードする
		void UploadTrailVertices(const std::vector<ParticleTrailVertex>& vertices);

		//--------- accessor -----------------------------------------------------

		D3D12_GPU_VIRTUAL_ADDRESS GetGeometryGPUAddress() const { return geometry_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetMaterialsGPUAddress() const { return materials_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetCustomParametersGPUAddress() const {
			return customParameterBuffer_ ? customParameterBuffer_->GetGPUVirtualAddress() : 0;
		}
		uint32_t GetInstanceCount() const { return instanceCount_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetTrailVerticesGPUAddress() const { return trailVertices_.GetGPUAddress(); }
		uint32_t GetTrailVertexCount() const { return trailVertexCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StructuredInstanceBuffer<ParticleGeometryData> geometry_{ "gParticleGeometry" };
		StructuredInstanceBuffer<ParticleMaterialData> materials_{ "gParticleMaterials" };
		ID3D12Device* device_ = nullptr;
		ComPtr<ID3D12Resource> customParameterBuffer_{};
		uint8_t* customParameterMapped_ = nullptr;
		uint32_t customParameterCapacity_ = 0;
		uint32_t instanceCount_ = 0;
		StructuredInstanceBuffer<ParticleTrailVertex> trailVertices_{ "gTrailVertices" };
		uint32_t trailVertexCount_ = 0;
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 可変パラメータ用バッファを必要なバイト数まで拡張する
		void EnsureCustomParameterCapacity(uint32_t requiredSize);
	};
} // Engine
