#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/Graphics/DxLib/DxStructures.h>
#include <Engine/Core/Graphics/Render/Queue/RenderPayloadArena.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector4.h>

// c++
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	RenderQueue structures
	//============================================================================

	// 描画アイテムの種類ID
	namespace RenderBackendID {

		static constexpr uint32_t Sprite = 0x1001;
		static constexpr uint32_t Text = 0x1002;
		static constexpr uint32_t Mesh = 0x1003;
	}

	// スプライト描画データ
	struct SpriteRenderPayload {

		// テクスチャ
		AssetID texture{};

		Vector2 size = Vector2::AnyInit(1.0f);
		Vector2 pivot = Vector2::AnyInit(0.5f);

		Color4 color = Color4::White();
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};
	// テキスト描画データ
	struct TextRenderPayload {

		// フォント
		AssetID font{};
		std::string_view text = "Text";

		float fontSize = 32.0f;
		float charSpacing = 0.0f;
		Color4 color = Color4::White();
	};
	// メッシュ描画データ
	struct MeshRenderPayload {

		// メッシュ
		AssetID mesh{};

		// 深度前描画を有効にするか
		bool enableZPrepass = true;
	};
	// 描画アイテム
	struct RenderItem {

		// 対象エンティティ
		Entity entity{};
		ECSWorld* world = nullptr;
		UUID sceneInstanceID{};

		// 描画アイテムの種類
		uint32_t backendID = 0;
		// 描画フェーズ
		std::string renderPhase = "Opaque";

		// 描画の可視レイヤーマスク
		uint32_t visibilityLayerMask = 0xFFFFFFFFu;
		// 描画のソートレイヤーと順序
		int32_t sortingLayer = 0;
		int32_t sortingOrder = 0;

		// ワールド変換行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;

		// 描画に使用するマテリアル
		AssetID material{};
		// 描画アイテムの種類ごとのデータへのキー
		uint64_t batchKey = 0;

		// 描画に使用するカメラ
		RenderCameraDomain cameraDomain = RenderCameraDomain::Perspective;

		// 描画アイテムの参照情報
		RenderPayload payload{};
	};

	//============================================================================
	//	RenderSceneBatch class
	//	フレーム中の描画アイテムを統一管理する
	//============================================================================
	class RenderSceneBatch {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderSceneBatch() = default;
		~RenderSceneBatch() = default;

		// 描画アイテムの追加
		void Add(RenderItem&& item);
		// 描画アイテムのクリア
		void Clear();
		// フレーム中に追加される描画アイテムとペイロードの容量を事前に確保
		void Reserve(uint32_t itemCount, uint32_t payloadByteCount);
		// 描画アイテムのソート
		void Sort();

		// 描画アイテムのペイロードの追加
		template<class T>
		RenderPayload PushPayload(const T& payload) { return payloadArena_.Push(payload); }

		//--------- accessor -----------------------------------------------------

		const std::vector<RenderItem>& GetItems() const { return items_; }

		template<class T>
		const T* GetPayload(const RenderItem& item) const { return payloadArena_.Get<T>(item.payload); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<RenderItem> items_;
		RenderPayloadArena payloadArena_{};
	};
} // Engine
