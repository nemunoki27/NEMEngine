#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderTypes.h>
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <vector>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	LineImmediateBuffer class
	//	C#等から発行された即時ライン描画を1フレーム分ためる、フレーム頭でクリアする
	//============================================================================
	class LineImmediateBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 即時描画1件、点列はpoints_へのoffsetとcountで参照する
		struct Entry {

			uint32_t pointOffset = 0;
			uint32_t pointCount = 0;

			// trueなら連結ポリライン、falseなら線分リスト
			bool connected = true;
			bool loop = false;
			bool is2D = false;

			AssetID material{};
			BlendMode blendMode = BlendMode::Normal;
			RenderPhase queue = RenderPhase::Transparent;
		};

		LineImmediateBuffer() = default;
		~LineImmediateBuffer() = default;

		// フレーム頭で前フレーム分を破棄する
		void BeginFrame();

		// ポリラインを追加する、materialが空なら描画時に既定Lineへ解決する
		void AddPolyline(const LinePoint* points, uint32_t count, bool connected, bool loop, bool is2D, AssetID material);
		// 組み込み球を線分リストとして追加する
		void AddSphere(const Vector3& center, float radius, const Color4& color,
			uint32_t division, float thickness, AssetID material);

		//--------- accessor -----------------------------------------------------

		const std::vector<Entry>& GetEntries() const { return entries_; }
		// 抽出時に解決する、appendでpoints_が再確保されても確定後に呼ぶので安全
		const LinePoint* GetPoints(const Entry& entry) const { return points_.data() + entry.pointOffset; }

		// singleton
		static LineImmediateBuffer& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 全エントリの点列を連結して持つプール
		std::vector<LinePoint> points_{};
		std::vector<Entry> entries_{};
	};
} // Engine
