# Repository Guidelines

## Project Structure & Module Organization
- `Example/` holds numbered hardware demos (e.g., `11_key`, `28_dht11`, `31_touch_key`); each contains `template.c` plus a `BUILD.gn` static library that pulls in board support code from `vendor/pzkj/pz_hi3861/common/bsp`.
- Image assets under `Example/*/*.png` document wiring for the matching demo.
- `src/` is reserved for shared or custom extensions; keep new modules self-contained and mirror the existing numbering pattern.

## Build, Flash, and Development Commands
- Build with the OpenHarmony toolchain from the workspace root (`hi3861_hdu_iot_application`): run `hb set` once to select the Hi3861 product that includes this demo, then `hb build --target //vendor/pzkj/pz_hi3861/demo/49_Exam/Example/11_key:template` (swap the path for other modules).
- Flash the produced image with your board tool (commonly `hid_download_py -p COMx OHOS_Image.bin` or an equivalent GUI) and monitor logs over UART at 115200 8N1.
- For faster iteration, rebuild only the touched module/library; keep thread stack sizes lean (1 KB in current samples) to fit Hi3861 limits.

## Coding Style & Naming Conventions
- C files use 4-space indentation, brace-on-same-line for functions, snake_case for functions/variables, and uppercase for macros/constants.
- Follow the existing task structure: configure `osThreadAttr_t`, create threads with `osThreadNew`, and guard cross-task shared state with `volatile` or lightweight synchronization.
- Update `BUILD.gn` when adding sources or include paths; avoid leaking module-only headers outside their folder to keep dependencies clear.

## Testing Guidelines
- No automated tests are present; validate on hardware: flash the image, open a serial console, and exercise the wiring shown in the corresponding `Example/*` diagrams.
- Log behavior changes with `printf` (e.g., sensor readings, task transitions) and perform multiple power cycles to catch boot-time regressions.
- Record expected sensor/actuator outputs before and after changes to track correctness when tuning timings or priorities.

## Commit & Pull Request Guidelines
- Write concise, imperative summaries referencing the module touched (e.g., "Tighten debounce in Example/28_dht11"); expand in the body if multiple demos are affected.
- PRs should note hardware setup, commands run, and attach serial output or photos when wiring changes; link issues or tasks when available.
- Call out BSP or build-configuration updates that require others to re-run `hb set` or clean builds.

## Security & Configuration Tips
- Do not commit Wi-Fi credentials, device IDs, or cloud tokens; load them from device NVRAM or a private, ignored header.
- Confirm flash offsets, clock settings, and GPIO mappings match your Hi3861 board revision before releasing images.
