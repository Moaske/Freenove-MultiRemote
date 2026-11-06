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
    public event Action? ConnectionChanged;   // Windows (dis)connected the remote
    bool _triedUncached;

    public bool Connected =>
        _device?.ConnectionStatus == BluetoothConnectionStatus.Connected && _meta != null && _art != null;

    public void SetStatus(string s)
    {
        if (s == Status) return;
        Log.Write("Status: " + s);
        Status = s;
        StatusChanged?.Invoke();
    }

    public void ResetCache()
    {
        _lastMeta = null;
        _lastArt = null;
    }

    // Gentle on the PC's Bluetooth adapter: this never makes Windows connect.
    // Windows reconnects the paired remote by itself (it is a HID device);
    // we only look at its service once Windows reports it as connected, and
    // the BluetoothLEDevice is opened once and kept.
    public async Task<bool> TryConnectAsync()
    {
        try
        {
            if (_device == null)
            {
                // A local lookup in Windows' list of paired devices: no radio traffic.
                var selector = BluetoothLEDevice.GetDeviceSelectorFromPairingState(true);
                var found = await DeviceInformation.FindAllAsync(selector);
                var info = found.FirstOrDefault(d => d.Name.StartsWith(DeviceName, StringComparison.OrdinalIgnoreCase));
                if (info == null)
                {
                    if (Status != "Remote not paired with this PC")
                        Log.Write($"Paired Bluetooth LE devices: {found.Count}" +
                                  string.Concat(found.Select(d => $"\n    '{d.Name}'  {d.Id}")));
                    SetStatus("Remote not paired with this PC");
                    return false;
                }
                _device = await BluetoothLEDevice.FromIdAsync(info.Id);
                if (_device == null) { Log.Write("FromIdAsync returned null"); SetStatus("Remote not reachable"); return false; }
                Log.Write($"Opened '{_device.Name}', connection: {_device.ConnectionStatus}");
                _device.ConnectionStatusChanged += OnConnectionChanged;
            }

            if (_device.ConnectionStatus != BluetoothConnectionStatus.Connected)
            {
                SetStatus("Remote asleep or out of range");
                return false;
            }

            // Windows keeps the services it found at pairing; ask for a fresh
            // look at most once per connection, only if the cached list lacks ours.
            var services = await _device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Cached);
            if ((services.Status != GattCommunicationStatus.Success || services.Services.Count == 0) && !_triedUncached)
            {
                _triedUncached = true;
                Log.Write($"Our service not in Windows' cache ({services.Status}), asking the remote once");
                services = await _device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
            }
            Log.Write($"Our service: {services.Status}, found {services.Services?.Count ?? 0}" +
                      (services.ProtocolError.HasValue ? $", protocol error {services.ProtocolError}" : ""));
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            {
                await LogAllServicesAsync();
                SetStatus("Track info service not found on the remote");
                return false;
            }
            _service?.Dispose();
            _service = services.Services[0];

            var meta = await _service.GetCharacteristicsForUuidAsync(MetaUuid, BluetoothCacheMode.Cached);
            var art = await _service.GetCharacteristicsForUuidAsync(ArtUuid, BluetoothCacheMode.Cached);
            Log.Write($"Characteristics: meta {meta.Status} ({meta.Characteristics?.Count ?? 0}), art {art.Status} ({art.Characteristics?.Count ?? 0})");
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
            Log.Error("connect", ex);
            SetStatus("Remote not reachable");
            DropService();
            return false;
        }
    }

    void OnConnectionChanged(BluetoothLEDevice d, object _)
    {
        Log.Write("Connection status: " + d.ConnectionStatus);
        if (d.ConnectionStatus == BluetoothConnectionStatus.Connected)
        {
            _triedUncached = false;
        }
        else
        {
            _meta = _art = null;
            SetStatus("Remote asleep or out of range");
        }
        ConnectionChanged?.Invoke();
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
                Log.Write($"Sending cover ({np.Art.Length} bytes)");
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
                Log.Write($"Sending track: {state} '{np.Title}' / '{np.Artist}' / '{np.Album}', cover: {np.Art != null}");
                if (!await WriteAsync(_meta!, Encoding.ASCII.GetBytes(meta))) return false;
                _lastMeta = meta;
            }
            SetStatus(np.State == PlayState.None ? "Connected - nothing playing" : "Connected - " + Short(np.Title));
            return true;
        }
        catch (Exception ex)
        {
            Log.Error("send", ex);
            DropService();
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
        if (r.Status != GattCommunicationStatus.Success)
            Log.Write($"Write of {data.Length} bytes failed: {r.Status}" +
                      (r.ProtocolError.HasValue ? $", protocol error {r.ProtocolError}" : ""));
        return r.Status == GattCommunicationStatus.Success;
    }

    // When our service is missing: what the remote does offer.
    async Task LogAllServicesAsync()
    {
        try
        {
            var all = await _device!.GetGattServicesAsync(BluetoothCacheMode.Cached);
            Log.Write($"All services: {all.Status}" + string.Concat(all.Services.Select(s => "\n    " + s.Uuid)));
            foreach (var s in all.Services) s.Dispose();
        }
        catch (Exception ex) { Log.Error("list services", ex); }
    }

    // Forget the service (looked up again next time), keep the device.
    void DropService()
    {
        _meta = _art = null;
        _service?.Dispose();
        _service = null;
    }

    void Drop()
    {
        DropService();
        if (_device != null)
        {
            _device.ConnectionStatusChanged -= OnConnectionChanged;
            _device.Dispose();
            _device = null;
        }
    }

    public void Dispose() => Drop();
}
