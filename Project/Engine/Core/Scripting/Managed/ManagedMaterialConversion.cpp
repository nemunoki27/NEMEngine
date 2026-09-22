#include "ManagedMaterialConversion.h"
#include "ManagedScriptUtility.h"

#include <array>
#include <cstring>

namespace Engine::ManagedMaterialConversion {

		bool DecodeMaterialParameterValue(
			const Engine::ManagedMaterialParameterValue& source,
			Engine::MaterialParameterValue& outValue) {

			std::array<float, 4> floatValues{};
			std::memcpy(floatValues.data(), &source.data0, sizeof(floatValues));
			switch (static_cast<Engine::ManagedMaterialParameterValueType>(source.type)) {
			case Engine::ManagedMaterialParameterValueType::Float:
				outValue.value = floatValues[0];
				return true;
			case Engine::ManagedMaterialParameterValueType::Vector2:
				outValue.value = Engine::Vector2(floatValues[0], floatValues[1]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Vector3:
				outValue.value = Engine::Vector3(
					floatValues[0], floatValues[1], floatValues[2]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Vector4:
				outValue.value = Engine::Vector4(
					floatValues[0], floatValues[1], floatValues[2], floatValues[3]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Color:
				outValue.value = Engine::Color4(
					floatValues[0], floatValues[1], floatValues[2], floatValues[3]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Texture: {
				Engine::ManagedAssetGUID asset{};
				std::memcpy(&asset, &source.data0, sizeof(asset));
				outValue.value = Engine::ToAssetID(asset);
				return true;
			}
			case Engine::ManagedMaterialParameterValueType::Int: {
				int32_t value = 0;
				std::memcpy(&value, &source.data0, sizeof(value));
				outValue.value = value;
				return true;
			}
			case Engine::ManagedMaterialParameterValueType::UInt: {
				uint32_t value = 0;
				std::memcpy(&value, &source.data0, sizeof(value));
				outValue.value = value;
				return true;
			}
			case Engine::ManagedMaterialParameterValueType::Bool: {
				int32_t value = 0;
				std::memcpy(&value, &source.data0, sizeof(value));
				outValue.value = value != 0;
				return true;
			}
			}
			return false;
		}

		bool EncodeMaterialParameterValue(
			const Engine::MaterialParameterValue& source,
			Engine::ManagedMaterialParameterValue& outValue) {

			outValue = {};
			auto writeFloats = [&](std::array<float, 4> values) {
				std::memcpy(&outValue.data0, values.data(), sizeof(values));
			};

			if (const float* floatValue = std::get_if<float>(&source.value)) {
				writeFloats({ *floatValue, 0.0f, 0.0f, 0.0f });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Float);
			} else if (const Engine::Vector2* vector2 =
				std::get_if<Engine::Vector2>(&source.value)) {

				writeFloats({ vector2->x, vector2->y, 0.0f, 0.0f });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Vector2);
			} else if (const Engine::Vector3* vector3 =
				std::get_if<Engine::Vector3>(&source.value)) {

				writeFloats({ vector3->x, vector3->y, vector3->z, 0.0f });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Vector3);
			} else if (const Engine::Vector4* vector4 =
				std::get_if<Engine::Vector4>(&source.value)) {

				writeFloats({ vector4->x, vector4->y, vector4->z, vector4->w });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Vector4);
			} else if (const Engine::Color4* color =
				std::get_if<Engine::Color4>(&source.value)) {

				writeFloats({ color->r, color->g, color->b, color->a });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Color);
			} else if (const Engine::AssetID* assetID =
				std::get_if<Engine::AssetID>(&source.value)) {

				const Engine::ManagedAssetGUID asset =
					Engine::ToManagedAssetGUID(*assetID);
				std::memcpy(&outValue.data0, &asset, sizeof(asset));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Texture);
			} else if (const int32_t* intValue =
				std::get_if<int32_t>(&source.value)) {

				std::memcpy(&outValue.data0, intValue, sizeof(*intValue));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Int);
			} else if (const uint32_t* uintValue =
				std::get_if<uint32_t>(&source.value)) {

				std::memcpy(&outValue.data0, uintValue, sizeof(*uintValue));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::UInt);
			} else if (const bool* boolValue = std::get_if<bool>(&source.value)) {
				const int32_t native = *boolValue ? 1 : 0;
				std::memcpy(&outValue.data0, &native, sizeof(native));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Bool);
			} else {
				return false;
			}
			return true;
		}
}
