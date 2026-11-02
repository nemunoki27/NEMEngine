using System.Text;

namespace NEM.ComponentBindingGen;

// 通常のComponent操作と同じ検証と更新通知でアニメーションを適用する
internal static class AnimationPropertyBindingEmitter {

    internal static void Emit(StringBuilder output, List<ComponentModel> components) {
        output.Append("\tvoid RegisterAnimationProperties(AnimationPropertyRegistry& registry) {\n");
        foreach (var component in components) {
            foreach (var property in component.Properties) {
                if (property.ReadOnly || property.Visibility != "Public" || property.NativeMember.StartsWith("runtime") ||
                    property.Kind is not ("Float" or "Int" or "Vector2" or "Vector3" or "Vector4" or "Color3" or "Color4" or "Quaternion") ||
                    property.NativeMember is "layer" or "order" or "priority") continue;
                string type = property.Kind switch { "Float" => "float", "Int" => "int32_t", _ => property.Kind };
                string name = component.RegistryName + "." + property.NativeMember;
                output.Append("\t\t{\n\t\t\tAnimationPropertyDescriptor descriptor;\n");
                output.Append($"\t\t\tdescriptor.componentName = \"{component.RegistryName}\";\n");
                output.Append($"\t\t\tdescriptor.propertyPath = \"{property.NativeMember}\";\n");
                output.Append($"\t\t\tdescriptor.displayName = \"{name}\";\n");
                output.Append($"\t\t\tdescriptor.valueType = AnimationValueType::{property.Kind};\n");
                output.Append($"\t\t\tdescriptor.hasComponent = [](ECSWorld& world, const Entity& entity) {{ return world.HasComponent<{component.NativeType}>(entity); }};\n");
                output.Append("\t\t\tdescriptor.getValue = [](ECSWorld& world, const Entity& entity, AnimationPropertyValue& value) {\n");
                output.Append($"\t\t\t\t{type} result{{}};\n");
                output.Append($"\t\t\t\tif ({component.ManagedType}_GetProp(world, entity, {property.ID}, &result, sizeof(result)) != ManagedStatus::Ok) return false;\n");
                output.Append("\t\t\t\tvalue = result; return true;\n\t\t\t};\n");
                output.Append("\t\t\tdescriptor.setValue = [](ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value) {\n");
                output.Append($"\t\t\t\tconst auto* typed = std::get_if<{type}>(&value);\n");
                output.Append($"\t\t\t\treturn typed && {component.ManagedType}_SetProp(world, entity, {property.ID}, typed, sizeof(*typed)) == ManagedStatus::Ok;\n");
                output.Append("\t\t\t};\n\t\t\tregistry.Register(descriptor);\n\t\t}\n");
            }
        }
        output.Append("\t}\n\n");
    }
}
