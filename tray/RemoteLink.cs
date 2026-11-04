using System.Security.Cryptography;
using System.Text;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Storage.Streams;

namespace MultiRemoteTray;

// The Bluetooth LE link to the paired remote: finds it, and writes the
// track info to its service (see src/ble_link.h in the firmware).
sealed class RemoteLink : IDisposable
{
    static readonly Guid ServiceUuid = new("6b3f0001-8c2e-4a5e-9d1b-3c7e2f4a9b10");
    static readonly Guid MetaUuid    = new("6b3f0002-8c2e-4a5e-9d1b-3c7e2f4a9b10");
    static readonly Guid ArtUuid     = new("6b3f0003-8c2e-4a5e-9d1b-3c7e2f4a9b10");
    const string DeviceName = "MultiRemote";
    const int ArtChunk = 180;   // fits in one write on any MTU Windows picks

    BluetoothLEDevice? _device;
    GattDeviceService? _service;
    GattCharacteristic? _meta, _art;
    readonly SemaphoreSlim _sending = new(1, 1);

    string? _lastMeta;
    string? _lastArt;

    public string Status { get; private set; } = "Looking for the remote...";
    public event Action? StatusChanged;

    public bool Connected =>
        _device?.ConnectionStatus == BluetoothConnectionStatus.Connected && _meta != null && _art != null;

    public void SetStatus(string s)
    {
        if (s == Status) return;
        Status = s;
        StatusChanged?.Invoke();
    }

    public void ResetCache()
    {
        _lastMeta = null;
        _lastArt = null;
    }

    public async Task<bool> TryConnectAsync()
    {
        try
        {
            if (_device == null)
            {
                var selector = BluetoothLEDevice.GetDeviceSelectorFromPairingState(true);
                var found = await DeviceInformation.FindAllAsync(selector);
                var info = found.FirstOrDefault(d => d.Name.StartsWith(DeviceName, StringComparison.OrdinalIgnoreCase));
                if (info == null)
                {
                    SetStatus("Remote not paired with this PC");
                    return false;
                }
                _device = await BluetoothLEDevice.FromIdAsync(info.Id);
                if (_device == null) { SetStatus("Remote not reachable"); return false; }
                _device.ConnectionStatusChanged += OnConnectionChanged;
            }

            // Asking for the service also makes Windows connect.
            var services = await _device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            {
                SetStatus("Remote not in range or asleep");
                return false;
            }
            _service?.Dispose();
            _service = services.Services[0];

            var meta = await _service.GetCharacteristicsForUuidAsync(MetaUuid, BluetoothCacheMode.Uncached);
            var art = await _service.GetCharacteristicsForUuidAsync(ArtUuid, BluetoothCacheMode.Uncached);
            if (meta.Status != GattCommunicationStatus.Success || meta.Characteristics.Count == 0 ||
                art.Status != GattCommunicationStatus.Success || art.Characteristics.Count == 0)
            {
                SetStatus("Remote firmware too old for track info");
                return false;
            }
            _meta = meta.Characteristics[0];
            _art = art.Characteristics[0];
            ResetCache();
            SetStatus("Connected to the remote");
            return true;
        }
        catch (Exception ex)
        {
            SetStatus("Remote not reachable");
            System.Diagnostics.Debug.WriteLine(ex);
            Drop();
            return false;
        }
    }

    void OnConnectionChanged(BluetoothLEDevice d, object _)
    {
        if (d.ConnectionStatus == BluetoothConnectionStatus.Connected) return;
        _meta = _art = null;
        SetStatus("Remote not in range or asleep");
    }

    public async Task<bool> SendAsync(NowPlaying np)
    {
        await _sending.WaitAsync();
        try
        {
            if (!Connected) return false;

            // The cover first, so it is there by the time the text says "hasArt".
            string? artKey = np.Art == null ? null : Convert.ToHexString(SHA1.HashData(np.Art));
            if (np.Art != null && artKey != _lastArt)
            {
                for (int off = 0; off < np.Art.Length; off += ArtChunk)
                {
                    int n = Math.Min(ArtChunk, np.Art.Length - off);
                    var chunk = new byte[n + 2];
                    chunk[0] = (byte)(off & 0xFF);
                    chunk[1] = (byte)(off >> 8);
                    System.Buffer.BlockCopy(np.Art, off, chunk, 2, n);
                    if (!await WriteAsync(_art!, chunk)) return false;
                }
                _lastArt = artKey;
            }

            char state = np.State switch { PlayState.Playing => 'P', PlayState.Paused => 'p', _ => 'S' };
            string meta = string.Join('\u001F', state.ToString(), np.Title, np.Artist, np.Album, np.Art != null ? "1" : "0");
            if (meta != _lastMeta)
            {
                if (!await WriteAsync(_meta!, Encoding.ASCII.GetBytes(meta))) return false;
                _lastMeta = meta;
            }
            SetStatus(np.State == PlayState.None ? "Connected - nothing playing" : "Connected - " + Short(np.Title));
            return true;
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine(ex);
            Drop();
            SetStatus("Lost the remote, retrying...");
            return false;
        }
        finally
        {
            _sending.Release();
        }
    }

    static string Short(string s) => s.Length > 30 ? s[..30] + "..." : s;

    static async Task<bool> WriteAsync(GattCharacteristic c, byte[] data)
    {
        var w = new DataWriter();
        w.WriteBytes(data);
        var r = await c.WriteValueWithResultAsync(w.DetachBuffer(), GattWriteOption.WriteWithResponse);
        return r.Status == GattCommunicationStatus.Success;
    }

    void Drop()
    {
        _meta = _art = null;
        _service?.Dispose();
        _service = null;
        if (_device != null)
        {
            _device.ConnectionStatusChanged -= OnConnectionChanged;
            _device.Dispose();
            _device = null;
        }
    }

    public void Dispose() => Drop();
}
