namespace NEM.ComponentBindingGen;

// 生成したソースの先頭表記を作る
internal static class BindingOutputText {

    private const string GeneratorVersion = "4";
    private const int SupportedSchemaVersion = 2;
    // Nativeソースの先頭表記を作る
    internal static string NativeBanner() {
        return "//============================================================================\n" +
               "//\tAUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n" +
               $"//\tgenerator: NEM.ComponentBindingGen v{GeneratorVersion}\n" +
               $"//\tmetadata schemaVersion: {SupportedSchemaVersion}\n" +
               "//\t編集する場合は ComponentManifest.json または ManagedNativeAPI.json を更新して再生成する\n" +
               "//============================================================================\n";
    }

    // Managedソースの先頭表記を作る
    internal static string CSBanner() {
        return "//============================================================================\n" +
               "//\tAUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n" +
               $"//\tgenerator: NEM.ComponentBindingGen v{GeneratorVersion}\n" +
               $"//\tmetadata schemaVersion: {SupportedSchemaVersion}\n" +
               "//============================================================================\n" +
               "#nullable enable\n";
    }
}
