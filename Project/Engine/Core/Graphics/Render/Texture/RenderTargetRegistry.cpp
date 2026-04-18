#include "RenderTargetRegistry.h"

//============================================================================
//	RenderTargetRegistry classMethods
//============================================================================

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
	// シーンレンダーターゲットのカラー出力の情報を取得する
	std::vector<Engine::SceneRenderTargetColorDesc> GetEffectiveSceneRenderTargetColors(
		const Engine::SceneRenderTargetDesc& desc) {

		if (!desc.colors.empty()) {
			return desc.colors;
		}

		Engine::SceneRenderTargetColorDesc legacy{};
		legacy.name = desc.name.empty() ? "Color0" : desc.name;
		legacy.format = desc.colorFormat;
		legacy.createUAV = desc.createUAV;
		return { legacy };
	}
	// 色アタッチメントの名前を取得する
	std::vector<std::string> GetEffectiveColorNames(const Engine::SceneRenderTargetDesc& desc) {

		std::vector<std::string> result{};
		for (const auto& color : GetEffectiveSceneRenderTargetColors(desc)) {
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
		const auto colors = GetEffectiveSceneRenderTargetColors(desc);
		if (!colors.empty()) {
			return colors.front().name + ".Depth";
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

bool Engine::RegisteredRenderTargetSet::Matches(const RenderTargetSetReference& reference) const {

	if (!surface) {
		return false;
	}

	// ビューならtrueを返す
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
	if (reference.depth.has_value() && depthName.has_value()) {
		if (*reference.depth != *depthName) {
			return false;
		}
	}
	return true;
}

void Engine::RenderTargetRegistry::Clear() {

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

	auto found = aliasTable_.find(alias);
	if (found == aliasTable_.end()) {

		// レンダーターゲットセットのエントリーを構築する
		RegisteredRenderTargetSet entry{};
		entry.alias = std::move(alias);
		entry.surface = surface;
		entry.colorNames = colorNames;
		entry.depthName = depthName;
		// 登録
		aliasTable_[entry.alias] = entries_.size();
		entries_.emplace_back(std::move(entry));
		return;
	}
	// すでに登録されているエントリーを更新する
	RegisteredRenderTargetSet& entry = entries_[found->second];
	entry.surface = surface;
	entry.colorNames = colorNames;
	entry.depthName = depthName;
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::Find(const std::string& alias) const {

	auto it = aliasTable_.find(alias);
	if (it == aliasTable_.end()) {
		return nullptr;
	}
	return entries_[it->second].surface;
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::Resolve(
	const RenderTargetSetReference& reference) const {

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

Engine::MultiRenderTargetCreateDesc Engine::RenderTargetRegistry::BuildCreateDesc(
	const SceneRenderTargetDesc& desc, uint32_t viewWidth, uint32_t viewHeight) {

	uint32_t width = 1;
	uint32_t height = 1;

	// モードに応じてサイズを決定する
	if (desc.sizeMode == SceneRenderTargetSizeMode::Fixed) {

		width = (std::max)(1u, desc.fixedWidth);
		height = (std::max)(1u, desc.fixedHeight);
	} else {

		width = (std::max)(1u, static_cast<uint32_t>(static_cast<float>(viewWidth) * desc.widthScale));
		height = (std::max)(1u, static_cast<uint32_t>(static_cast<float>(viewHeight) * desc.heightScale));
	}

	MultiRenderTargetCreateDesc createDesc{};
	createDesc.width = width;
	createDesc.height = height;

	// 色レンダーテクスチャの情報を構築する
	const auto colorDescs = GetEffectiveSceneRenderTargetColors(desc);
	createDesc.colors.reserve(colorDescs.size());

	for (const auto& colorDesc : colorDescs) {

		ColorAttachmentDesc color{};
		color.name = colorDesc.name;
		color.format = ToColorFormat(colorDesc.format);
		color.clearColor = colorDesc.clearColor.value_or(Color::Black());
		color.createUAV = colorDesc.createUAV;
		createDesc.colors.emplace_back(std::move(color));
	}

	// 深度レンダーテクスチャの情報を構築する
	if (desc.withDepth) {

		DepthTextureCreateDesc depth{};
		depth.width = width;
		depth.height = height;
		depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
		depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		const std::string depthName = GetEffectiveDepthName(desc).value_or("Depth");
		depth.debugName = std::wstring(depthName.begin(), depthName.end());

		createDesc.depth = depth;
	}

	return createDesc;
}

Engine::MultiRenderTarget* Engine::RenderTargetRegistry::ResizeTransient(GraphicsCore& graphicsCore,
	const SceneRenderTargetDesc& desc, uint32_t viewWidth, uint32_t viewHeight) {

	if (desc.name.empty()) {
		return nullptr;
	}

	MultiRenderTargetCreateDesc createDesc = BuildCreateDesc(desc, viewWidth, viewHeight);

	// すでに同名のエントリーが存在するか
	auto found = transients_.find(desc.name);
	bool needsCreate = false;
	if (found == transients_.end()) {

		needsCreate = true;
	} else {

		const SceneRenderTargetDesc& oldDesc = found->second.desc;
		auto oldColors = GetEffectiveSceneRenderTargetColors(oldDesc);
		auto newColors = GetEffectiveSceneRenderTargetColors(desc);

		// サイズや構成が変わっていたら再生成する
		needsCreate = (found->second.resolvedWidth != createDesc.width) ||
			(found->second.resolvedHeight != createDesc.height) ||
			(oldDesc.sizeMode != desc.sizeMode) ||
			(oldDesc.widthScale != desc.widthScale) ||
			(oldDesc.heightScale != desc.heightScale) ||
			(oldDesc.fixedWidth != desc.fixedWidth) ||
			(oldDesc.fixedHeight != desc.fixedHeight) ||
			(oldDesc.withDepth != desc.withDepth) ||
			(!AreSameColorAttachmentDescs(oldColors, newColors));
	}

	// 条件が変わったらサーフェイスを再生成
	if (needsCreate) {

		TransientEntry entry{};
		entry.desc = desc;
		entry.resolvedWidth = createDesc.width;
		entry.resolvedHeight = createDesc.height;
		entry.surface = std::make_unique<MultiRenderTarget>();
		entry.surface->Create(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetRTVDescriptor(),
			&graphicsCore.GetDSVDescriptor(), &graphicsCore.GetSRVDescriptor(), createDesc);
		transients_[desc.name] = std::move(entry);
	}

	// レンダーターゲットセットを登録表に登録する
	MultiRenderTarget* surface = transients_[desc.name].surface.get();
	RegisterOrUpdate(desc.name, surface, GetEffectiveColorNames(desc), GetEffectiveDepthName(desc));
	return surface;
}

DXGI_FORMAT Engine::RenderTargetRegistry::ToColorFormat(SceneRenderTargetFormat format) {

	switch (format) {
	case SceneRenderTargetFormat::RGBA8_UNORM:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case SceneRenderTargetFormat::RGBA16_FLOAT:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case SceneRenderTargetFormat::RGBA32_FLOAT:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	}
	return DXGI_FORMAT_R16G16B16A16_FLOAT;
}