# ListNetworks

A CLI tool for AmigaOS 3.x to list wireless networks using the SANA2 protocol.

Based on the ShowSanaDev source code by Philippe CARPENTIER.

## Requirements

- AmigaOS 3.x
- A SANA2-compatible wireless network device driver
- SAS/C 6.59 compiler (for building from source)

## Building

From the AmigaOS Shell:

```
smake
```

## Usage

```
ListNetworks [DEVICE=<devicename>] [UNIT=<unitnumber>] [VERBOSE]
```

### Arguments

- **DEVICE** — Name of the SANA2 device to use (e.g. `prism2.device`).
  If not specified, ListNetworks will scan the system for available
  SANA2 devices and use the first one found.

- **UNIT** — Unit number to open (default: 0).

- **VERBOSE** — Show extended device information including MAC address,
  hardware type, signal quality, connected network info, and supported
  encryption types.

### Examples

Scan using auto-detected device:
```
ListNetworks
```

Scan using a specific device:
```
ListNetworks DEVICE=prism2.device
```

Scan with verbose device info:
```
ListNetworks DEVICE=prism2.device VERBOSE
```

Scan on a specific unit:
```
ListNetworks DEVICE=atheros5000.device UNIT=1
```

### Output

The tool displays a table with the following columns:

| Column | Description                                  |
|--------|----------------------------------------------|
| Signal | Signal-to-Noise Ratio in dB                  |
| BSSID  | MAC address of the access point              |
| Chan   | WiFi channel number                          |
| Band   | Frequency band (2.4 GHz or 5 GHz)           |
| SSID   | Network name                                 |

## License

Public domain.
