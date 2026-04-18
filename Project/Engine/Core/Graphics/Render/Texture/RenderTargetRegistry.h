#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scene/Header/SceneHeader.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/Core/Graphics/GraphicsCore.h>

// c++
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	RenderTargetRegistry struct
	//============================================================================

	// シーン記述で参照されるレンダーターゲットセットの情報
	struct RegisteredRenderTargetSet {

		// パス記述から参照するための別名
		std::string alias;

		// ランタイムアタッチメント名
		std::vector<std::string> colorNames;
		std::optional<std::string> depthName;

		// 描画サーフェイス
		MultiRenderTarget* surface = nullptr;

		// 参照と一致するか
		bool Matches(const RenderTargetSetReference& reference) const;
	};

	//============================================================================
	//	RenderTargetRegistry class
	//	1つのビュー実行中に使うレンダーターゲットを管理するクラス
	//============================================================================
	class RenderTargetRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderTargetRegistry() = default;
		~RenderTargetRegistry() = default;

		// コピー禁止
		RenderTargetRegistry(const RenderTargetRegistry&) = delete;
		RenderTargetRegistry& operator=(const RenderTargetRegistry&) = delete;
		// ムーブ可能
		RenderTargetRegistry(RenderTargetRegistry&&) noexcept = default;
		RenderTargetRegistry& operator=(RenderTargetRegistry&&) noexcept = default;

		// レンダーターゲットセットを登録する
		void Register(const std::string& alias, MultiRenderTarget* surface,
			const std::vector<std::string>& colorNames, const std::optional<std::string>& depthName);

		// フレーム開始処理
		void BeginFrame();

		// クリア
		void Clear();

		// レンダーターゲットセットをリサイズする
		MultiRenderTarget* ResizeTransient(GraphicsCore& graphicsCore,
			const SceneRenderTargetDesc& desc, uint32_t viewWidth, uint32_t viewHeight);

		//--------- accessor -----------------------------------------------------

		// 別名からレンダーターゲットセットを検索する
		MultiRenderTarget* Find(const std::string& alias) const;
		// 参照からレンダーターゲットセットを検索する
		MultiRenderTarget* Resolve(const RenderTargetSetReference& reference) const;

		std::vector<MultiRenderTarget*> GatherUniqueSurfaces() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 一時的に使用するレンダーターゲットセットのエントリー
		struct TransientEntry {

			SceneRenderTargetDesc desc{};
			uint32_t resolvedWidth = 0;
			uint32_t resolvedHeight = 0;
			std::unique_ptr<MultiRenderTarget> surface{};
		};

		//--------- variables ----------------------------------------------------

		// 登録されたレンダーターゲットセットのエントリー
		std::vector<RegisteredRenderTargetSet> entries_;
		// エントリーのインデックステーブル
		std::unordered_map<std::string, size_t> aliasTable_;
		// 一時的に使用するレンダーターゲットセットのエントリー
		std::unordered_map<std::string, TransientEntry> transients_;

		//--------- functions ----------------------------------------------------

		// レンダーターゲットセットを登録または更新する
		void RegisterOrUpdate(std::string alias, MultiRenderTarget* surface,
			const std::vector<std::string>& colorNames, const std::optional<std::string>& depthName);
		// レンダーターゲットセットの情報から作成情報を構築する
		static MultiRenderTargetCreateDesc BuildCreateDesc(const SceneRenderTargetDesc& desc,
			uint32_t viewWidth, uint32_t viewHeight);
		// シーンレンダーターゲットのフォーマットをDXGI_FORMATに変換する
		static DXGI_FORMAT ToColorFormat(SceneRenderTargetFormat format);
	};
} // Engine