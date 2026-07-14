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
	//	ParticleTrailPointData
	//	GPUでリボンへ展開する評価済みのトレイル点
	//============================================================================
	struct ParticleTrailPointData {

		Vector3 position = Vector3::AnyInit(0.0f);
		float halfWidth = 0.0f;
		Vector3 tangent = Vector3(0.0f, 0.0f, 1.0f);
		float ribbonT = 0.0f;
		Color4 color = Color4::White();
		uint32_t materialIndex = 0;
		Vector3 pad0 = Vector3::AnyInit(0.0f);
	};
	static_assert(sizeof(ParticleTrailPointData) == 64);

	//============================================================================
	//	ParticleTrailRenderData
	//	トレイル描画で共有する点列とマテリアルデータ
	//============================================================================
	struct ParticleTrailRenderData {

		std::vector<ParticleTrailPointData> points{};
		std::vector<uint32_t> segments{};
		std::vector<ParticleMaterialData> materials{};
		std::vector<uint8_t> customParameters{};

		void Clear();
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
		// トレイル点列とセグメント索引をアップロードする
		void UploadTrailGeometry(const ParticleTrailRenderData& data);
		// トレイルの固定マテリアルデータをアップロードする
		void UploadTrailMaterials(const std::vector<ParticleMaterialData>& materials);
		// トレイルの可変カスタムデータをアップロードする
		void UploadTrailCustomParameters(const std::vector<uint8_t>& data);

		//--------- accessor -----------------------------------------------------

		D3D12_GPU_VIRTUAL_ADDRESS GetGeometryGPUAddress() const { return geometry_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetMaterialsGPUAddress() const { return materials_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetCustomParametersGPUAddress() const {
			return customParameterBuffer_ ? customParameterBuffer_->GetGPUVirtualAddress() : 0;
		}
		uint32_t GetInstanceCount() const { return instanceCount_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetTrailPointsGPUAddress() const { return trailPoints_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetTrailSegmentsGPUAddress() const { return trailSegments_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetTrailMaterialsGPUAddress() const { return trailMaterials_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetTrailCustomParametersGPUAddress() const {
			return trailCustomParameterBuffer_ ? trailCustomParameterBuffer_->GetGPUVirtualAddress() : 0;
		}
		uint32_t GetTrailSegmentCount() const { return trailSegmentCount_; }
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
		StructuredInstanceBuffer<ParticleTrailPointData> trailPoints_{ "gTrailPoints" };
		StructuredInstanceBuffer<uint32_t> trailSegments_{ "gTrailSegments" };
		StructuredInstanceBuffer<ParticleMaterialData> trailMaterials_{ "gParticleMaterials_Trail" };
		ComPtr<ID3D12Resource> trailCustomParameterBuffer_{};
		uint8_t* trailCustomParameterMapped_ = nullptr;
		uint32_t trailCustomParameterCapacity_ = 0;
		uint32_t trailSegmentCount_ = 0;
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 可変パラメータ用バッファを必要なバイト数まで拡張する
		void EnsureCustomParameterCapacity(uint32_t requiredSize);
		// トレイル可変パラメータ用バッファを必要なバイト数まで拡張する
		void EnsureTrailCustomParameterCapacity(uint32_t requiredSize);
	};
} // Engine
