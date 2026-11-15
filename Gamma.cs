namespace Dusk;

internal static class ColorMath
{
    public static (double R, double G, double B) FromKelvin(int kelvin)
    {
        var (r, g, b) = Raw(kelvin);
        var (wr, wg, wb) = Raw(6500);
        r /= wr; g /= wg; b /= wb;
        var max = Math.Max(r, Math.Max(g, b));
        if (max > 1) { r /= max; g /= max; b /= max; }
        return (r, g, b);
    }

    private static (double R, double G, double B) Raw(int kelvin)
    {
        var t = kelvin / 100.0;
        double r, g, b;
        if (t <= 66)
        {
            r = 255;
            g = 99.4708025861 * Math.Log(t) - 161.1195681661;
            b = t <= 19 ? 0 : 138.5177312231 * Math.Log(t - 10) - 305.0447927307;
        }
        else
        {
            r = 329.698727446 * Math.Pow(t - 60, -0.1332047592);
            g = 288.1221695283 * Math.Pow(t - 60, -0.0755148492);
            b = 255;
        }
        return (Math.Clamp(r, 0, 255), Math.Clamp(g, 0, 255), Math.Clamp(b, 0, 255));
    }
}

internal static class Gamma
{
    public static bool Apply(string deviceName, double r, double g, double b)
    {
        var ramp = new ushort[768];
        for (var i = 0; i < 256; i++)
        {
            var v = i * 257.0;
            ramp[i] = (ushort)Math.Clamp(v * r, 0, 65535);
            ramp[256 + i] = (ushort)Math.Clamp(v * g, 0, 65535);
            ramp[512 + i] = (ushort)Math.Clamp(v * b, 0, 65535);
        }

        var hdc = Native.CreateDC(null, deviceName, null, IntPtr.Zero);
        if (hdc == IntPtr.Zero) return false;
        try
        {
            return Native.SetDeviceGammaRamp(hdc, ramp);
        }
        finally
        {
            Native.DeleteDC(hdc);
        }
    }
}
