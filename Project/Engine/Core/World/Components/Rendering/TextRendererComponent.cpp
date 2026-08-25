#include "TextRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <vector>

//============================================================================
//	TextRendererComponent classMethods
//============================================================================
namespace {

	void ReadTextRendererSettings(const nlohmann::json& in,
		Engine::TextRendererComponent& component) {

		component.font = Engine::ParseAssetID(in, "font");
		component.material = Engine::ParseAssetID(in, "material");
		Engine::ReadMaterialInstance(
			in.value("materialInstance", nlohmann::json::object()),
			component.materialInstance);
		component.text = in.value("text", component.text);
		component.fontSize = in.value("fontSize", component.fontSize);
		component.charSpacing = in.value("charSpacing", component.charSpacing);
		if (in.contains("pivot")) {
			component.pivot = Engine::Vector2::FromJson(in["pivot"]);
		}
		component.uvPerCharacter =
			in.value("uvPerCharacter", component.uvPerCharacter);
		Engine::ReadRenderCommonFields(in, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
		component.renderingLayerMask = in.value(
			"renderingLayerMask", component.renderingLayerMask) &
			Engine::kRenderingLayerMaskBits;
		component.dimension = static_cast<Engine::Dimension>(
			in.value("dimension", static_cast<int>(component.dimension)));
		component.worldScale = in.value("worldScale", component.worldScale);
	}

	void WriteTextRendererSettings(nlohmann::json& out,
		const Engine::TextRendererComponent& component) {

		out["font"] = Engine::ToAssetReferenceJson(component.font);
		out["material"] = Engine::ToAssetReferenceJson(component.material);
		out["materialInstance"] =
			Engine::WriteMaterialInstance(component.materialInstance);
		out["text"] = component.text;
		out["fontSize"] = component.fontSize;
		out["charSpacing"] = component.charSpacing;
		out["pivot"] = component.pivot.ToJson();
		out["uvPerCharacter"] = component.uvPerCharacter;
		Engine::WriteRenderCommonFields(out, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
		out["renderingLayerMask"] = component.renderingLayerMask &
			Engine::kRenderingLayerMaskBits;
		out["dimension"] = static_cast<int>(component.dimension);
		out["worldScale"] = component.worldScale;
	}

	std::vector<Engine::TextCharTransform> ReadTextCharTransforms(
		const nlohmann::json& in) {

		std::vector<Engine::TextCharTransform> transforms{};
		if (!in.contains("charTransforms") ||
			!in["charTransforms"].is_array()) {
			return transforms;
		}
		transforms.reserve(in["charTransforms"].size());
		for (const nlohmann::json& charData : in["charTransforms"]) {

			Engine::TextCharTransform transform{};
			transform.translation = Engine::Vector2(
				charData.value("tx", 0.0f), charData.value("ty", 0.0f));
			transform.rotation = charData.value("rotation", 0.0f);
			transform.scale = Engine::Vector2(
				charData.value("sx", 1.0f), charData.value("sy", 1.0f));
			transforms.emplace_back(transform);
		}
		return transforms;
	}
}

void Engine::TextRendererComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] TextRendererComponent& component) {

	if (!world.HasBuffer<TextCharTransform>(entity)) {
		world.AddBuffer<TextCharTransform>(entity);
	}
	if (!world.HasBuffer<TextLayoutGlyph>(entity)) {
		world.AddBuffer<TextLayoutGlyph>(entity);
	}
	if (!world.HasComponent<TextLayoutRuntimeComponent>(entity)) {
		world.AddComponent<TextLayoutRuntimeComponent>(entity);
	}
}

void Engine::TextRendererComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasBuffer<TextCharTransform>(entity)) {
		world.RemoveBuffer<TextCharTransform>(entity);
	}
	if (world.HasBuffer<TextLayoutGlyph>(entity)) {
		world.RemoveBuffer<TextLayoutGlyph>(entity);
	}
	if (world.HasComponent<TextLayoutRuntimeComponent>(entity)) {
		world.RemoveComponent<TextLayoutRuntimeComponent>(entity);
	}
}

void Engine::TextRendererComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] TextRendererComponent& component) {
}

void Engine::TextRendererComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] TextRendererComponent& component) {
}

void Engine::TextRendererComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	TextRendererComponent& component) {

	ReadTextRendererSettings(in, component);
	const std::vector<TextCharTransform> transforms =
		ReadTextCharTransforms(in);
	SetTextCharTransforms(world, entity, transforms);
	InvalidateTextLayout(world, entity);
}

void Engine::TextRendererComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	const TextRendererComponent& component, nlohmann::json& out) {

	SerializeTextRenderer(
		component, GetTextCharTransforms(world, entity), out);
}

void Engine::from_json(const nlohmann::json& in, TextRendererComponent& component) {

	ReadTextRendererSettings(in, component);
}

void Engine::to_json(nlohmann::json& out, const TextRendererComponent& component) {

	WriteTextRendererSettings(out, component);
	out["charTransforms"] = nlohmann::json::array();
}

std::span<Engine::TextCharTransform> Engine::GetTextCharTransforms(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<TextCharTransform>(entity).GetSpan();
}

std::span<const Engine::TextCharTransform> Engine::GetTextCharTransforms(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<TextCharTransform>(entity);
}

void Engine::SetTextCharTransforms(ECSWorld& world, const Entity& entity,
	std::span<const TextCharTransform> transforms) {

	DynamicBuffer<TextCharTransform> buffer =
		world.TryGetBuffer<TextCharTransform>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<TextCharTransform>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(transforms.size()));
	for (const TextCharTransform& transform : transforms) {
		buffer.Add(transform);
	}
	world.MarkComponentModified<TextCharTransform>(entity);
}

std::span<Engine::TextLayoutGlyph> Engine::GetTextLayoutGlyphs(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<TextLayoutGlyph>(entity).GetSpan();
}

std::span<const Engine::TextLayoutGlyph> Engine::GetTextLayoutGlyphs(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<TextLayoutGlyph>(entity);
}

void Engine::SetTextLayoutGlyphs(ECSWorld& world, const Entity& entity,
	std::span<const TextLayoutGlyph> glyphs) {

	DynamicBuffer<TextLayoutGlyph> buffer =
		world.TryGetBuffer<TextLayoutGlyph>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<TextLayoutGlyph>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(glyphs.size()));
	for (const TextLayoutGlyph& glyph : glyphs) {
		buffer.Add(glyph);
	}
	world.MarkComponentModified<TextLayoutGlyph>(entity);
}

void Engine::InvalidateTextLayout(ECSWorld& world, const Entity& entity) {

	if (TextLayoutRuntimeComponent* runtime =
		world.TryGetComponent<TextLayoutRuntimeComponent>(entity)) {
		runtime->valid = false;
	}
}

uint64_t Engine::HashTextLayoutString(std::string_view text) {

	uint64_t hash = 14695981039346656037ull;
	for (unsigned char value : text) {
		hash ^= value;
		hash *= 1099511628211ull;
	}
	return hash;
}

Engine::TextGlyphGeometry Engine::ResolveTextGlyphGeometry(
	const TextRendererComponent& renderer,
	const TextLayoutRuntimeComponent& layout,
	const TextLayoutGlyph& glyph,
	const TextCharTransform* charTransform,
	const Matrix4x4& worldMatrix) {

	const Vector2 pivotOffset(
		-renderer.pivot.x * layout.boundsSize.x,
		-renderer.pivot.y * layout.boundsSize.y);
	TextGlyphGeometry geometry{};
	geometry.rectMin = glyph.rectMin + pivotOffset;
	geometry.rectMax = glyph.rectMax + pivotOffset;
	geometry.worldMatrix = worldMatrix;
	if (!charTransform) {
		return geometry;
	}

	const Vector2 pivot(
		(geometry.rectMin.x + geometry.rectMax.x) * 0.5f,
		(geometry.rectMin.y + geometry.rectMax.y) * 0.5f);
	const Matrix4x4 toOrigin = Matrix4x4::MakeAffineMatrix(
		Vector3(1.0f, 1.0f, 1.0f), Vector3(0.0f, 0.0f, 0.0f),
		Vector3(-pivot.x, -pivot.y, 0.0f));
	const Matrix4x4 srt = Matrix4x4::MakeAffineMatrix(
		Vector3(charTransform->scale.x, charTransform->scale.y, 1.0f),
		Vector3(0.0f, 0.0f, charTransform->rotation),
		Vector3(pivot.x + charTransform->translation.x,
			pivot.y + charTransform->translation.y, 0.0f));
	geometry.worldMatrix = (toOrigin * srt) * worldMatrix;
	return geometry;
}

void Engine::SerializeTextRenderer(const TextRendererComponent& component,
	std::span<const TextCharTransform> transforms, nlohmann::json& out) {

	WriteTextRendererSettings(out, component);
	out["charTransforms"] = nlohmann::json::array();
	for (const TextCharTransform& charTransform : transforms) {

		nlohmann::json charData;
		charData["tx"] = charTransform.translation.x;
		charData["ty"] = charTransform.translation.y;
		charData["rotation"] = charTransform.rotation;
		charData["sx"] = charTransform.scale.x;
		charData["sy"] = charTransform.scale.y;
		out["charTransforms"].push_back(charData);
	}
}
