#include "MeshSubMeshAuthoring.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace {

	// モデルのマテリアル係数とテクスチャをmaterialInstanceへ流す、overwrite=falseは未設定のみ
	bool ApplyLayoutItemToSubMesh(Engine::SubMeshMaterial& subMesh,
		const Engine::MeshSubMeshLayoutItem& item, bool overwrite) {

		bool changed = false;
		auto setParam = [&](std::string_view name,
			const Engine::MaterialParameterValue& value) {

			const Engine::MaterialParameterID id =
				Engine::MaterialParameterID::FromName(name);
			if (!overwrite &&
				subMesh.materialInstance.Find(id)) {
				return;
			}
			subMesh.materialInstance.Set(
				id, name,
				Engine::ResolveMaterialParameterSemantic(name),
				value);
			changed = true;
			};
		auto setTexture = [&](std::string_view name,
			const Engine::AssetID& texture) {
			if (!texture) {
				return;
			}
			Engine::MaterialParameterValue value{};
			value.value = texture;
			setParam(name, value);
			};
		auto colorValue = [](const Engine::Color4& c) {
			Engine::MaterialParameterValue v{}; v.value = c; return v;
			};
		auto floatValue = [](float f) {
			Engine::MaterialParameterValue v{}; v.value = f; return v;
			};

		if (item.hasBaseColorFactor) { setParam(Engine::MaterialParameterNames::BaseColor, colorValue(item.baseColorFactor)); }
		if (item.hasEmissiveFactor) { setParam(Engine::MaterialParameterNames::EmissiveColor, colorValue(item.emissiveFactor)); }
		if (item.hasMetallicFactor) { setParam(Engine::MaterialParameterNames::Metallic, floatValue(item.metallicFactor)); }
		if (item.hasRoughnessFactor) { setParam(Engine::MaterialParameterNames::Roughness, floatValue(item.roughnessFactor)); }

		const auto& tex = item.defaultTextureAssets;
		setTexture(Engine::MaterialParameterNames::BaseColorTexture, tex.baseColorTexture);
		setTexture(Engine::MaterialParameterNames::NormalTexture, tex.normalTexture);
		setTexture(Engine::MaterialParameterNames::EmissiveTexture, tex.emissiveTexture);
		setTexture(Engine::MaterialParameterNames::MetallicRoughnessTexture, tex.metallicRoughnessTexture);
		setTexture(Engine::MaterialParameterNames::MetallicTexture, tex.metallicTexture);
		setTexture(Engine::MaterialParameterNames::RoughnessTexture, tex.roughnessTexture);
		setTexture(Engine::MaterialParameterNames::DisplacementTexture, tex.displacementTexture);
		setTexture(Engine::MaterialParameterNames::AmbientOcclusionTexture, tex.occlusionTexture);
		setTexture(Engine::MaterialParameterNames::OpacityTexture, tex.opacityTexture);
		setTexture(Engine::MaterialParameterNames::SpecularTexture, tex.specularTexture);
		return changed;
	}

}

bool Engine::MeshSubMeshAuthoring::SyncComponentToLayout(
	const std::vector<MeshSubMeshLayoutItem>& layout,
	std::vector<SubMeshMaterial>& subMeshes, bool preserveOverrides) {

	// 空レイアウトなら空に揃える
	if (layout.empty()) {
		if (subMeshes.empty()) {
			return false;
		}
		subMeshes.clear();
		return true;
	}

	// 全件の対応を確認してから既存値を更新する
	const bool alreadyMatched = subMeshes.size() == layout.size() &&
		std::equal(subMeshes.begin(), subMeshes.end(), layout.begin(), [](const auto& current, const auto& item) {
			return current.name == item.name && current.sourceSubMeshIndex == item.sourceSubMeshIndex && current.stableID;
		});
	if (alreadyMatched && preserveOverrides) {

		bool updated = false;
		for (size_t i = 0; i < layout.size(); ++i) {

			auto& current = subMeshes[i];
			if (current.sourcePivot.x != layout[i].sourcePivot.x ||
				current.sourcePivot.y != layout[i].sourcePivot.y ||
				current.sourcePivot.z != layout[i].sourcePivot.z) {

				current.sourcePivot = layout[i].sourcePivot;
				updated = true;
			}
			const bool sourceSurfaceWasUnset =
				current.sourceSurfaceMode == MaterialSurfaceMode::Auto;
			if (current.sourceSurfaceMode != layout[i].sourceSurfaceMode) {
				current.sourceSurfaceMode = layout[i].sourceSurfaceMode;
				updated = true;
			}
			if (sourceSurfaceWasUnset &&
				current.alphaCutoff != layout[i].alphaCutoff) {
				current.alphaCutoff = layout[i].alphaCutoff;
				updated = true;
			}
		}
		return updated;
	}

	const std::vector<SubMeshMaterial> oldSubMeshes = subMeshes;
	std::vector<bool> used(oldSubMeshes.size(), false);
	std::vector<int32_t> namedMatches(layout.size(), -1);
	if (preserveOverrides) {
		// 並べ替え前後で一意な名前を先に対応付ける
		std::unordered_map<std::string_view, int32_t> oldNames;
		std::unordered_map<std::string_view, size_t> newNames;
		for (size_t index = 0; index < oldSubMeshes.size(); ++index) {
			if (oldSubMeshes[index].name.empty()) continue;
			const auto [found, inserted] = oldNames.emplace(oldSubMeshes[index].name, static_cast<int32_t>(index));
			if (!inserted) found->second = -1;
		}
		for (const auto& item : layout) ++newNames[item.name];
		for (size_t index = 0; index < layout.size(); ++index) {
			const auto found = oldNames.find(layout[index].name);
			if (found != oldNames.end() && found->second >= 0 && newNames[layout[index].name] == 1) {
				namedMatches[index] = found->second;
				used[found->second] = true;
			}
		}
	}
	auto findReusableOldIndex = [&](size_t newIndex, const MeshSubMeshLayoutItem& item) -> int32_t {

		if (!preserveOverrides) {
			return -1;
		}
		if (namedMatches[newIndex] >= 0) return namedMatches[newIndex];
		for (size_t oldIndex = 0; oldIndex < oldSubMeshes.size(); ++oldIndex) {
			if (used[oldIndex]) {
				continue;
			}
			if (oldSubMeshes[oldIndex].sourceSubMeshIndex == item.sourceSubMeshIndex) {
				return static_cast<int32_t>(oldIndex);
			}
		}
		for (size_t oldIndex = 0; oldIndex < oldSubMeshes.size(); ++oldIndex) {
			if (used[oldIndex]) {
				continue;
			}
			if (oldSubMeshes[oldIndex].name == item.name) {
				return static_cast<int32_t>(oldIndex);
			}
		}
		// 同位置フォールバック
		if (newIndex < oldSubMeshes.size() && !used[newIndex]) {
			return static_cast<int32_t>(newIndex);
		}
		return -1;
		};

	// レイアウトに合わせてサブメッシュを再構築する
	std::vector<SubMeshMaterial> rebuilt{};
	rebuilt.resize(layout.size());
	for (size_t i = 0; i < layout.size(); ++i) {

		SubMeshMaterial entry{};

		const int32_t reusableOldIndex = findReusableOldIndex(i, layout[i]);
		const bool reused = 0 <= reusableOldIndex;
		if (reused) {
			// 空のTexture指定も編集値として保持する
			entry = oldSubMeshes[reusableOldIndex];
			used[reusableOldIndex] = true;
		} else {
			// 新規エントリはモデルの係数とデフォルトテクスチャで初期化
			ApplyLayoutItemToSubMesh(entry, layout[i], false);
		}

		// 正規レイアウトを上書き
		entry.name = layout[i].name;
		entry.sourceSubMeshIndex = layout[i].sourceSubMeshIndex;
		entry.sourcePivot = layout[i].sourcePivot;
		const bool sourceSurfaceWasUnset =
			entry.sourceSurfaceMode == MaterialSurfaceMode::Auto;
		entry.sourceSurfaceMode = layout[i].sourceSurfaceMode;
		if (!reused || sourceSurfaceWasUnset) {
			entry.alphaCutoff = layout[i].alphaCutoff;
		}

		// 実Entity化、選択保持のための永続ID
		if (!entry.stableID) {

			entry.stableID = UUID::New();
		}
		rebuilt[i] = std::move(entry);
	}
	subMeshes = std::move(rebuilt);
	return true;
}

bool Engine::MeshSubMeshAuthoring::SyncComponent(AssetDatabase* assetDatabase,
	AssetID meshAssetID, std::vector<SubMeshMaterial>& subMeshes,
	bool preserveOverrides) {

	if (!meshAssetID) {
		if (subMeshes.empty()) {
			return false;
		}
		subMeshes.clear();
		return true;
	}

	std::vector<MeshSubMeshLayoutItem> layout{};
	// レイアウトが解決できない時は、今の内容を壊さない
	if (!TryBuildLayout(assetDatabase, meshAssetID, layout)) {
		return false;
	}
	return SyncComponentToLayout(layout, subMeshes, preserveOverrides);
}

bool Engine::MeshSubMeshAuthoring::SyncEntity(AssetDatabase* assetDatabase,
	ECSWorld& world, const Entity& entity, bool preserveOverrides) {

	MeshRendererComponent* renderer =
		world.TryGetComponent<MeshRendererComponent>(entity);
	if (!renderer) {
		return false;
	}
	const std::span<const SubMeshMaterial> source =
		GetMeshSubMeshes(world, entity);
	std::vector<SubMeshMaterial> subMeshes(source.begin(), source.end());
	if (!SyncComponent(assetDatabase, renderer->mesh, subMeshes, preserveOverrides)) {
		return false;
	}
	SetMeshSubMeshes(world, entity, subMeshes);
	return true;
}

void Engine::MeshSubMeshAuthoring::ApplyModelMaterialParameters(
	const std::vector<MeshSubMeshLayoutItem>& layout,
	std::span<SubMeshMaterial> subMeshes) {

	for (SubMeshMaterial& subMesh : subMeshes) {

		// 元のサブメッシュインデックスでモデル側の対応itemを探す
		const MeshSubMeshLayoutItem* item = nullptr;
		for (const MeshSubMeshLayoutItem& layoutItem : layout) {
			if (layoutItem.sourceSubMeshIndex == subMesh.sourceSubMeshIndex) {
				item = &layoutItem;
				break;
			}
		}
		if (item) {
			ApplyLayoutItemToSubMesh(subMesh, *item, true);
		}
	}
}

int32_t Engine::MeshSubMeshAuthoring::FindSubMeshIndexByStableID(
	std::span<const SubMeshMaterial> subMeshes, UUID stableID) {

	if (!stableID) {
		return -1;
	}
	for (uint32_t i = 0; i < static_cast<uint32_t>(subMeshes.size()); ++i) {
		if (subMeshes[i].stableID == stableID) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}
