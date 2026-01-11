# nvidia_mmio_hwmon-dkms

Out-of-tree Linux **hwmon** driver for NVIDIA GPUs, exposing additional temperature
and telemetry data not available via standard kernel drivers.

This module combines **direct MMIO access** for on-die thermal sensors with
**NVML-bridged userspace telemetry** for values that are not safely accessible
from kernel space.

The project is packaged for **DKMS**, allowing it to automatically rebuild
against multiple kernels.

---

## Features

### Exposed hwmon sensors

| hwmon node | Source | Description |
|-----------|--------|-------------|
| `temp1_input` | NVML | GPU core temperature |
| `temp2_input` | MMIO | GPU junction / hotspot temperature |
| `temp3_input` | MMIO | VRAM temperature |
| `power1_input` | NVML | Total board power (mW) |
| `fan1_input` | NVML | Fan speed (percent or RPM, device-dependent) |

All values follow standard **hwmon units** and work with `lm_sensors`,
monitoring daemons, and dashboards.

---

## Design overview

- **MMIO (kernel)**  
  Used only for stable, read-only thermal registers:
  - Junction / hotspot temperature
  - VRAM temperature

- **NVML (userspace helper)**  
  Used for values that NVIDIA does not expose safely via MMIO:
  - GPU core temperature
  - Board power
  - Fan speed

The userspace helper writes an atomic snapshot into the kernel driver via sysfs.
This avoids firmware reverse engineering, PMU access, or reliance on proprietary
kernel drivers for sensor visibility.

---

## Non-goals

This project intentionally does **not** provide:

- Clock frequencies (not part of hwmon)
- Voltage rails
- Fan control
- Overclocking or power limits
- Replacement for NVML or the proprietary NVIDIA driver

---

## Requirements

- Linux kernel with hwmon support
- NVIDIA GPU supported by documented MMIO thermal tables
- NVIDIA NVML library (`nvidia-utils`)
- DKMS
- Root privileges to load the module and update sysfs

---

## Installation (Arch Linux / DKMS)

Build and install the package:

```sh
makepkg -si
```
