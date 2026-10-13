# malitools 🚀

`malitools` is a lightweight, high-performance Vulkan interception and emulation layer (shim) designed for **ARM Mali GPUs** (specifically modern Valhall architectures such as Mali-G710 on Google Tensor G2 / Pixel 7 series).

Inspired by `adrenotools` on Qualcomm Adreno devices, `malitools` bridges the gap between stock Android Mali Vulkan drivers and translation layers like **DXVK**, **VKD3D-Proton**, and **Wine**, enabling x86/Windows compatibility layers (such as DroidDeck / Box64 / Termux environments) to run on Mali hardware.

---

## 🌟 Key Features

1. **Vulkan Loader Hooking (`dlopen` / `dlsym` interception):**
   - Intercepts core entrypoints: `vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateDevice`, and `vkEnumerateDeviceExtensionProperties`.
2. **Missing DXVK Extension Injection:**
   - Dynamically advertises critical desktop Vulkan extensions required by DXVK / Proton that stock Mali drivers lack:
     - `VK_EXT_transform_feedback`
     - `VK_EXT_robustness2`
     - `VK_EXT_custom_border_color`
     - `VK_EXT_extended_dynamic_state`
     - `VK_EXT_extended_dynamic_state2`
     - `VK_KHR_shader_float16_int8`
3. **`vkCreateDevice` Sanitization:**
   - Filters out emulated extensions before handing calls down to the underlying Mali proprietary driver (`libvulkan.so`), preventing `VK_ERROR_EXTENSION_NOT_PRESENT` fatal crashes.
4. **Transform Feedback Stubbing:**
   - Provides safe fallback stubs for geometry stream-out commands to prevent null-pointer dereferences in games.

---

## 🛠️ Building with Android NDK

### Prerequisites:
- CMake 3.22+
- Android NDK r25+ (r26b tested)
- Target ABI: `arm64-v8a`

```bash
mkdir build && cd build

NDK=$ANDROID_HOME/ndk/26.1.10909125

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build .
```

### Outputs:
- `libmalivkshim.so` — The Vulkan shim library to preload or dlopen.
- `test_shim` — A standalone CLI test binary to verify device detection and extension injection on an Android target device via ADB.

---

## 📱 Testing on Device via ADB

```bash
adb push build/libmalivkshim.so /data/local/tmp/
adb push build/test_shim /data/local/tmp/
adb shell chmod 755 /data/local/tmp/test_shim /data/local/tmp/libmalivkshim.so
adb shell /data/local/tmp/test_shim
```

---

## 📄 License
MIT License.
