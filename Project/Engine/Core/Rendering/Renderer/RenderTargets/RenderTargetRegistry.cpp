#include "RenderTargetRegistry.h"

//============================================================================
//	include
//============================================================================
#include "RenderTargetSizing.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <type_traits>
#include <utility>
#include <unordered_set>

namespace {

	// 名前配列が同一か
	bool IsSameNameArray(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {

		if (lhs.size() != rhs.size()) {
			return false;
		}
		// 要素ごとに比較
		for (size_t i = 0; i < lhs.size(); ++i) {
			if (lhs[i] != rhs[i]) {
				return false;
			}
		}
		return true;
	}
	// 色アタッチメントの名前を取得する
	std::vector<std::string> GetEffectiveColorNames(const Engine::SceneRenderTargetDesc& desc) {

		std::vector<std::string> result{};
		for (const auto& color : desc.colors) {
			result.emplace_back(color.name);
		}
		return result;
	}
	// 深度アタッチメントの名前を取得する
	std::optional<std::string> GetEffectiveDepthName(const Engine::SceneRenderTargetDesc& desc) {

		if (!desc.withDepth) {
			return std::nullopt;
		}

		if (!desc.name.empty()) {
			return desc.name + ".Depth";
		}
		if (!desc.colors.empty()) {
			return desc.colors.front().name + ".Depth";
		}
		return std::optional<std::string>("Depth");
	}
	// 色アタッチメントの情報が同一か
	bool AreSameColorAttachmentDescs(const std::vector<Engine::SceneRenderTargetColorDesc>& lhs,
		const std::vector<Engine::SceneRenderTargetColorDesc>& rhs) {

		if (lhs.size() != rhs.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs.size(); ++i) {
			if (lhs[i].name != rhs[i].name) {
				return false;
			}
			if (lhs[i].format != rhs[i].format) {
				return false;
			}
			if (lhs[i].createUAV != rhs[i].createUAV) {
				return false;
			}
		}
		return true;
	}
}

//============================================================================
//	RenderTargetRegistry classMethods
//============================================================================
bool Engine::RegisteredRenderTargetSet::Matches(const RenderTargetSetReference& reference) const {

	if (!surface) {
		return false;
	}

	// 登録した別名との一致を調べる
	if (reference.colors.size() == 1 && !reference.depth.has_value()) {
		if (reference.colors.front() == alias) {
			return true;
		}
	}
	// 色と深度の名前が一致するか
	if (!IsSameNameArray(colorNames, reference.colors)) {
		return false;
	}
	if (reference.depth.has_value() != depthName.has_value()) {
		return false;
	}
	if (reference.depth.has_value()) {
		if (*reference.depth != *depthName) {
			return false;
		}
	}
	return true;
}

void Engine::RenderTargetRegistry::Clear() {

	// 一時描画先の資源を回収へ渡す
	for (auto& entry : transients_) {
		if (entry.second.surface) {
			entry.second.surface->Destroy();
			entry.second.surface.reset();
		}
	}
	entries_.clear();
	aliasTable_.clear();
	transients_.clear();
}

void Engine::RenderTargetRegistry::Register(const std::string& alias, MultiRenderTarget* surface,
	const std::vector<std::string>& colorNames, const std::optional<std::string>& depthName) {

	RegisterOrUpdate(alias, surface, colorNames, depthName);
}

void Engine::RenderTargetRegistry::BeginFrame() {

	// そのフレームでの解決表だけリセットする
	entries_.clear();
	aliasTable_.clear();
}

void Engine::RenderTargetRegistry::RegisterOrUpdate(std::string alias, MultiRenderTarget* surface,
	const std::vector<std::string>& colorNames, const std::optional<std::string>& depthName) {

	// 名前と参照を揃えてから登録表を更新する
	RegisteredRenderTargetSet candidate{};
	candidate.alias = std::move(alias);
	candidate.surface = surface;
	candidate.colorNames = colorNames;
	candidate.depthName = depthName;
	static_assert(std::is_nothrow_swappable_v<RegisteredRenderTargetSet>);
	auto found = aliasTable_.find(candidate.alias);
	if (found == aliasTable_.end()) {

		// 別名の登録に失敗したら追加分を戻す
		entries_.emplace_back(std::move(candidate));
		try {
			aliasTable_.emplace(entries_.back().alias, entries_.size() - 1);
		} catch (...) {
			entries_.pop_back();
			throw;
		}
		return;
	}
	// 既存の名前と参照をまとめて差し替える
	std::swap(entries_[found->second], candidate);
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::Find(const std::string& alias) const {

	auto it = aliasTable_.find(alias);
	if (it == aliasTable_.end()) {
		return nullptr;
	}
	return entries_[it->second].surface;
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::Resolve(const RenderTargetSetReference& reference) const {

	// 未指定なら規定のビューを返す
	if (reference.colors.empty() && !reference.depth.has_value()) {
		return Find("View");
	}

	// 単一文字列はエイリアスとしても扱う
	if (reference.colors.size() == 1 && !reference.depth.has_value()) {
		if (MultiRenderTarget* byAlias = Find(reference.colors.front())) {
			return byAlias;
		}
	}
	// 登録されたエントリーと照合して一致するものを返す
	for (const auto& entry : entries_) {
		if (entry.Matches(reference)) {
			return entry.surface;
		}
	}
	return nullptr;
}

Engine::RenderTexture2D* Engine::RenderTargetRegistry::FindColorByName(const std::string& colorName) const {

	// 登録セットのcolorNamesは色インデックスと並びが一致するので、名前位置の色テクスチャを返す
	for (const auto& entry : entries_) {
		if (!entry.surface) {
			continue;
		}
		for (size_t i = 0; i < entry.colorNames.size(); ++i) {
			if (entry.colorNames[i] == colorName) {
				return entry.surface->GetColorTexture(static_cast<uint32_t>(i));
			}
		}
	}
	return nullptr;
}

Engine::DepthTexture2D* Engine::RenderTargetRegistry::FindDepthByName(const std::string& depthName) const {

	for (const auto& entry : entries_) {
		if (entry.surface && entry.depthName.has_value() && *entry.depthName == depthName) {
			return entry.surface->GetDepthTexture();
		}
	}
	return nullptr;
}

std::vector<Engine::MultiRenderTarget*> Engine::RenderTargetRegistry::GatherUniqueSurfaces() const {

	std::vector<MultiRenderTarget*> result{};
	std::unordered_set<MultiRenderTarget*> visited{};
	for (const auto& entry : entries_) {
		if (!entry.surface) {
			continue;
		}
		if (visited.insert(entry.surface).second) {
			result.emplace_back(entry.surface);
		}
	}
	return result;
}

std::optional<Engine::MultiRenderTargetCreateDesc> Engine::RenderTargetRegistry::BuildCreateDesc(
	const SceneRenderTargetDesc& desc, uint32_t viewWidth, uint32_t viewHeight) {

	// モードに応じてサイズを検証する
	auto size = desc.sizeMode == SceneRenderTargetSizeMode::Fixed ?
		RenderTargetSizing::ResolveSize(desc.fixedWidth, desc.fixedHeight) :
		RenderTargetSizing::ResolveSize(viewWidth, viewHeight, desc.widthScale, desc.heightScale);
	if (!size) {
		return std::nullopt;
	}

	MultiRenderTargetCreateDesc createDesc{};
	createDesc.width = size->width;
	createDesc.height = size->height;

	// 色レンダーテクスチャの情報を構築する
	createDesc.colors.reserve(desc.colors.size());

	for (const auto& colorDesc : desc.colors) {

		ColorAttachmentDesc color{};
		color.name = colorDesc.name;
		color.format = ToColorFormat(colorDesc.format);
		color.clearColor = colorDesc.clearColor.value_or(Color4::Black());
		color.createUAV = colorDesc.createUAV;
		createDesc.colors.emplace_back(std::move(color));
	}

	// 深度レンダーテクスチャの情報を構築する
	if (desc.withDepth) {

		DepthTextureCreateDesc depth{};
		depth.width = size->width;
		depth.height = size->height;
		depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
		depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		std::string depthName = GetEffectiveDepthName(desc).value_or("Depth");
		depth.debugName = std::wstring(depthName.begin(), depthName.end());

		createDesc.depth = depth;
	}

	return createDesc;
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::ResizeTransient(GraphicsCore& graphicsCore,
	const SceneRenderTargetDesc& desc, uint32_t viewWidth, uint32_t viewHeight) {

	// 不正な指定ではGraphicsの初期化状態に触れない
	auto createDesc = BuildCreateDesc(desc, viewWidth, viewHeight);
	if (desc.name.empty() || !createDesc) {
		return nullptr;
	}
	RenderTargetCreationContext context{graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetRTVDescriptor(),
		graphicsCore.GetDSVDescriptor(), graphicsCore.GetSRVDescriptor()};
	return PublishTransient(context, desc, *createDesc);
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::ResizeTransient(const RenderTargetCreationContext& context,
	const SceneRenderTargetDesc& desc, uint32_t viewWidth, uint32_t viewHeight) {

	if (desc.name.empty()) {
		return nullptr;
	}

	// 不正なサイズでは既存の描画先を変更しない
	auto resolvedDesc = BuildCreateDesc(desc, viewWidth, viewHeight);
	if (!resolvedDesc) {
		return nullptr;
	}
	return PublishTransient(context, desc, *resolvedDesc);
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::PublishTransient(const RenderTargetCreationContext& context,
	const SceneRenderTargetDesc& desc, const MultiRenderTargetCreateDesc& createDesc) {

	// すでに同名のエントリーが存在するか
	auto found = transients_.find(desc.name);
	bool needsCreate = false;
	if (found == transients_.end()) {

		needsCreate = true;
	} else {

		const SceneRenderTargetDesc& oldDesc = found->second.desc;
		// サイズや構成が変わっていたら再生成する
		needsCreate = (found->second.resolvedWidth != createDesc.width) ||
			(found->second.resolvedHeight != createDesc.height) ||
			(oldDesc.sizeMode != desc.sizeMode) ||
			(oldDesc.widthScale != desc.widthScale) ||
			(oldDesc.heightScale != desc.heightScale) ||
			(oldDesc.fixedWidth != desc.fixedWidth) ||
			(oldDesc.fixedHeight != desc.fixedHeight) ||
			(oldDesc.withDepth != desc.withDepth) ||
			(!AreSameColorAttachmentDescs(oldDesc.colors, desc.colors));
	}

	// 条件が変わったらサーフェイスを再生成
	if (needsCreate) {

		// 新しい描画先が完成するまで旧資源を保持する
		TransientEntry candidate{};
		candidate.desc = desc;
		candidate.resolvedWidth = createDesc.width;
		candidate.resolvedHeight = createDesc.height;
		candidate.surface = std::make_unique<MultiRenderTarget>();
		candidate.surface->Create(context.device, &context.targets, &context.depths, &context.shaders, createDesc);
		static_assert(std::is_nothrow_swappable_v<TransientEntry>);

		// 登録に失敗した空の所有枠だけ取り除く
		auto [slot, inserted] = transients_.try_emplace(desc.name);
		try {
			RegisterOrUpdate(desc.name, candidate.surface.get(), GetEffectiveColorNames(desc), GetEffectiveDepthName(desc));
		} catch (...) {
			if (inserted) {
				transients_.erase(slot);
			}
			throw;
		}
		// 公開後に所有を移し、旧資源を回収へ渡す
		std::swap(slot->second, candidate);
		return slot->second.surface.get();
	}

	// レンダーターゲットセットを登録表に登録する
	MultiRenderTarget* surface = found->second.surface.get();
	RegisterOrUpdate(desc.name, surface, GetEffectiveColorNames(desc), GetEffectiveDepthName(desc));
	return surface;
}

DXGI_FORMAT Engine::RenderTargetRegistry::ToColorFormat(SceneRenderTargetFormat format) {

	switch (format) {
	case SceneRenderTargetFormat::R8_UNORM:
		return DXGI_FORMAT_R8_UNORM;
	case SceneRenderTargetFormat::R16_FLOAT:
		return DXGI_FORMAT_R16_FLOAT;
	case SceneRenderTargetFormat::RG16_FLOAT:
		return DXGI_FORMAT_R16G16_FLOAT;
	case SceneRenderTargetFormat::RGBA8_UNORM:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case SceneRenderTargetFormat::RGBA16_FLOAT:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case SceneRenderTargetFormat::R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;
	case SceneRenderTargetFormat::RG32_FLOAT:
		return DXGI_FORMAT_R32G32_FLOAT;
	case SceneRenderTargetFormat::RGBA32_FLOAT:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	}
	return DXGI_FORMAT_R16G16B16A16_FLOAT;
}
