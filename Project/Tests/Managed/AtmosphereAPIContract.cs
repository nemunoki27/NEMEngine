using NEMEngine;

namespace NEMTests.Scripts;

// Inspectorと同じ公開項目をゲームコードから編集するコンパイル検証
internal static class AtmosphereAPIContract {

    internal static void Edit(SkyAtmosphere sky, ExponentialHeightFog fog, GameObject sun) {

        sky.Enabled = true;
        _ = sky.Enabled;
        sky.RenderingLayerMask = 1u;
        _ = sky.RenderingLayerMask;
        sky.SunEntity = sun;
        _ = sky.SunEntity;
        sky.PlanetRadiusKm = 0.5f;
        _ = sky.PlanetRadiusKm;
        sky.AtmosphereHeightKm = 0.5f;
        _ = sky.AtmosphereHeightKm;
        sky.RayleighScattering = new Color3(0.2f, 0.3f, 0.4f);
        _ = sky.RayleighScattering;
        sky.RayleighScaleHeightKm = 0.5f;
        _ = sky.RayleighScaleHeightKm;
        sky.MieScattering = 0.5f;
        _ = sky.MieScattering;
        sky.MieAbsorption = 0.5f;
        _ = sky.MieAbsorption;
        sky.MieScaleHeightKm = 0.5f;
        _ = sky.MieScaleHeightKm;
        sky.MieAnisotropy = 0.5f;
        _ = sky.MieAnisotropy;
        sky.OzoneAbsorption = new Color3(0.2f, 0.3f, 0.4f);
        _ = sky.OzoneAbsorption;
        sky.OzoneCenterHeightKm = 0.5f;
        _ = sky.OzoneCenterHeightKm;
        sky.OzoneWidthKm = 0.5f;
        _ = sky.OzoneWidthKm;
        sky.GroundAlbedo = new Color3(0.2f, 0.3f, 0.4f);
        _ = sky.GroundAlbedo;
        sky.IblIntensity = 0.5f;
        _ = sky.IblIntensity;
        sky.AerialPerspectiveDistance = 0.5f;
        _ = sky.AerialPerspectiveDistance;
        sky.AerialPerspectiveStrength = 0.5f;
        _ = sky.AerialPerspectiveStrength;
        fog.Enabled = true;
        _ = fog.Enabled;
        fog.RenderingLayerMask = 1u;
        _ = fog.RenderingLayerMask;
        fog.Density = 0.5f;
        _ = fog.Density;
        fog.HeightFalloff = 0.5f;
        _ = fog.HeightFalloff;
        fog.Color = new Color3(0.2f, 0.3f, 0.4f);
        _ = fog.Color;
        fog.StartDistance = 0.5f;
        _ = fog.StartDistance;
        fog.MaxOpacity = 0.5f;
        _ = fog.MaxOpacity;
        fog.CutoffDistance = 0.5f;
        _ = fog.CutoffDistance;
        fog.DirectionalColor = new Color3(0.2f, 0.3f, 0.4f);
        _ = fog.DirectionalColor;
        fog.DirectionalExponent = 0.5f;
        _ = fog.DirectionalExponent;
        fog.DirectionalStartDistance = 0.5f;
        _ = fog.DirectionalStartDistance;
        fog.DirectionalIntensity = 0.5f;
        _ = fog.DirectionalIntensity;
        fog.SkyContribution = 0.5f;
        _ = fog.SkyContribution;
        fog.SecondDensity = 0.5f;
        _ = fog.SecondDensity;
        fog.SecondHeightFalloff = 0.5f;
        _ = fog.SecondHeightFalloff;
        fog.SecondHeightOffset = 0.5f;
        _ = fog.SecondHeightOffset;
    }
}
