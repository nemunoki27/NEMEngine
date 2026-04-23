#include "SceneHeader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/UUID/UUID.h>

//============================================================================
//	SceneHeader classMethods
//============================================================================

namespace {

	Engine::RenderTargetSetReference ParseRenderTargetSet(const nlohmann::json& data) {

		Engine::RenderTargetSetReference out{};

		if (data.is_object()) {
			if (data.contains("colors") && data["colors"].is_array()) {
				for (const auto& color : data["colors"]) {
					if (color.is_string()) {
						out.colors.push_back(color.get<std::string>());
					}
				}
			}
			if (data.contains("depth") && data["depth"].is_string()) {
				out.depth = data["depth"].get<std::string>();
			}
			return out;
		}

		if (data.is_array()) {
			for (const auto& color : data) {
				if (color.is_string()) {
					out.colors.push_back(color.get<std::string>());
				}
			}
			return out;
		}

		if (data.is_string()) {
			out.colors.push_back(data.get<std::string>());
			return out;
		}
		return out;
	}
	nlohmann::json RenderTargetSetToJson(const Engine::RenderTargetSetReference& reference) {

		nlohmann::json data = nlohmann::json::object();
		data["colors"] = nlohmann::json::array();
		for (const auto& color : reference.colors) {
			data["colors"].push_back(color);
		}
		if (reference.depth.has_value()) {
			data["depth"] = *reference.depth;
		}
		return data;
	}
	Engine::SceneRenderTargetColorDesc ParseSceneRenderTargetColorDesc(const nlohmann::json& data,
		const std::string& ownerName, size_t colorIndex) {

		Engine::SceneRenderTargetColorDesc color{};

		if (data.is_string()) {

			color.name = data.get<std::string>();
		} else if (data.is_object()) {

			color.name = data.value("name", "");
			color.format = Engine::EnumAdapter<Engine::SceneRenderTargetFormat>::FromString(
				data.value("format", "RGBA32_FLOAT")).value();
			color.createUAV = data.value("createUAV", false);

			if (data.contains("clearColor") && !data["clearColor"].is_null()) {
				color.clearColor = Engine::Color::FromJson(data["clearColor"]);
			}
		}
		if (color.name.empty()) {
			if (!ownerName.empty() && colorIndex == 0) {
				color.name = ownerName;
			} else if (!ownerName.empty()) {
				color.name = ownerName + ".Color" + std::to_string(colorIndex);
			} else {
				color.name = "Color" + std::to_string(colorIndex);
			}
		}
		return color;
	}
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

	Engine::SceneRenderTargetDesc ParseSceneRenderTargetDesc(const nlohmann::json& data) {

		Engine::SceneRenderTargetDesc desc{};
		desc.name = data.value("name", "");
		desc.sizeMode = Engine::EnumAdapter<Engine::SceneRenderTargetSizeMode>::FromString(
			data.value("sizeMode", "ViewRelative")).value();

		desc.widthScale = data.value("widthScale", 1.0f);
		desc.heightScale = data.value("heightScale", 1.0f);
		desc.fixedWidth = data.value("fixedWidth", 0u);
		desc.fixedHeight = data.value("fixedHeight", 0u);
		desc.withDepth = data.value("withDepth", false);

		desc.colors.clear();
		if (data.contains("colors") && data["colors"].is_array()) {

			size_t colorIndex = 0;
			for (const auto& colorData : data["colors"]) {

				desc.colors.emplace_back(
					ParseSceneRenderTargetColorDesc(colorData, desc.name, colorIndex));
				++colorIndex;
			}
		}

		desc.colorFormat = Engine::EnumAdapter<Engine::SceneRenderTargetFormat>::FromString(
			data.value("colorFormat", "RGBA32_FLOAT")).value();
		desc.createUAV = data.value("createUAV", false);

		if (desc.colors.empty()) {

			Engine::SceneRenderTargetColorDesc legacy{};
			legacy.name = desc.name.empty() ? "Color0" : desc.name;
			legacy.format = desc.colorFormat;
			legacy.createUAV = desc.createUAV;
			desc.colors.emplace_back(std::move(legacy));
		}

		if (desc.name.empty() && !desc.colors.empty()) {
			desc.name = desc.colors.front().name;
		}

		if (!desc.colors.empty()) {
			desc.colorFormat = desc.colors.front().format;
			desc.createUAV = desc.colors.front().createUAV;
		}
		return desc;
	}

	nlohmann::json SceneRenderTargetDescToJson(const Engine::SceneRenderTargetDesc& renderTarget) {

		nlohmann::json item = nlohmann::json::object();
		item["name"] = renderTarget.name;
		item["sizeMode"] = Engine::EnumAdapter<Engine::SceneRenderTargetSizeMode>::ToString(renderTarget.sizeMode);
		item["widthScale"] = renderTarget.widthScale;
		item["heightScale"] = renderTarget.heightScale;
		item["fixedWidth"] = renderTarget.fixedWidth;
		item["fixedHeight"] = renderTarget.fixedHeight;
		item["withDepth"] = renderTarget.withDepth;

		item["colors"] = nlohmann::json::array();
		for (const auto& color : GetEffectiveSceneRenderTargetColors(renderTarget)) {

			nlohmann::json colorItem = nlohmann::json::object();
			colorItem["name"] = color.name;
			colorItem["format"] = Engine::EnumAdapter<Engine::SceneRenderTargetFormat>::ToString(color.format);
			colorItem["createUAV"] = color.createUAV;

			if (color.clearColor.has_value()) {
				colorItem["clearColor"] = color.clearColor->ToJson();
			} else {
				colorItem["clearColor"] = nullptr;
			}

			item["colors"].push_back(colorItem);
		}
		return item;
	}

	Engine::ClearPassDesc ParseClearPass(const nlohmann::json& data) {

		Engine::ClearPassDesc pass{};
		if (data.contains("dest")) {

			pass.dest = ParseRenderTargetSet(data["dest"]);
		}
		pass.clearColor = data.value("clearColor", true);
		if (data.contains("clearColorValue") && !data["clearColorValue"].is_null()) {

			pass.clearColorValue = Engine::Color::FromJson(data["clearColorValue"]);
		} else {

			pass.clearColorValue = std::nullopt;
		}
		pass.clearDepth = data.value("clearDepth", false);
		pass.clearDepthValue = data.value("clearDepthValue", 1.0f);
		pass.clearStencil = data.value("clearStencil", false);
		pass.clearStencilValue = static_cast<uint8_t>(data.value("clearStencilValue", 0));
		return pass;
	}
}

bool Engine::FromJson(const nlohmann::json& data, SceneHeader& sceneHeader, AssetDatabase* assetDatabase) {

	// JSONがオブジェクトでない場合は失敗
	if (!data.is_object()) {
		return false;
	}

	// JSONからシーンヘッダーの情報を取得する
	{
		std::string guidStr = data.value("guid", "");
		sceneHeader.guid = guidStr.empty() ? UUID::New() : FromString16Hex(guidStr);
		sceneHeader.name = data.value("name", "UntitledScene");
	}

	// サブシーン
	sceneHeader.subScenes.clear();
	if (data.contains("subScenes") && data["subScenes"].is_array()) {

		size_t generatedIndex = 0;
		for (const auto& item : data["subScenes"]) {

			SubSceneSlotDesc desc{};
			if (item.is_string()) {

				desc.slotName = "SubScene" + std::to_string(generatedIndex++);
				nlohmann::json temp = { {"sceneAsset", item.get<std::string>()} };
				desc.sceneAsset = ParseAssetReference(temp, "sceneAsset", assetDatabase, AssetType::Scene);
				desc.enabled = true;
			} else if (item.is_object()) {

				desc.slotName = item.value("slotName", "SubScene" + std::to_string(generatedIndex++));
				desc.sceneAsset = ParseAssetReference(item, "sceneAsset", assetDatabase, AssetType::Scene);
				desc.enabled = item.value("enabled", true);
			} else {
				continue;
			}
			if (desc.slotName.empty()) {
				desc.slotName = "SubScene" + std::to_string(generatedIndex++);
			}
			if (!desc.sceneAsset) {
				continue;
			}
			sceneHeader.subScenes.emplace_back(std::move(desc));
		}
	}

	// 描画ターゲット
	sceneHeader.renderTargets.clear();
	if (data.contains("renderTargets") && data["renderTargets"].is_array()) {
		for (const auto& renderTarget : data["renderTargets"]) {

			sceneHeader.renderTargets.emplace_back(ParseSceneRenderTargetDesc(renderTarget));
		}
	}

	// 描画パス
	sceneHeader.passOrder.clear();
	if (data.contains("passOrder") && data["passOrder"].is_array()) {
		for (const auto& pass : data["passOrder"]) {

			ScenePassDesc passDesc{};
			passDesc.type = EnumAdapter<ScenePassType>::FromString(pass.value("type", "Draw")).value();
			switch (passDesc.type) {
			case ScenePassType::Clear:
				passDesc.clear = ParseClearPass(pass);
				break;
			case ScenePassType::DepthPrepass:
				passDesc.depthPrepass.queue = pass.value("queue", "Opaque");
				passDesc.depthPrepass.passName = pass.value("passName", "ZPrepass");
				if (pass.contains("dest")) {
					passDesc.depthPrepass.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			case ScenePassType::Draw:
				passDesc.draw.queue = pass.value("queue", "");
				if (pass.contains("dest")) {
					passDesc.draw.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			case ScenePassType::PostProcess:
				passDesc.postProcess.material = ParseAssetReference(pass, "material", assetDatabase, AssetType::Material);
				if (pass.contains("source")) {
					passDesc.postProcess.source = ParseRenderTargetSet(pass["source"]);
				}
				if (pass.contains("dest")) {
					passDesc.postProcess.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			case ScenePassType::Compute:
				passDesc.compute.material = ParseAssetReference(pass, "material", assetDatabase, AssetType::Material);
				passDesc.compute.passName = pass.value("passName", "Compute");
				passDesc.compute.dispatchMode = EnumAdapter<ComputeDispatchMode>::FromString(
					pass.value("dispatchMode", "FromDestSize")).value();
				passDesc.compute.groupCountX = pass.value("groupCountX", 1u);
				passDesc.compute.groupCountY = pass.value("groupCountY", 1u);
				passDesc.compute.groupCountZ = pass.value("groupCountZ", 1u);
				if (pass.contains("source")) {
					passDesc.compute.source = ParseRenderTargetSet(pass["source"]);
				}
				if (pass.contains("dest")) {
					passDesc.compute.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			case ScenePassType::RenderScene:
				passDesc.renderScene.subSceneSlot = pass.value("subSceneSlot", "");
				if (pass.contains("dest")) {
					passDesc.renderScene.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			case ScenePassType::Blit:
				passDesc.blit.material = ParseAssetReference(pass, "material", assetDatabase, AssetType::Material);
				if (pass.contains("source")) {
					passDesc.blit.source = ParseRenderTargetSet(pass["source"]);
				}
				if (pass.contains("dest")) {
					passDesc.blit.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			case ScenePassType::Raytracing:
				passDesc.raytracing.material = ParseAssetReference(pass, "material", assetDatabase, AssetType::Material);
				passDesc.raytracing.passName = pass.value("passName", "Raytracing");
				if (pass.contains("source")) {
					passDesc.raytracing.source = ParseRenderTargetSet(pass["source"]);
				}
				if (pass.contains("dest")) {
					passDesc.raytracing.dest = ParseRenderTargetSet(pass["dest"]);
				}
				break;
			}
			sceneHeader.passOrder.push_back(std::move(passDesc));
		}
	}
	return true;
}

nlohmann::json Engine::ToJson(const SceneHeader& sceneHeader) {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = ToString(sceneHeader.guid);
	data["name"] = sceneHeader.name;

	data["subScenes"] = nlohmann::json::array();
	for (const auto& subScene : sceneHeader.subScenes) {
		nlohmann::json item = nlohmann::json::object();
		item["slotName"] = subScene.slotName;
		item["sceneAsset"] = ToString(subScene.sceneAsset);
		item["enabled"] = subScene.enabled;
		data["subScenes"].push_back(item);
	}

	data["renderTargets"] = nlohmann::json::array();
	for (const auto& renderTarget : sceneHeader.renderTargets) {

		data["renderTargets"].push_back(SceneRenderTargetDescToJson(renderTarget));
	}

	data["passOrder"] = nlohmann::json::array();
	for (const auto& pass : sceneHeader.passOrder) {

		nlohmann::json item = nlohmann::json::object();
		item["type"] = EnumAdapter<ScenePassType>::ToString(pass.type);

		switch (pass.type) {
		case ScenePassType::Clear:
			item["dest"] = RenderTargetSetToJson(pass.clear.dest);
			item["clearColor"] = pass.clear.clearColor;
			if (pass.clear.clearColorValue.has_value()) {

				item["clearColorValue"] = pass.clear.clearColorValue->ToJson();
			} else {
				item["clearColorValue"] = nullptr;
			}
			item["clearDepth"] = pass.clear.clearDepth;
			item["clearDepthValue"] = pass.clear.clearDepthValue;
			item["clearStencil"] = pass.clear.clearStencil;
			item["clearStencilValue"] = pass.clear.clearStencilValue;
			break;
		case ScenePassType::DepthPrepass:
			item["queue"] = pass.depthPrepass.queue;
			item["passName"] = pass.depthPrepass.passName;
			item["dest"] = RenderTargetSetToJson(pass.depthPrepass.dest);
			break;
		case ScenePassType::Draw:
			item["queue"] = pass.draw.queue;
			item["dest"] = RenderTargetSetToJson(pass.draw.dest);
			break;
		case ScenePassType::PostProcess:
			item["material"] = ToString(pass.postProcess.material);
			item["source"] = RenderTargetSetToJson(pass.postProcess.source);
			item["dest"] = RenderTargetSetToJson(pass.postProcess.dest);
			break;
		case ScenePassType::Compute:
			item["material"] = ToString(pass.compute.material);
			item["passName"] = pass.compute.passName;
			item["dispatchMode"] = EnumAdapter<ComputeDispatchMode>::ToString(pass.compute.dispatchMode);
			item["groupCountX"] = pass.compute.groupCountX;
			item["groupCountY"] = pass.compute.groupCountY;
			item["groupCountZ"] = pass.compute.groupCountZ;
			item["source"] = RenderTargetSetToJson(pass.compute.source);
			item["dest"] = RenderTargetSetToJson(pass.compute.dest);
			break;
		case ScenePassType::RenderScene:
			item["subSceneSlot"] = pass.renderScene.subSceneSlot;
			item["dest"] = RenderTargetSetToJson(pass.renderScene.dest);
			break;
		case ScenePassType::Blit:
			item["material"] = ToString(pass.blit.material);
			item["source"] = RenderTargetSetToJson(pass.blit.source);
			item["dest"] = RenderTargetSetToJson(pass.blit.dest);
			break;
		case ScenePassType::Raytracing:
			item["material"] = ToString(pass.raytracing.material);
			item["passName"] = pass.raytracing.passName;
			item["source"] = RenderTargetSetToJson(pass.raytracing.source);
			item["dest"] = RenderTargetSetToJson(pass.raytracing.dest);
			break;
		}
		data["passOrder"].push_back(item);
	}
	return data;
}
