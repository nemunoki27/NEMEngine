#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <span>
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	TextRendererComponent struct
	//============================================================================
	// 文字レイアウト済みの1グリフデータ
	struct TextLayoutGlyph {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 0;
		static constexpr bool kSerializable = false;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;

		Vector2 rectMin{};
		Vector2 rectMax{};
		Vector2 uvMin{};
		Vector2 uvMax{};
	};
	// 文字ごとのトランスフォーム、グリフ中心を基準にSRTを掛ける
	struct TextCharTransform {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 4;
		static constexpr bool kSerializable = false;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;

		Vector2 translation{};
		// Z回転(度)
		float rotation = 0.0f;
		Vector2 scale = Vector2::AnyInit(1.0f);
	};
	// ランタイムキャッシュデータ
	struct TextLayoutRuntimeComponent {

		static constexpr bool kSerializable = false;

		// キャッシュ生成時の設定
		AssetID font{};
		uint64_t fontContentRevision = 0;
		uint64_t textHash = 0;
		float fontSize = 32.0f;
		float charSpacing = 0.0f;

		// 描画時に使う固定長情報
		Vector2 atlasSize = Vector2::AnyInit(1.0f);
		float pxRange = 8.0f;
		// テキストブロック全体のサイズ、ピボット適用の基準に使う
		Vector2 boundsSize = Vector2::AnyInit(0.0f);

		// キャッシュが有効か
		bool valid = false;
	};
	// 描画とピッキングで共有する1グリフ分の矩形と変換
	struct TextGlyphGeometry {

		Vector2 rectMin{};
		Vector2 rectMax{};
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
	};
	// テキスト描画
	struct TextRendererComponent {

		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Render;

		// フォント設定
		AssetID font{};
		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ上書き、reflection駆動で描画/アニメーションに使う
		MaterialInstanceParameters materialInstance{};
		// 描画するテキスト
		std::string text = "Text";

		// フォントサイズ
		float fontSize = 32.0f;
		// 文字間隔
		float charSpacing = 0.0f;
		// ピボット、テキストブロックを正規化した0-1基準でこの点が原点に合う、スプライトと同じ扱い
		Vector2 pivot = Vector2::AnyInit(0.0f);
		// UVを文字ごとの0-1で扱うか
		bool uvPerCharacter = true;

		// 描画レイヤー
		int32_t layer = 0;
		// 描画レイヤー内の中での順序
		int32_t order = 0;
		// 表示フラグ
		bool visible = true;
		// RenderFeatureとLightが参照する描画レイヤー
		uint32_t renderingLayerMask = 1u;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;
		// 描画キュー
		RenderPhase queue = RenderPhase::ScreenUI;

		// 2D(スクリーン空間)か3D(ワールド空間)かの次元
		Dimension dimension = Dimension::Type2D;
		// 3D描画時にピクセル単位のグリフをワールド単位へ縮小するスケール
		float worldScale = 0.01f;

		// Registryから呼ばれる文字列BufferとRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, TextRendererComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, TextRendererComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, TextRendererComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, TextRendererComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const TextRendererComponent& component, nlohmann::json& out);
	};

	// json変換
	void from_json(const nlohmann::json& in, TextRendererComponent& component);
	void to_json(nlohmann::json& out, const TextRendererComponent& component);
	// Entityに付随する文字別変換とレイアウト済みグリフ
	std::span<TextCharTransform> GetTextCharTransforms(
		ECSWorld& world, const Entity& entity);
	std::span<const TextCharTransform> GetTextCharTransforms(
		const ECSWorld& world, const Entity& entity);
	void SetTextCharTransforms(ECSWorld& world, const Entity& entity,
		std::span<const TextCharTransform> transforms);
	std::span<TextLayoutGlyph> GetTextLayoutGlyphs(
		ECSWorld& world, const Entity& entity);
	std::span<const TextLayoutGlyph> GetTextLayoutGlyphs(
		const ECSWorld& world, const Entity& entity);
	void SetTextLayoutGlyphs(ECSWorld& world, const Entity& entity,
		std::span<const TextLayoutGlyph> glyphs);
	// レイアウトキャッシュを無効化する
	void InvalidateTextLayout(ECSWorld& world, const Entity& entity);
	// 文字列のキャッシュ比較用Hashを返す
	uint64_t HashTextLayoutString(std::string_view text);
	// 描画側と同じピボットと文字別SRTを適用したグリフ形状を返す
	TextGlyphGeometry ResolveTextGlyphGeometry(
		const TextRendererComponent& renderer,
		const TextLayoutRuntimeComponent& layout,
		const TextLayoutGlyph& glyph,
		const TextCharTransform* charTransform,
		const Matrix4x4& worldMatrix);
	// 文字別変換を含む保存データへ変換する
	void SerializeTextRenderer(const TextRendererComponent& component,
		std::span<const TextCharTransform> transforms, nlohmann::json& out);

} // Engine
