#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorldLifetime.h>
#include <cstdint>
#include <memory>
#include <dxgiformat.h>

namespace Engine {

	class RenderSceneBatch;
	class RenderAssetLibrary;
	class AssetDatabase;
	class ECSWorld;
	class GraphicsCore;
	//============================================================================
	//	RenderColorPrecisionPolicy class
	//	内部Albedoの範囲を確認して保存形式を選ぶ
	//============================================================================
	class RenderColorPrecisionPolicy {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 同じScene入力の形式判定を再利用する
		DXGI_FORMAT Resolve(GraphicsCore& graphics, RenderAssetLibrary& assets,
			const RenderSceneBatch& batch, const AssetDatabase* database, ECSWorld* world, bool allowCompact = true);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct InputKey {

			uint64_t renderRevision = 0;
			uint64_t materialRevision = 0;
			uint64_t textureRevision = 0;
			bool operator==(const InputKey&) const = default;
		};

		//--------- variables ----------------------------------------------------

		InputKey key_;
		std::shared_ptr<const ECSWorldLifetime> lifetime_;
		const RenderSceneBatch* batch_ = nullptr;
		DXGI_FORMAT format_ = DXGI_FORMAT_R32G32B32A32_FLOAT;
	};
}
