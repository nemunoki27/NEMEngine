#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPayloadArena.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <vector>
#include <span>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Engine {

	// front
	class ECSWorld;
	class MaterialParameterSet;
	struct PrimitiveRendererComponent;

	//============================================================================
	//	RenderQueue structures
	//============================================================================
	// 描画アイテムの種類ID
	namespace RenderBackendID {

		static constexpr uint32_t Sprite = 0x1001;
		static constexpr uint32_t Text = 0x1002;
		static constexpr uint32_t Mesh = 0x1003;
		static constexpr uint32_t Line = 0x1004;
		static constexpr uint32_t Primitive = 0x1006;
		static constexpr uint32_t Particle = 0x1007;
	}

	// スプライト描画データ
	struct SpriteRenderPayload {

		Vector2 size = Vector2::AnyInit(1.0f);
		Vector2 pivot = Vector2::AnyInit(0.5f);

		Matrix4x4 uvMatrix = Matrix4x4::Identity();

		// エンティティ固有のマテリアル値を同フレーム内だけ参照する
		const MaterialParameterSet* materialInstance = nullptr;
	};
	// テキスト描画データ
	struct TextRenderPayload {

		// フォント
		AssetID font{};
		std::string_view text = "Text";

		float fontSize = 32.0f;
		float charSpacing = 0.0f;
		Matrix4x4 uvMatrix = Matrix4x4::Identity();

		// エンティティ固有のマテリアル値を同フレーム内だけ参照する
		const MaterialParameterSet* materialInstance = nullptr;
	};
	// メッシュ描画データ
	inline constexpr uint32_t kAllMeshSubMeshes = UINT32_MAX;
	struct MeshRenderPayload {

		// メッシュ
		AssetID mesh{};
		// 描画対象サブメッシュ、UINT32_MAXは全サブメッシュ
		uint32_t subMeshIndex = kAllMeshSubMeshes;
		// 混在モデルを描画状態でまとめたグループ、UINT32_MAXは全グループ
		uint32_t subMeshGroupIndex = UINT32_MAX;

		// 深度前描画を有効にするか
		bool enableZPrepass = true;
	};
	// プロシージャル形状描画データ
	struct PrimitiveRenderPayload {

		// ジオメトリ生成に使うコンポーネントを指す、同フレーム内のみ有効
		const PrimitiveRendererComponent* renderer = nullptr;

		// UVTransformComponentのUV行列、無ければ単位行列
		Matrix4x4 uvMatrix = Matrix4x4::Identity();

		// エンティティ固有のマテリアル値
		const MaterialParameterSet* materialInstance = nullptr;
	};
	// パーティクル描画データ
	struct ParticleRenderPayload {

		// 外部Runtime Storage内のグループを描画時に安全に引き直す
		UUID groupID{};
		uint32_t groupIndex = 0;
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
		RenderPhase renderPhase = RenderPhase::Opaque;

		// 描画の可視レイヤーマスク
		uint32_t visibilityLayerMask = 0xFFFFFFFFu;
		// RenderFeatureとLightが参照する描画レイヤーマスク
		uint32_t renderingLayerMask = 1u;
		// 描画のソートレイヤーと順序
		int32_t sortingLayer = 0;
		int32_t sortingOrder = 0;
		// Canvas配下はマテリアルよりヒエラルキー順を優先する
		bool orderedUI = false;
		uint32_t hierarchyOrder = 0;

		// ワールド変換行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		// ビュー依存ソートに使う代表座標
		Vector3 sortPosition = Vector3::AnyInit(0.0f);
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
		uint32_t motionFrameSerial = 0;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;
		// 解決済みの表面方式
		MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
		// サブメッシュ指定がMaterialの表面方式より優先されるか
		bool surfaceModeOverridden = false;
		// RendererとMaterialを解決した最終的な影設定
		bool castShadows = true;
		bool receiveShadows = true;

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
	struct RenderTransformChange {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
		uint32_t motionFrameSerial = 0;
	};

	class RenderSceneBatch {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

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
		// 抽出元Worldと描画データ、Transform世代を記録する
		void SetSource(const ECSWorld* world,
			uint64_t renderRevision, uint64_t transformRevision);
		// Transformだけを更新した世代を記録する
		void SetTransformSource(uint64_t transformRevision,
			bool completeChanges);
		// 指定EntityだけのTransformを更新
		void RefreshTransforms(ECSWorld& world,
			std::span<const Entity> changedEntities);
		// 差分履歴が利用できない場合に全Transformを更新
		void RefreshAllTransforms();

		// 描画アイテムのペイロードの追加
		template<class T>
		RenderPayload PushPayload(const T& payload) { return payloadArena_.Push(payload); }

		//--------- accessor -----------------------------------------------------

		const std::vector<RenderItem>& GetItems() const { return items_; }
		std::vector<RenderItem>& GetMutableItems() { return items_; }
		std::span<const RenderTransformChange>
			GetTransformChanges() const {
			return transformChanges_;
		}
		bool HasCompleteTransformChanges() const {
			return completeTransformChanges_;
		}
		// Raytracing等が描画内容の変更検知に使用する世代
		uint64_t GetSourceRevision() const { return contentRevision_; }
		// 色の変更もRaytracing等の内容更新へ伝える
		void SetMaterialSource(uint64_t revision);
		uint64_t GetSourceRenderRevision() const { return sourceRenderRevision_; }
		uint64_t GetSourceTransformRevision() const { return sourceTransformRevision_; }
		bool MatchesStructure(const ECSWorld* world, uint64_t renderRevision) const {
			return sourceWorld_ == world &&
				sourceRenderRevision_ == renderRevision;
		}
		bool MatchesTransforms(uint64_t transformRevision) const {
			return sourceTransformRevision_ == transformRevision;
		}

		template<class T>
		const T* GetPayload(const RenderItem& item) const { return payloadArena_.Get<T>(item.payload); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::vector<RenderItem> items_;
		std::unordered_multimap<uint64_t, size_t>
			entityItemLookup_;
		std::vector<RenderTransformChange> transformChanges_;
		RenderPayloadArena payloadArena_{};
		const ECSWorld* sourceWorld_ = nullptr;
		uint64_t sourceRenderRevision_ = 0;
		uint64_t sourceMaterialRevision_ = 0;
		uint64_t sourceTransformRevision_ = 0;
		uint64_t contentRevision_ = 0;
		bool completeTransformChanges_ = false;

		// Entityを描画アイテム索引へ変換するキー
		static uint64_t BuildEntityKey(const Entity& entity);
		// ソート後の描画アイテム索引を構築
		void RebuildEntityLookup();
		// 現在の行列とサブメッシュ情報から代表座標を更新する
		void RefreshSortPosition(RenderItem& item) const;
	};
} // Engine

