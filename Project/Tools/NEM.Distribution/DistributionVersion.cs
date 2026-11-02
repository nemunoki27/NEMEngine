namespace NEMEngine.Distribution;

public static class DistributionVersion {
    public static string Current => typeof(DistributionVersion).Assembly.GetName().Version!.ToString(3);
}
