# QcaBtMac

macOS IOKit USB firmware loader kext for Qualcomm Atheros **QCA9377 / QCA6174 (Rome)** Bluetooth controllers (configured out of the box for Lite-On `04ca:3015`, ROM `0x00000302`).

> [!WARNING]
> **AI-Generated ("Vibecoded") Kernel Extension:** This driver was completely vibecoded with an AI assistant rather than written by an experienced macOS kernel developer. Because it runs in kernel space, bugs may cause kernel panics, USB bus hangs, or system instability—use at your own risk and keep a working backup EFI/bootable recovery USB handy.

**Confirmed Working Environment:** macOS **15.7.5** (Build `24G624`, `x86_64`) on an Acer Aspire E5-575 with a Lite-On QCA9377 (`04ca:3015`, ROM `0x00000302`) combo card. Other macOS versions have not been tested.

---

## Overview & How It Works

Qualcomm Rome USB Bluetooth controllers boot in a minimal ROM bootloader state (`Firmware Version: v0 c0`) and cannot function as standard HCI Bluetooth adapters until their RAM patch (`rampatch`) and board Non-Volatile Memory configuration (`nvm`) are uploaded over USB.

`QcaBtMac.kext` handles this one-shot initialization during boot:

1. **Device Matching (`IOUSBHostDevice`):** Matches the USB Bluetooth controller via `IOKitPersonalities` (`IOProbeScore = 100000`) and merges `Built-In = true` and `UsbUserClientEntitlementRequired = com.apple.bluetooth.control` onto the provider via `AppleUSBHostMergeProperties`.
2. **Target & Status Query:** Sends vendor control IN requests `0x09` (`kQcaGetTargetVersion`) to read `romVersion` / `buildVersion` and `0x05` (`kQcaCheckStatus`) to check whether the patch (`0x80`) and system configuration (`0x40`) flags are already active.
3. **Firmware & NVM Upload:** Selects the embedded profile matching `romVersion` and streams `rampatch_usb_<rom>.bin` followed by `nvm_usb_<rom>.bin` by sending the first 4 bytes via vendor control OUT (`bRequest = 0x01`) and the remaining payload in up to 4096-byte chunks over Bulk OUT endpoint `0x02` (interface `0`).
4. **RAM Firmware Activation:** Sends a standard Bluetooth `HCI_Reset` (`0x0C03`) over EP0 Class Control OUT (`bRequest = 0x00`) to switch the controller from ROM bootloader mode into active RAM firmware.
5. **Handoff to macOS Bluetooth Stack:** Records status properties on the provider, calls `dev->close(this)` to release exclusive ownership, registers child `IOUSBHostInterface` nodes (`registerService()`), and exits `start()` (`return false`) so `BlueToolFixup.kext` and `bluetoothd` can attach cleanly.

---

## Hardware Specificity (Same Chipset, Different OEM Board/Controller)

Having the same QCA9377 or QCA6174 silicon **does not guarantee out-of-the-box compatibility** across different laptop OEMs. Controller and board compatibility depends on three layers:

1. **OEM USB Vendor & Product ID (`QcaBtMac.kext/Contents/Info.plist`):**
   - USB Bluetooth controllers expose **different USB `idVendor:idProduct` pairs depending on the module OEM** (Lite-On, Foxconn/Hon Hai, AzureWave, Dell, Lenovo, etc.).
   - `Info.plist` currently matches **only** Lite-On `04ca:3015` (decimal `idVendor = 1226`, `idProduct = 12309`).
   - **What to change:** Check your controller's USB ID in macOS (`ioreg -p IOUSB -l`) or Linux (`lsusb`), convert the hex VID/PID to **decimal**, and update both `QCA9377_LiteOn_04ca_3015_Bootloader` and `QCA9377_LiteOn_04ca_3015_Entitlement` inside `QcaBtMac.kext/Contents/Info.plist`.
2. **Chip ROM Revision (`firmware/rampatch_usb_<rom>.bin`):**
   - At runtime, `QcaBtMac` selects firmware strictly by matching `ver.romVersion` against compiled profiles (`QcaBtPayload::selectProfile`).
   - Only **`0x00000302`** (`rampatch_usb_00000302.bin` + `nvm_usb_00000302.bin`) is bundled in `firmware/`. If your module reports a different ROM version, initialization stops with `no_profile_rom=0x...` unless you supply the matching `rampatch` and `nvm` binaries and rebuild.
3. **Board-Specific NVM Parameters (`firmware/nvm_usb_<rom>.bin`):**
   - The `.nvm` binary stores board-level RF calibration, reference clock (XTAL), Wi-Fi/Bluetooth coexistence signaling, and default `BD_ADDR` settings tailored to the physical module layout. OEM boards with different RF front-ends or coexistence wiring may require their vendor-specific `.nvm` payload.

---

## Requirements

- **Tested OS:** macOS 15.7.5 (Build `24G624`, `x86_64`)
- **Build Dependencies:**
  - Xcode Command Line Tools (`clang++`, `xcrun`)
  - [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) (configure the SDK and symbol-checker paths in `Makefile` to match your local setup)
- **Runtime Dependencies:**
  - [Lilu.kext](https://github.com/acidanthera/Lilu)
  - [BlueToolFixup.kext](https://github.com/acidanthera/BrcmPatchRAM)

---

## Building from Source

1. *(Optional)* If adding another ROM profile, place `rampatch_usb_<8-hex-rom>.bin` and `nvm_usb_<8-hex-rom>.bin` inside `firmware/` and regenerate `src/QcaBtPayload.hpp`:
   ```sh
   make prepare-firmware
   ```
2. Compile and verify the kext bundle:
   ```sh
   make kext
   ```
   The compiled kext is written to `build/out/QcaBtMac.kext`.

---

## Installation (OpenCore)

1. **Copy Kexts:**
   Place `Lilu.kext`, `BlueToolFixup.kext`, and `QcaBtMac.kext` into `EFI/OC/Kexts/`.
2. **Register in `EFI/OC/config.plist` (`Kernel -> Add`):**
   Ensure `Lilu.kext` loads first, followed by `BlueToolFixup.kext` and `QcaBtMac.kext`.
3. **USB Port Mapping:**
   Ensure the internal USB port hosting the Bluetooth controller (e.g., `PortNum = 5` on Acer Aspire E5-575) is mapped as **Internal (`255` / `0xFF`)** in your USBMap/UTBMap kext.
4. **Boot Arguments & NVRAM (`NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82`):**
   For SMBIOS models that do not natively use USB Bluetooth controllers (such as `MacBookPro13,1`), add:
   - **`boot-args`:** `-btlfxboardid -btlfxallowanyaddr`
   - **`bluetoothExternalDongleFailed` (Data):** `00`
   - **`bluetoothInternalControllerInfo` (Data):** `0000000000000000000000000000` (14 zero bytes)
5. **Cold Boot:**
   Shut down the machine completely (do **not** use a warm restart) and power it back on.

---

## Verification & Diagnostics

- **Check IOReg Initialization Stage:**
  ```sh
  ioreg -l | grep -E "QcaBtMac"
  ```
  A successful initialization sets `QcaBtMacStage` to:
  ```text
  ok_rom=0x302_pre=0x20_post=0xe0_hci=0x0_bld=0x111
  ```
  (`post=0xe0` confirms both the `0x80` RAM patch and `0x40` sysconfig bits are active.)
- **Check macOS Bluetooth Stack:**
  ```sh
  system_profiler SPBluetoothDataType
  ```
  Confirm `State: On`, a valid MAC `Address` (not `NULL`), and `Firmware Version: v602 c0` (or equivalent non-zero RAM firmware build).

---

## Known Bugs, Limitations & Quirks

### 1. Warm Reboot Drops the USB Controller (Cold Boot Required)
- **Symptom:** After a warm restart from macOS (or rebooting from another OS), Bluetooth disappears (`Address: NULL`, `State: Off`, `Firmware Version: v0 c0`), and the controller is absent from `ioreg -p IOUSB`.
- **Cause:** On laptops such as the Acer Aspire E5-575, the QCA9377 USB Bluetooth endpoint does not cleanly reset across warm reboots and remains hung on the XHCI bus until power is cut.
- **Workaround:** Always perform a full **Shut Down** rather than **Restart**. Because the Embedded Controller (EC) and USB rails can occasionally retain standby power even after shutdown, cold-boot enumeration can still be intermittent; if Bluetooth fails to appear after powering on, shut down, unplug AC power, hold the power button for 10–15 seconds to discharge residual rail power, and power back on.

### 2. macOS 15.7.5 `bluetoothd` Faults / Disconnect Flicker with Classic Audio / TWS Devices
- **Symptom:** Bluetooth scanning works, but connecting to certain Bluetooth Classic audio devices (such as EDIFIER X2s TWS earbuds) causes Bluetooth to toggle off briefly and immediately turn back on without connecting.
- **Cause:** During Classic link encryption (`HCIEvent EncryptionChange`), the QCA9377 controller reports `key size = 255` (`0xFF`) with `Encryption E0 enabled`. On macOS 15.7.5, `bluetoothd` can log an `EXC_GUARD` fault (`Transport layer initialization failed`) and respawn mid-handshake when stale pairing records exist in macOS Sequoia's Bluetooth database (`/Library/Bluetooth/`) or when the controller has not been freshly initialized from a cold boot. *(Note: Legacy `/Library/Preferences/com.apple.Bluetooth.plist` paths no longer exist on macOS Sequoia.)*
- **Workaround:**
  1. Remove the cached macOS Sequoia Bluetooth pairing database files:
     ```sh
     sudo rm -f /Library/Bluetooth/Library/Preferences/com.apple.MobileBluetooth.devices.plist \
                /Library/Bluetooth/com.apple.MobileBluetooth.ledevices.*
     ```
  2. Perform a full **Shut Down** (cold boot) and power the laptop back on so the QCA9377 controller and `bluetoothd` initialize cleanly.
  3. Put the earbuds/headset into fresh **pairing mode** (not passive reconnect) and pair immediately in **System Settings > Bluetooth**.

### 3. Sleep / Wake Instability
- Depending on your laptop's ACPI `_PRW` / USB power-gating behavior during S3 sleep, the QCA9377 module may lose power without triggering a clean USB re-enumeration on wake, requiring a cold boot to restore Bluetooth.

### 4. No Continuity / AirDrop
- Because macOS identifies the controller as `Chipset: THIRD_PARTY_DONGLE` (`Vendor ID: 0x004C`) without Apple proprietary AWDL/Wi-Fi coexistence hooks, **AirDrop, Handoff, Universal Clipboard, and Apple Watch Unlock** are not supported.
