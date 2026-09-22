#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================

// c++
#include <charconv>
#include <algorithm>

namespace Engine::ScriptFieldInspector {

	Engine::FloatEditSetting MakeFloatSetting(const Engine::ManagedFieldSchema& field) {
		Engine::FloatEditSetting s{};
		if (field.hasDragSpeed) { s.dragSpeed = field.dragSpeed; }
		if (field.hasMin) { s.minValue = field.minValue; }
		if (field.hasRange) {
			s.minValue = field.rangeMin;
			s.maxValue = field.rangeMax;
			s.flags = ImGuiSliderFlags_AlwaysClamp;
		}
		return s;
	}

	void DrawHeaderIfAny(const Engine::ManagedFieldSchema& field) {
		if (!field.header.empty()) {
			ImGui::SeparatorText(field.header.c_str());
		}
	}

	const std::string& FieldDisplayLabel(const Engine::ManagedFieldSchema& field) {
		return field.label.empty() ? field.name : field.label;
	}

	void DrawTooltipIfAny(const Engine::ManagedFieldSchema& field) {
		if (!field.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", field.tooltip.c_str());
		}
	}

	Engine::ValueEditResult DrawTextNumber(const char* label, std::string& text) {
		return Engine::MyGUI::InputText(label, text);
	}

	Engine::ValueEditResult DrawClampedInt(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, long long lo, long long hi) {

		int v = value.is_number_integer() ? static_cast<int>(value.get<long long>()) : 0;
		Engine::IntEditSetting s{};
		if (field.hasDragSpeed) { s.dragSpeed = field.dragSpeed; }
		s.minValue = static_cast<int32_t>(std::max<long long>(lo, s.minValue));
		s.maxValue = static_cast<int32_t>(std::min<long long>(hi, s.maxValue));
		if (field.hasMin) { s.minValue = static_cast<int32_t>(std::max<long long>(lo, static_cast<long long>(field.minValue))); }
		if (field.hasRange) {
			s.minValue = static_cast<int32_t>(std::max<long long>(lo, static_cast<long long>(field.rangeMin)));
			s.maxValue = static_cast<int32_t>(std::min<long long>(hi, static_cast<long long>(field.rangeMax)));
		}
		Engine::ValueEditResult r = Engine::MyGUI::DragInt(label, v, s);
		if (r.valueChanged) {
			long long clamped = std::clamp<long long>(v, lo, hi);
			value = clamped;
		}
		return r;
	}

	Engine::ValueEditResult DrawLong(const char* label, nlohmann::json& value, bool isUnsigned) {

		std::string text;
		if (isUnsigned) {
			unsigned long long v = value.is_number_unsigned() ? value.get<unsigned long long>()
				: (value.is_number_integer() ? static_cast<unsigned long long>(value.get<long long>()) : 0ull);
			text = std::to_string(v);
		} else {
			long long v = value.is_number_integer() ? value.get<long long>() : 0;
			text = std::to_string(v);
		}
		Engine::ValueEditResult r = DrawTextNumber(label, text);
		if (r.valueChanged) {
			if (isUnsigned) {
				unsigned long long parsed = 0;
				auto res = std::from_chars(text.data(), text.data() + text.size(), parsed);
				if (res.ec == std::errc()) { value = parsed; }
			} else {
				long long parsed = 0;
				auto res = std::from_chars(text.data(), text.data() + text.size(), parsed);
				if (res.ec == std::errc()) { value = parsed; }
			}
		}
		return r;
	}

	Engine::ValueEditResult DrawDouble(const char* label, nlohmann::json& value) {

		double v = value.is_number() ? value.get<double>() : 0.0;
		std::string text = std::to_string(v);
		Engine::ValueEditResult r = DrawTextNumber(label, text);
		if (r.valueChanged) {
			double parsed = 0.0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
			if (result.ec == std::errc() && result.ptr == text.data() + text.size()) {
				value = parsed;
			}
		}
		return r;
	}

	Engine::ValueEditResult DrawEnum(const char* label, nlohmann::json& value, const Engine::ManagedFieldSchema& field) {

		Engine::ValueEditResult result{};
		long long current = value.is_number_integer() ? value.get<long long>() : 0;

		// 現在値に対応する名前を探す
		int currentIndex = -1;
		for (size_t i = 0; i < field.enumValues.size(); ++i) {
			long long parsed = 0;
			const std::string& text = field.enumValues[i];
			const auto parse = std::from_chars(text.data(), text.data() + text.size(), parsed);
			if (parse.ec == std::errc() && parse.ptr == text.data() + text.size() && parsed == current) {
				currentIndex = static_cast<int>(i);
				break;
			}
		}
		const std::string preview = currentIndex >= 0 ? field.enumNames[currentIndex]
			: ("(" + std::to_string(current) + ")");

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}
		if (ImGui::BeginCombo("##Value", preview.c_str())) {
			for (size_t i = 0; i < field.enumNames.size(); ++i) {
				const bool selected = (static_cast<int>(i) == currentIndex);
				if (ImGui::Selectable(field.enumNames[i].c_str(), selected)) {
					long long parsed = 0;
					const std::string& text = field.enumValues[i];
					const auto parse = std::from_chars(text.data(), text.data() + text.size(), parsed);
					if (parse.ec == std::errc() && parse.ptr == text.data() + text.size()) {
						value = parsed;
						result.valueChanged = true;
					}
				}
				if (selected) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged;
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	Engine::ValueEditResult DrawValue(const Engine::ManagedFieldSchema& field, nlohmann::json& value,
		const DrawContext& ctx, const char* label) {

		using namespace Engine;
		switch (field.kind) {
		case Kind::Bool: {
			bool v = value.is_boolean() ? value.get<bool>() : false;
			ValueEditResult r = InspectorDrawerCommon::DrawCheckboxField(label, v);
			if (r.valueChanged) { value = v; }
			return r;
		}
		case Kind::Byte:   return DrawClampedInt(label, value, field, 0, 255);
		case Kind::SByte:  return DrawClampedInt(label, value, field, -128, 127);
		case Kind::Short:  return DrawClampedInt(label, value, field, -32768, 32767);
		case Kind::UShort: return DrawClampedInt(label, value, field, 0, 65535);
		case Kind::Int:    return DrawClampedInt(label, value, field, INT32_MIN, INT32_MAX);
		case Kind::UInt:   return DrawLong(label, value, true);
		case Kind::Long:   return DrawLong(label, value, false);
		case Kind::ULong:  return DrawLong(label, value, true);
		case Kind::Float: {
			float v = value.is_number() ? value.get<float>() : 0.0f;
			ValueEditResult r = MyGUI::DragFloat(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = v; }
			return r;
		}
		case Kind::Double: return DrawDouble(label, value);
		case Kind::String: {
			std::string v = value.is_string() ? value.get<std::string>() : std::string{};
			TextEditSetting s{};
			s.multiLine = field.multiline;
			ValueEditResult r = MyGUI::InputText(label, v, s);
			if (r.valueChanged) { value = v; }
			return r;
		}
		case Kind::Enum: return DrawEnum(label, value, field);
		case Kind::Vector2: {
			Vector2 v = ReadVector2(value);
			ValueEditResult r = MyGUI::DragVector2(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = WriteVector2(v); }
			return r;
		}
		case Kind::Vector3: {
			Vector3 v = ReadVector3(value);
			ValueEditResult r = MyGUI::DragVector3(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = WriteVector3(v); }
			return r;
		}
		case Kind::Vector4: {
			Vector4 v = ReadVector4(value);
			ValueEditResult r = MyGUI::DragVector4(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = WriteVector4(v); }
			return r;
		}
		case Kind::Quaternion: {
			Quaternion v = ReadQuaternion(value);
			ValueEditResult r = MyGUI::DragQuaternion(label, v, true);
			if (r.valueChanged) { value = WriteQuaternion(v); }
			return r;
		}
		case Kind::Color3: {
			Color3 v = ReadColor3(value);
			ValueEditResult r = MyGUI::ColorEdit(label, v);
			if (r.valueChanged) { value = WriteColor3(v); }
			return r;
		}
		case Kind::Color4: {
			Color4 v = ReadColor4(value);
			ValueEditResult r = MyGUI::ColorEdit(label, v);
			if (r.valueChanged) { value = WriteColor4(v); }
			return r;
		}
		case Kind::Nullable: return DrawNullable(label, value, field, ctx);
		case Kind::Array:
		case Kind::List: return DrawCollection(label, value, field, ctx);
		case Kind::AssetRef: return DrawAssetRef(label, value, field, ctx);
		case Kind::EntityRef: return DrawEntityRef(label, value, ctx);
		case Kind::ScriptRef: return DrawScriptRef(label, value, field, ctx);
		case Kind::ComponentRef: return DrawComponentRef(label, value, field, ctx);
		case Kind::Object: return DrawObject(label, value, field, ctx);
		case Kind::ManagedReference: return DrawManagedReference(label, value, field, ctx);
		default:
			ImGui::TextDisabled("%s : 未対応の型", label);
			return {};
		}
	}
}
