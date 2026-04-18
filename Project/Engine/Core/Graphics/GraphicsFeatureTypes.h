#pragma once

//============================================================================
//	include
//============================================================================
#include <d3d12.h>

// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	GraphicsFeatureTypes structures
	//============================================================================

	// GPUアダプタの基本情報
	struct GraphicsAdapterInfo {

		std::string adapterName{};
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		uint64_t dedicatedVideoMemoryBytes = 0;
	};


	// GPU/Driverが対応している機能を保持
	struct GraphicsFeatureSupport {

		// 一番ハイレベルのモデル
		D3D_SHADER_MODEL highestShaderModel = D3D_SHADER_MODEL_6_0;
		// メッシュシェーダのTier
		D3D12_MESH_SHADER_TIER meshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
		// レイトレーシングのTier
		D3D12_RAYTRACING_TIER raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
		bool waveOps = false;

		// メッシュシェーダ対応しているか
		bool SupportsShaderModel6_6() const { return D3D_SHADER_MODEL_6_6 <= highestShaderModel; }
		bool SupportsMeshShaderPath() const { return SupportsShaderModel6_6() && meshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
		// レイトレーシング対応しているか
		bool SupportsRayTracingPath() const { return D3D12_RAYTRACING_TIER_1_0 <= raytracingTier; }
		bool SupportsRayTracingTier1_1() const { return D3D12_RAYTRACING_TIER_1_1 <= raytracingTier; }
	};

	// ユーザー参照設定
	struct GraphicsFeaturePreferences {

		bool allowMeshShader = true;
		bool allowInlineRayTracing = true;
		bool allowDispatchRays = true;
	};

	// ランタイムで使用する機能
	struct GraphicsRuntimeFeatures {

		bool useMeshShader = false;
		bool useInlineRayTracing = false;
		bool useDispatchRays = false;

		bool UsesAnyRayTracing() const { return useInlineRayTracing || useDispatchRays; }
	};

	// エディター汎用関数
	namespace GraphicsFeatureText {

		const char* ToString(D3D_FEATURE_LEVEL value);
		const char* ToString(D3D_SHADER_MODEL value);
		const char* ToString(D3D12_MESH_SHADER_TIER value);
		const char* ToString(D3D12_RAYTRACING_TIER value);
	}
} // Engine