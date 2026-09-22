using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class BindingOutputText {

    private const string GeneratorVersion = "4";
    private const int SupportedSchemaVersion = 2;
    internal static string NativeBanner() {
        return "//============================================================================\n" +
               "//\tAUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n" +
               $"//\tgenerator: NEM.ComponentBindingGen v{GeneratorVersion}\n" +
               $"//\tmetadata schemaVersion: {SupportedSchemaVersion}\n" +
               "//\t編集する場合は ComponentManifest.json または ManagedNativeAPI.json を更新して再生成する\n" +
               "//============================================================================\n";
    }

    internal static string CsBanner() {
        return "//============================================================================\n" +
               "//\tAUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n" +
               $"//\tgenerator: NEM.ComponentBindingGen v{GeneratorVersion}\n" +
               $"//\tmetadata schemaVersion: {SupportedSchemaVersion}\n" +
               "//============================================================================\n" +
               "#nullable enable\n";
    }
}
