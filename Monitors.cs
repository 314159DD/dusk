using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace Dusk;

internal sealed record MonitorInfo(
    IntPtr Handle,
    string DeviceName,
    string Id,
    string Name,
    RECT Bounds,
    RECT Work,
    bool Primary);

internal static class MonitorEnum
{
    public static List<MonitorInfo> GetAll()
    {
        var result = new List<MonitorInfo>();
        Native.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (handle, _, _, _) =>
        {
            var info = new MONITORINFOEX { cbSize = Marshal.SizeOf<MONITORINFOEX>() };
            if (!Native.GetMonitorInfo(handle, ref info)) return true;

            var dd = new DISPLAY_DEVICE { cb = Marshal.SizeOf<DISPLAY_DEVICE>() };
            var hasDevice = Native.EnumDisplayDevices(info.szDevice, 0, ref dd, Native.EDD_GET_DEVICE_INTERFACE_NAME);
            var id = hasDevice && !string.IsNullOrEmpty(dd.DeviceID) ? dd.DeviceID : info.szDevice;

            var name = hasDevice ? ReadEdidName(dd.DeviceID) : null;
            name ??= "Display " + (result.Count + 1);

            result.Add(new MonitorInfo(
                handle,
                info.szDevice,
                id,
                name,
                info.rcMonitor,
                info.rcWork,
                (info.dwFlags & Native.MONITORINFOF_PRIMARY) != 0));
            return true;
        }, IntPtr.Zero);
        return result;
    }

    private static string? ReadEdidName(string interfaceName)
    {
        try
        {
            var parts = interfaceName.Split('#');
            if (parts.Length < 3) return null;
            var path = $@"SYSTEM\CurrentControlSet\Enum\DISPLAY\{parts[1]}\{parts[2]}\Device Parameters";
            using var key = Registry.LocalMachine.OpenSubKey(path);
            if (key?.GetValue("EDID") is not byte[] edid || edid.Length < 126) return null;

            for (var offset = 54; offset <= 108; offset += 18)
            {
                if (edid[offset] != 0 || edid[offset + 1] != 0 || edid[offset + 3] != 0xFC) continue;
                var text = Encoding.ASCII.GetString(edid, offset + 5, 13);
                var end = text.IndexOf('\n');
                if (end >= 0) text = text[..end];
                text = text.Trim();
                if (text.Length > 0) return text;
            }
        }
        catch
        {
        }
        return null;
    }
}
