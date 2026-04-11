# AGENTS.md — Fritz!Home Agent Guidance

## Project Overview

**Fritz!Home** is a **Qt desktop application** (C++17, CMake-based) for monitoring and controlling AVM Fritz!Box Smart Home devices over the local network. Single-window UI with split view: grouped device tree (left) + context-sensitive control/chart panel (right).

Key quirks:
- **Multi-target cross-platform build** — Qt 5 (Leap 15.6) or Qt 6 (Leap 16.0+, Tumbleweed, Ubuntu 24.04) with optional KDE Frameworks (KF5/KF6)
- **Compile-time feature flags** — `USE_KF` (0/5/6/qt6/auto) controls Qt version + KDE+password backend at cmake config time
- **Docker-based build workflow** — recommended approach; all official binaries built this way
- **Cross-architecture support** — includes aarch64 native cross-compilation for Tumbleweed
- **Translation system split** — KF builds use `.po`/`.mo` via KI18n; no-KF builds embed `.qm` as Qt resource

## Build System

### Quick Build Commands

**Always use Docker (recommended — no local Qt/KF required):**

All builds should use the Docker build environment to ensure consistency across Qt5, Qt6, and KDE Frameworks versions.

```bash
# Build for a single distro (Tumbleweed x86_64, Release)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# Build for all five supported distros
./docker/build.sh --distro all --build-type Release

# Debug build with qDebug output enabled
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Qt5-only build (Leap 15.6)
./docker/build.sh --distro opensuse-leap-15.6-x86_64 --build-type Release

# Qt6 + KF6 build (Leap 16.0)
./docker/build.sh --distro opensuse-leap-16.0-x86_64 --build-type Release

# Cross-compiled aarch64 binary
./docker/build.sh --distro opensuse-tumbleweed-aarch64 --build-type Release

# Ubuntu 24.04 Qt6-only build
./docker/build.sh --distro ubuntu-24.04-x86_64 --build-type Release
```

**Why Docker for all builds:**
- **Eliminates "works on my machine" problems** — all builds run in identical, isolated containers
- **No host system pollution** — Docker environment is disposable and reproducible
- **Automatic dependency resolution** — Qt, KDE Frameworks, build tools are pre-configured per distro
- **Cross-compilation guaranteed** — aarch64 builds work correctly on x86_64 hosts via Docker
- **Version consistency** — exact compiler, CMake, and dependency versions match all developers and CI/CD

### Build Output Locations

All builds use Docker, output is placed in:

- **Docker builds** → `out/<family>/<distro>/<arch>/` (binary + package)
  - Binaries: `out/opensuse/tumbleweed/x86_64/fritzhome` (executable)
  - Packages: `out/opensuse/tumbleweed/x86_64/fritzhome-1.0.0-1.x86_64.rpm`
  - Ubuntu: `out/ubuntu/24.04/amd64/fritzhome_1.0.0-1_amd64.deb`

Package filenames follow distribution conventions:
- **RPM** (openSUSE): `name-version-release.arch.rpm`
- **DEB** (Ubuntu): `name_version-release_arch.deb`

The directory path encodes the distro family, release, and architecture.

### Key `USE_KF` Values

| Value | Qt | KDE Frameworks | Password Backend | Use Case |
|-------|-----|---|---| --- |
| `0` | Qt5 | None | libsecret or QSettings | Leap 15.6 (standard OSS repo) |
| `5` | Qt5 | KF5 | KWallet | Leap 15.6 (KDE:Frameworks5 OBS repo) |
| `6` | Qt6 | KF6 | KWallet | Leap 16.0, Tumbleweed (standard repos) |
| `qt6` | Qt6 | None | libsecret | Ubuntu 24.04 LTS |
| (empty) | auto | auto | auto | Default: tries KF6 → KF5 → Qt5-only |

## Architecture & Code Organization

### Source Files Map

Core entry point and network layer:
- `src/main.cpp` — CLI argument parsing, QApplication/KApplication init, translator setup
- `src/fritzapi.h / .cpp` — **REST API client** (login pbkdf2/md5, async polling, response caching + request deduplication, JSON parsing)
- `src/fritzdevice.h` — data structs (FritzDevice, all *Stats substruct, FunctionMask)

UI layer:
- `src/mainwindow.h / .cpp` — top-level window; device tree, panel switching, signal routing
- `src/loginwindow.h / .cpp` — connection dialog (host, username, password, TLS cert warning checkbox)
- `src/devicemodel.h / .cpp` — QAbstractItemModel (2-level tree: groups + devices)
- `src/chartwidget.h / .cpp` — **Qt Charts time-series + energy history** (complex state management)

Device control panels (one per device type, all inherit DeviceWidget):
- `src/switchwidget.h / .cpp` — smart plug control
- `src/thermostatwidget.h / .cpp` — radiator controller (HKR)
- `src/energywidget.h / .cpp` — energy meter (read-only)
- `src/dimmerwidget.h / .cpp` — dimmable bulb/dimmer
- `src/blindwidget.h / .cpp` — roller blind/jalousie
- `src/colorwidget.h / .cpp` — RGBW colour bulb
- `src/humiditysensorwidget.h / .cpp` — humidity sensor (read-only)
- `src/alarmwidget.h / .cpp` — door/window alarm sensor

Utilities:
- `src/secretstore.h / .cpp` — cross-backend password storage (KWallet / libsecret / QSettings fallback)
- `src/i18n_shim.h` — i18n() macro bridging KI18n (HAVE_KF=1) and QCoreApplication::translate (HAVE_KF=0)

Translations:
- `po/de/fritzhome.po` — German catalogue (KF builds; installed as `.mo`)
- `translations/fritzhome_de.ts` — German Qt translation (no-KF builds; compiled to `.qm` resource)

### Compile-Time Macros

- **`HAVE_KF`** — set to 1 (KDE Frameworks present) or 0 (pure Qt); controls `i18n_shim.h` macro, password backend selection, window base class (KXmlGuiWindow vs QMainWindow)
- **`HAVE_LIBSECRET`** — set to 1 if libsecret available (used when `HAVE_KF=0` for Secret Service API fallback); otherwise QSettings plaintext fallback with build warning

### Key Architectural Decisions

**DeviceModel structure:**
- 2-level tree model: invisible root → group headers → device leaves
- Group headers have `internalId=0xFFFFFFFF`; device leaves have `internalId=group_index`
- Rebuilt wholesale on each poll via `beginResetModel()`/`endResetModel()` (no incremental updates)
- Group bucket priority (first matching wins): Groups > ColorBulbs > Dimmers > SmartPlugs > Switches > Thermostats > Blinds > Alarms > HumiditySensors > Sensors

**FritzApi async caching:**
- Response cache keyed by `(endpoint, parameters)` with TTL (2s for device list, 5s for device stats)
- Request deduplication: in-flight requests queue callbacks, shared reply invokes all
- `clearAllCaches()` called after every successful command to ensure next poll gets fresh state
- Session expiry (HTTP 401) → silent auto-relogin (up to 3 retries); full reconnect flow only on persistent failure

**ChartWidget state complexity:**
- Energy History shows 3 resolutions: 15-min (900s), daily (86400s), monthly (2678400s)
- Single-device bar chart vs. stacked group bar chart
- Two-row axis overlay (day/month labels) for daily and monthly views (custom QGraphicsTextItem positioning)
- Pie chart for group member energy distribution (only groups)
- Skip-rebuild guard: caches `m_activeEnergyGrid` + `m_lastEnergyStats` to skip expensive teardown if new data matches last build
- All-zero/all-NaN fallback to "No energy data available" placeholder

**UI state persistence:**
- All window geometry, splitter positions, column widths, chart resolution, time-window combo index stored in QSettings
- Groups' expansion state restored by querying `Qt::UserRole` (raw label) before and after model resets

## API & Protocol Details

### Fritz!Box REST Endpoints

- `GET /login_sid.lua?version=2` — fetch challenge (pbkdf2 or md5)
- `GET /login_sid.lua?version=2&username=<u>&response=<r>` — submit response, get SID
- `GET /api/v0/smarthome/overview` — device list (entire JSON tree)
- `GET /api/v0/smarthome/overview/units/{unitUID}` — single device stats + energy history (with `?statistics_interval=<grid>`)
- `PUT /api/v0/smarthome/overview/units/{unitUID}` — send command (JSON body: `{"interfaces": {…}}`)

Response caching:
- Device list: 2s TTL
- Device stats: 5s TTL
- **No caching of command responses** — `clearAllCaches()` called on success, next poll forced within 500ms

### Password Storage Priority

1. **KWallet** (HAVE_KF=1) — encrypted, integrates with KDE unlock
2. **libsecret / Secret Service** (HAVE_KF=0, libsecret available) — freedesktop standard, works with GNOME Keyring + KDE Secret Service
3. **QSettings** (fallback only) — plaintext, logged as build warning, not recommended for production

## Code Quality Standards

The project follows these conventions to maintain maintainability across Qt5/Qt6 and KF builds:

### C++ Standard and Style

- **C++17 standard** — required; use modern features (auto, structured bindings, lambdas)
- **Naming conventions:**
  - **Member variables:** `m_camelCase` prefix (e.g., `m_deviceTree`, `m_statusLabel`, `m_reloginAttempts`)
  - **Constants/enums:** `kCamelCase` prefix (e.g., `kGroupSentinel`, `kWindowMs[]`, `kMaxReloginAttempts`)
  - **Local variables:** `camelCase` (no prefix)
  - **Private/protected methods:** `onCamelCase` for slots, `computeCamelCase` for helpers
- **Header organization:**
  - Qt includes first, then system/3rd-party, then local headers
  - Private classes/helpers in implementation files (`.cpp`), not in headers
  - Public API clearly separated from internals
  - Member variable initialization in constructor (not in-class initializers for Qt classes)
- **Scope markers in code:**
  - Use ASCII comment separators for major sections: `// ── Description ────────────────────`
  - Mark important blocks with `// Constructor`, `// Slots`, `// Utility methods` comments
  - Group related functions together; avoid scattering across the file
- **Qt-specific patterns:**
  - Prefer `QString` over `std::string` for all user-visible strings
  - Use `QPointer<T>` for pointers to QObjects (auto-zeroing on delete)
  - Prefer range-based for loops: `for (auto &item : container)` instead of iterator loops
  - Connect signals in constructors or dedicated `wireSignals()` methods, never in loop bodies
  - Use `Q_OBJECT` macro for all QObject subclasses
  - Avoid raw `new`/`delete`; rely on parent-child ownership or smart pointers

### Object-Oriented Design (Critical Priority)

Avoid lengthy functions with many `if` clauses — **extract conditional logic into separate objects and methods**:

**Anti-pattern: god function with deep if/else nesting**
```cpp
void MainWindow::handleDeviceUpdate(const FritzDevice &dev) {
    if (dev.isSwitch()) {
        if (dev.hasEnergyMeter()) {
            // ... 10 lines of switch + energy logic
        } else {
            // ... 5 lines of switch-only logic
        }
    } else if (dev.isThermostat()) {
        // ... 15 lines of thermostat logic
        if (dev.batteryLow()) {
            // ... nested battery logic
        }
    } else if (dev.isBlind()) {
        // ... 8 lines of blind logic
    }
    // ... 40+ more lines
}
```

**Solution: polymorphic dispatch with inheritance/strategy pattern**
```cpp
// In updateDevicePanel(device):
DeviceWidget *panel = createPanelForDevice(device);
if (panel) {
    panel->updateDevice(device);
    m_controlStack->setCurrentWidget(panel);
}

// Each DeviceWidget subclass (SwitchWidget, ThermostatWidget, BlindWidget, etc.)
// implements updateDevice() with only its own logic — zero knowledge of other types
// This is already the pattern in the codebase; extend it everywhere.
```

**Guidelines for OOP refactoring:**

- **Polymorphism over conditionals**: if a function branches on object type, extract each branch into a virtual method or separate class
- **Strategy pattern for variants**: use value objects or small strategy classes instead of large `if/else` chains
  - Example: instead of `if (cacheType == A) { ... } else if (cacheType == B) { ... }`, use `CacheStrategy::fetchData()`
- **Factory methods to hide creation logic**: move `if` branches that decide "what to create" into factories
  - Example: `createDevicePanel(device)` hides all the `if (device.isSwitch())` logic
- **Extract method**: if a function body exceeds 40 lines or has >3 levels of nesting, split into smaller private methods with clear names
  - Example: `setupLayoutsAndSignals()` instead of embedding 60 lines in the constructor
- **Single Responsibility**: each class/method should have one reason to change
  - `ChartWidget` manages chart tabs; `EnergyHistoryBuilder` builds energy history charts (separate class)
  - `FritzApi` handles network/caching; business logic for device updates lives in UI layer

**Testability benefit**: code split into small OOP units is naturally mockable. A separate `EnergyHistoryBuilder` class can be tested with fake data; a 100-line function with 5 nested `if` blocks cannot.

### Documentation

- **Public class headers:** include a brief Doxygen-style docstring explaining the class purpose, its single responsibility, and async/caching semantics (see `FritzApi` for example)
- **Complex methods:** add inline comments explaining the "why" not the "what"
  - Authentication flow, cache TTL semantics, signal routing, model rebuild triggers
  - Example: `// Cache hit with valid TTL — invoke callback immediately without network request`
- **Parameter documentation:** use Qt conventions (`param`, `return` tags in comments if needed)
- **Deprecated/fallback code:** mark clearly with comments explaining the reason and duration
- **Document why OOP patterns were chosen:** when extracting a method into a separate class, explain the reason
  - Example: `// EnergyHistoryBuilder: separate builder for complex chart construction. Allows testing with mock data.`
  - This helps future maintainers understand the design intent

### Version Compatibility

- **Qt5 vs. Qt6:** always guard version-specific code with `#if QT_VERSION_MAJOR == 6` / `#else`
  - Pay special attention to: signal/slot connection syntax, container APIs (e.g., `container.removeAll()` behavior), QDateTime methods
  - Test both Qt5 and Qt6 builds if modifying cross-version code
- **HAVE_KF conditionals:** guard all KDE-specific includes and API calls with `#if HAVE_KF ... #endif`
  - Ensure fallback codepaths compile and work on Qt-only builds
  - Use `i18n_shim.h` for all internationalization calls (never call `KLocalizedString` or `KI18n` directly)

### Async & Signal Integrity

- **No blocking I/O:** all network operations in `FritzApi` are async with callbacks or signals
  - Never call `waitForReadyRead()` or similar Qt network blocking methods
- **Signal/slot chains:** trace the full signal routing path in MainWindow and DeviceWidgets
  - If modifying a signal/slot connection, verify the callback chain reaches the UI or storage
- **Callback ordering:** in `FritzApi`, all callbacks are invoked **after** the cache is updated
  - Do not assume the network request is still in-flight when the callback fires (it may be cached)

### Resource & Memory Management

- **QWidget ownership:** always specify a parent to avoid memory leaks
  - Exception: top-level windows (MainWindow, LoginWindow) have no parent and handle their own cleanup
- **QSettings:** persisted immediately on value changes; no cleanup required
- **QTimer:** stop timers before object deletion to avoid dangling timer events
- **File handles:** use RAII or explicit close; no resource leaks in error paths

## Automated Testing & Verification

### Testing Strategy: Aspirational Goal

The repository **currently has no automated tests**, but **new code should be written with testing in mind**. The long-term goal is comprehensive test coverage using Qt's QTest framework.

**Current state:**
- Testing is primarily manual: visual testing, integration testing against real Fritz!Box, manual QA on each distro
- Rationale for current gap: Qt GUI testing with QTest requires mocking FritzApi responses, managing window lifecycle, and screenshot regression tests—historically exceeded project scope for a single-developer app

**New direction:**
- **New features and refactorings MUST write testable code** even if automated tests are not immediately written
- Follow the "Writing Code for Testability" guidelines (see below) so future contributors can add tests without major refactoring
- Prefer OOP designs that are naturally mockable (separate `FritzApi`, `ChartBuilder`, etc. from UI layer)

### Test Architecture (When Implementing)

When automated tests are added, use this architecture:

**Unit tests** (QTest framework):
```cpp
// tests/test_fritzapi.cpp
class TestFritzApi : public QObject {
    Q_OBJECT
private slots:
    void testLoginPbkdf2Response();      // Pure crypto logic, no network
    void testDeviceListCaching();        // Cache TTL validation
    void testRequestDeduplication();     // Multiple callbacks on single request
};

// tests/test_devicemodel.cpp
class TestDeviceModel : public QObject {
    Q_OBJECT
private slots:
    void testGroupBucketPriority();      // Verify sort order
    void testTreeStructure();            // Verify 2-level hierarchy
};

// tests/test_chartbuilder.cpp
class TestChartBuilder : public QObject {
    Q_OBJECT
private slots:
    void testEnergyHistoryResolutions(); // Single vs. stacked, 3 grids
    void testStackedBarChartAccuracy();  // Per-member accumulation
};
```

**Integration tests** (QTest + mock Fritz!Box):
```cpp
// tests/integration/test_mainwindow.cpp
class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void testDeviceSelectionAndPanelSwitch();  // UI workflow
    void testChartTabRefresh();                 // Signal routing
    void testGroupEnergyHistoryFlow();          // Async member fetching
};
```

**Mock infrastructure:**
```cpp
// tests/mocks/MockFritzApi.h
class MockFritzApi : public FritzApi {
    Q_OBJECT
public:
    void injectDeviceList(const FritzDeviceList &devices);
    void injectDeviceStats(const QString &ain, const DeviceBasicStats &stats);
    void simulateNetworkError(const QString &error);
    // Allows tests to verify signals without real network calls
};
```

### Compiler Warnings and Build-Time Checks

- **Compiler:** GCC 14 (standard on Tumbleweed, Leap 16.0) or system default
- **Flags:** `-std=c++17` (required); standard warning set (no custom `-Wall` additions)
- **Known false positive:** GCC 15's `-Wstringop-overflow` in `chartwidget.cpp` range analysis, suppressed via per-file `COMPILE_OPTIONS` (GCC-only guard in CMakeLists.txt, line 179-183)
- **Qt moc warnings:** moc may warn about forward-declared types in signals; these are safe and expected
- **No static analysis tools** (clang-tidy, cppcheck) currently configured; PRs are code-reviewed manually

### Runtime Checks

- **Debug builds** enable `qDebug()` output and Qt runtime assertions
  - Build with `-DCMAKE_BUILD_TYPE=Debug` for development
  - Logs to stderr; capture via `2>&1` if needed
- **Runtime debugging:** inspect network flow by checking FritzApi cache hits/misses in `fritzapi.cpp::onAsyncReply()`
- **Valgrind:** not typically used (X11/Wayland interaction complexity); for memory checking, run under Docker with native tools

### Before Committing

1. **Build successfully in Docker** on at least one distro profile:
   ```bash
   ./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release
   ```
   Output: binary and packages in `out/opensuse/tumbleweed/x86_64/`

2. **Run automated tests** (once test suite is in place) — run inside Docker:
   ```bash
   # Build with tests enabled
   ./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug
   
   # Then run tests inside the container
   # (exact command depends on test setup, but tests are built as part of docker/build.sh)
   ```

3. **Run and interact** with the built binary:
   - Extract and run the binary from `out/opensuse/tumbleweed/x86_64/fritzhome`
   - Connect to Fritz!Box, verify device list loads
   - Switch a device on/off, check refresh timing
   - Expand/collapse groups, verify tree state persists
   - Switch to Energy History tab, check chart renders without crashes

4. **Check for obvious issues** in the Docker build output:
   - New compilation warnings (non-GCC-15-false-positives)
   - Uninitialized variables (use `Q_ASSERT()` or assertions in debug builds if needed)
   - Memory leaks (particularly in chart teardown and DeviceModel reset flows)

5. **Cross-version testing:** if touching async, signals, or translation code, test both Qt5 and Qt6 builds in Docker:
   ```bash
   ./docker/build.sh --distro opensuse-leap-15.6-x86_64 --build-type Release   # Qt5
   ./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release  # Qt6
   ```

### Writing Code for Testability (Essential for New Code)

**This is mandatory for all new features and refactorings:**

- **Separate concerns:** keep FritzApi (network) independent of UI (MainWindow, ChartWidget)
  - FritzApi should not directly manipulate Qt widgets
  - UI should not know about cache keys or request deduplication internals
  - Benefit: FritzApi can be tested with injected responses without launching UI
- **Use callbacks / signals:** instead of blocking for results, emit signals or invoke callbacks
  - This allows tests to inject mock responses and verify async behavior
  - Example: `fetchDeviceList(onSuccess, onError)` callback pattern is testable; synchronous `getDeviceList()` is not
- **Stateless helpers:** utility functions (e.g., `parseDeviceListJson()`, `computePbkdf2Response()`) should be pure or side-effect-free
  - Avoids global state surprises in tests
  - Benefit: can be unit-tested with zero infrastructure
- **Extract builder classes:** complex object construction (e.g., `EnergyHistoryBuilder`, `ChartFactory`) should be in separate classes
  - Example: `EnergyHistoryBuilder { buildFor(device, resolution) }` instead of 50-line method
  - Benefit: builder can be tested independently with mock data
- **Dependency injection:** pass dependencies (e.g., `FritzApi *api`) as constructor parameters, not globals
  - Example: `ChartWidget(FritzApi *api)` allows injecting `MockFritzApi` in tests
  - Avoid: static methods, global singletons, direct file I/O
- **QSettings keys as constants:** define as `static const char *` (e.g., `kSettingInterval = "connection/interval"`)
  - Makes it easier to verify settings in tests; prevents typos
- **Enums over magic strings:** use `enum class DeviceType { Switch, Thermostat, ... }` instead of string comparisons
  - Benefit: compiler enforces exhaustive case coverage in switch statements; tests can verify all paths

### CMakeLists.txt: Adding Test Infrastructure (Future)

When tests are added, update CMakeLists.txt:
```cmake
# ── Enable testing ─────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# tests/CMakeLists.txt
find_package(Qt${_QT_MAJOR} REQUIRED COMPONENTS Test)

add_executable(test_fritzapi test_fritzapi.cpp ${MOCK_SOURCES})
target_link_libraries(test_fritzapi Qt${_QT_MAJOR}::Test Qt${_QT_MAJOR}::Network)
add_test(NAME FritzApi COMMAND test_fritzapi)

add_executable(test_devicemodel test_devicemodel.cpp)
target_link_libraries(test_devicemodel Qt${_QT_MAJOR}::Test Qt${_QT_MAJOR}::Gui)
add_test(NAME DeviceModel COMMAND test_devicemodel)

# ... more tests
```

Then run tests inside Docker:
```bash
# Build with tests enabled in Docker
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# The test suite will be built and executed as part of docker/build.sh
# Build artifacts including test executables are in:
# build/opensuse/tumbleweed/x86_64/
```

## Testing & Development Notes

- **Debug builds** enable `qDebug()` output — useful for network flow tracing
- **Cross-compilation:** aarch64 target on x86_64 host uses `cross-aarch64-gcc14` + sysroot from openSUSE ports mirror (see ARCHITECTURE.md §Cross-Compilation for details)
- **TLS certificate warnings:** checkbox in login dialog bypasses cert validation (`m_ignoreSsl` flag); persisted to QSettings
- **Docker build validation:** all five distro profiles built successfully as part of release process; output in `out/` directory

## Common Agent Pitfalls

1. **Qt version confusion** — code must support both Qt5 and Qt6; use `#ifdef`/`#if QT_VERSION_MAJOR` for version-specific code. Conversion utilities differ (QDateTime methods, container APIs, etc.)

2. **KDE Frameworks optional** — never assume KWallet, KI18n, or KXmlGui APIs are available. Always check `#if HAVE_KF` and provide fallback codepath.

3. **Async + signals everywhere** — FritzApi has no blocking I/O; all network calls are async with callbacks/signals. MainWindow must wire FritzApi signals to slots; DeviceWidgets must pass `m_api` pointer to register their command slots.

4. **DeviceModel wholesale reset** — any device list change triggers full `beginResetModel()/endResetModel()`, clearing QTreeView selection. MainWindow must restore `m_selectedAin` and re-select after reset to avoid flickering UI.

5. **ChartWidget energy state** — Energy History tab state is complex (3 resolutions, single vs. stacked, error displays, skip-rebuild cache). Touch `m_activeEnergyGrid` carefully; reset to 0 when chart enters non-chart state to prevent stale comparisons on next data arrival.

6. **Password backend at compile time** — password storage backend is chosen at CMake config time based on `HAVE_KF` and `HAVE_LIBSECRET` macros. Runtime fallback order is hardcoded; cannot be changed after build.

7. **Translation system split** — KF builds use `.po` → `.mo` (installed to system share/locale at package install time); no-KF builds embed `.qm` as Qt resource (self-contained binary). Editing translations requires understanding both paths.

8. **Group energy history complexity** — stacked bar chart requires fetching stats for **every group member**, then routing replies back to accumulate in `m_groupMemberStats`, then calling `ChartWidget::updateGroupEnergyStats()` when all replies arrive. Missing a member means incomplete stacked bars. See ARCHITECTURE.md §Stacked Energy History Bar Chart for Groups for detailed flow.

9. **GCC 15 compiler warning** — `-Wstringop-overflow` false positive in chartwidget.cpp, suppressed via per-file `COMPILE_OPTIONS` in CMakeLists.txt (GCC-only guard).

10. **Cross-compilation aarch64** — build system has special handling for `tumbleweed-aarch64` via `cmake/toolchain-aarch64.cmake`. If modifying CMake config, test both x86_64 and aarch64 Docker builds to avoid linker/sysroot surprises.

## Useful Commands

**All commands use Docker build environment (recommended approach):**

```bash
# Build for a single distro (Tumbleweed x86_64, Release)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# Build for all five supported distros (full validation)
./docker/build.sh --distro all --build-type Release

# Debug build with qDebug output enabled (fastest for testing)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Qt5-only build (Leap 15.6)
./docker/build.sh --distro opensuse-leap-15.6-x86_64 --build-type Release

# Qt6 + KF6 build (Leap 16.0)
./docker/build.sh --distro opensuse-leap-16.0-x86_64 --build-type Release

# Cross-compiled aarch64 binary (x86_64 host → aarch64 target)
./docker/build.sh --distro opensuse-tumbleweed-aarch64 --build-type Release

# Ubuntu 24.04 Qt6-only build
./docker/build.sh --distro ubuntu-24.04-x86_64 --build-type Release

# Run the built binary from Docker output
./out/opensuse/tumbleweed/x86_64/fritzhome --host fritz.box --username user --password pass --interval 5

# Inspect generated RPM packages
rpm -qi out/opensuse/tumbleweed/x86_64/fritzhome-*.rpm
rpm -qi out/opensuse/leap15.6/x86_64/fritzhome-*.rpm
rpm -qi out/opensuse/tumbleweed/aarch64/fritzhome-*.aarch64.rpm

# Inspect generated DEB packages
dpkg -c out/ubuntu/24.04/amd64/fritzhome_*.deb
```

## Deployment & Release Guidance

### Release Checklist

The release process ensures all five supported distro profiles build successfully with consistent binaries and packages.

**Pre-release validation (execute in this order):**

1. **Unit and integration tests** (if suite exists):
   ```bash
   ./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug
   # Tests are built and run as part of docker/build.sh; inspect build logs for PASSED/FAILED
   ```

2. **Build all five distro profiles** (full validation):
   ```bash
   ./docker/build.sh --distro all --build-type Release
   # Output: binaries and packages in out/ directory tree
   ```

3. **Verify binaries and packages exist** in `out/`:
   - `out/opensuse/tumbleweed/x86_64/fritzhome` (binary, amd64)
   - `out/opensuse/tumbleweed/aarch64/fritzhome` (binary, aarch64)
   - `out/opensuse/leap-15.6/x86_64/fritzhome` (binary, Qt5)
   - `out/opensuse/leap-16.0/x86_64/fritzhome` (binary, Qt6+KF6)
   - `out/ubuntu/24.04/amd64/fritzhome` (binary, Ubuntu Qt6)
   - All corresponding `.rpm` and `.deb` packages

4. **Spot-check binary signatures** (verify executable, not corrupted):
   ```bash
   file out/opensuse/tumbleweed/x86_64/fritzhome
   # Expected: ELF 64-bit LSB executable, x86-64, dynamically linked, ...
   
   file out/opensuse/tumbleweed/aarch64/fritzhome
   # Expected: ELF 64-bit LSB executable, ARM aarch64, dynamically linked, ...
   ```

5. **Test at least one binary** against a real Fritz!Box:
   ```bash
   ./out/opensuse/tumbleweed/x86_64/fritzhome --host fritz.box --username admin --password <pwd> --interval 5
   # Verify: device list loads, can switch devices, Energy History renders, no segfaults
   ```

6. **Verify package contents** (spot-check):
   ```bash
   # RPM: verify binary is in correct path
   rpm -qpl out/opensuse/tumbleweed/x86_64/fritzhome-*.rpm | grep -E "bin/fritzhome|share/locale"
   
   # DEB: verify binary and desktop file
   dpkg -c out/ubuntu/24.04/amd64/fritzhome_*.deb | grep -E "usr/bin/fritzhome|usr/share/applications"
   ```

### Versioning Strategy

**Version format:** `MAJOR.MINOR.PATCH` (e.g., `1.2.3`)

- **MAJOR:** breaking API changes, major UI restructure
- **MINOR:** new features (new device type, new chart resolution), significant refactorings
- **PATCH:** bug fixes, performance improvements, translation updates

**Version updates in repository:**

- Update version in `CMakeLists.txt` → `project(fritzhome VERSION 1.2.3 ...)`
- Update version in `src/main.cpp` → version string for `--version` CLI flag (if exists)
- Update version in packaging metadata (`.spec` file for RPM, debian/changelog for DEB)
- Tag git commit: `git tag -a v1.2.3 -m "Release 1.2.3 — added energy history improvements"`

**Release workflow:**

1. Create release branch: `git checkout -b release/v1.2.3`
2. Update version numbers (3 places above)
3. Run full build and tests: `./docker/build.sh --distro all --build-type Release`
4. Verify binaries and packages (checklist above)
5. Commit: `git commit -am "Release v1.2.3"`
6. Tag: `git tag -a v1.2.3 -m "Release 1.2.3"`
7. Push branch and tag: `git push origin release/v1.2.3 && git push origin v1.2.3`
8. Open pull request for release branch into main (for code review before merging)
9. Once approved: merge release branch into main, delete release branch

### Package Distribution

**openSUSE (RPM):**
- Host packages on OBS (openSUSE Build Service) or GitHub Releases
- Package spec: `.spec` file in repository root (generated or hand-crafted)
- Build target: `openSUSE_Tumbleweed`, `Leap_15.6`, `Leap_16.0` OBS repositories
- Installation: `zypper install fritzhome` (if added to official repos)

**Ubuntu (DEB):**
- Host packages on GitHub Releases or PPA (Personal Package Archive)
- Package metadata: `debian/control`, `debian/changelog` in repository
- Build target: Ubuntu 24.04 LTS (Jammy)
- Installation: `sudo apt install ./fritzhome_*.deb` (from local file)

**Artifact retention:**
- Keep built binaries and packages in `out/` directory for 1-2 weeks for manual testing
- Archive to GitHub Releases for permanent availability
- Delete old Docker build artifacts to save disk space: `./docker/build.sh --clean-artifacts` (if script supports it)

## Troubleshooting Docker Builds

### Common Docker Build Failures

**Symptom: "Docker daemon not running" or "Cannot connect to Docker socket"**
```
Error: Cannot connect to Docker daemon at unix:///var/run/docker.sock
```
**Solution:**
- Ensure Docker daemon is running: `sudo systemctl start docker` (or `service docker start`)
- Verify user is in docker group (no `sudo` required): `groups $USER | grep docker`
- If not: `sudo usermod -aG docker $USER` (requires re-login)

**Symptom: "Permission denied while trying to connect to Docker daemon"**
```
docker: permission denied while trying to connect to Docker daemon
```
**Solution:**
- User is not in docker group; run: `sudo usermod -aG docker $USER`
- Log out and log back in for group membership to take effect
- Or use `sudo ./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release`

**Symptom: "Disk space low" or "No space left on device"**
```
Error: docker build failed: no space left on device
```
**Solution:**
- Clean up old Docker images/containers: `docker system prune -a --volumes`
- This removes all unused images, containers, and volumes (safe if you're not using Docker for other projects)
- Increase Docker storage location if on small partition (check `docker info | grep "Docker Root Dir"`)

**Symptom: "CMake version too old" or "Qt library not found"**
```
CMake Error: version 3.21 or higher required
```
**Solution:**
- Docker image has all required tools pre-configured; this should not occur
- If it does: verify you're using the correct distro string: `./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release`
- Try rebuilding Docker image: `docker build -f docker/Dockerfile.opensuse-tumbleweed-x86_64 -t fritzhome-builder:tumbleweed .`

**Symptom: "Network error: Failed to fetch package" or "curl: connection timeout"**
```
ERROR: Could not resolve host: download.opensuse.org
```
**Solution:**
- Network connectivity issue (Docker container cannot reach package mirrors)
- Ensure host has internet access: `ping 8.8.8.8`
- Try building again; transient network failures are common
- If persistent: Docker container may have misconfigured DNS; check Docker network settings

**Symptom: "Cross-compilation error: cannot execute binary file"**
```
Error: /usr/bin/aarch64-linux-gnu-gcc: cannot execute binary file
```
**Solution:**
- Attempting aarch64 cross-compilation on host without cross-compiler support
- Use Docker: `./docker/build.sh --distro opensuse-tumbleweed-aarch64 --build-type Release`
- Docker image includes aarch64 cross-compiler toolchain pre-installed
- Do NOT attempt manual cross-compilation without Docker

**Symptom: "Git repository state is dirty; cannot build"**
```
Warning: uncommitted changes in working tree; build may include local modifications
```
**Solution:**
- This is a warning, not an error (build will proceed)
- Commit or stash changes: `git commit -am "message"` or `git stash`
- If intentional (testing local changes): ignore warning

**Symptom: "Test execution failed" or "ctest: command not found"**
```
Error: ctest failed with exit code 1
```
**Solution:**
- Tests are built and executed inside Docker container as part of `docker/build.sh`
- Check Docker build logs for test output (usually printed to stdout)
- Run Debug build for more verbose test output: `./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug`
- If tests fail, fix issues in code and rebuild; tests are not run separately

### Docker Volume Mount Issues

**Symptom: "Cannot write to output directory" or "Permission denied on out/"**
```
docker: error response from daemon: mkdir: permission denied
```
**Solution:**
- Docker container runs as root; output files may be owned by root
- Change ownership: `sudo chown -R $USER:$USER out/`
- Or set Docker to run as current user (advanced; not recommended for this project)

**Symptom: "Build output not found in out/ directory"**
```
ls: cannot access out/opensuse/tumbleweed/x86_64/: No such file or directory
```
**Solution:**
- Build may have failed silently; check Docker build exit code: `echo $?` (0 = success)
- Run build again with verbose output: `./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release 2>&1 | tee build.log`
- Inspect `build.log` for CMake or compile errors
- Ensure `docker/build.sh` script has execute permission: `chmod +x docker/build.sh`

### Build Performance Issues

**Slow build (>10 minutes for single distro):**
- First build is slower (downloads base Docker image, installs dependencies)
- Subsequent builds should be faster (cached layers)
- Parallel builds for multiple distros will slow each one (CPU contention); build sequentially for speed
- Check system resources: `free -h` (memory), `df -h /` (disk space), `nproc` (CPU cores)

## Contribution Workflow

### Fork, Branch, and Pull Request

**Initial setup (first-time contributors):**

1. Fork repository on GitHub: click "Fork" button at https://github.com/flunaras/fritzhome
2. Clone your fork locally:
   ```bash
   git clone https://github.com/<your-username>/fritzhome.git
   cd fritzhome
   ```
3. Add upstream remote for pulling latest changes:
   ```bash
   git remote add upstream https://github.com/flunaras/fritzhome.git
   ```

**For each contribution:**

1. Create feature branch from latest upstream main:
   ```bash
   git fetch upstream
   git checkout -b feature/short-description upstream/main
   # Example: git checkout -b feature/add-humidity-sensor-ui upstream/main
   ```

2. Make changes and commit:
   ```bash
   git add src/humiditysensorwidget.cpp src/humiditysensorwidget.h
   git commit -m "Add humidity sensor UI widget with real-time display"
   ```

3. Push to your fork:
   ```bash
   git push origin feature/short-description
   ```

4. Open pull request on GitHub:
   - Base: `flunaras/fritzhome:main`
   - Compare: `<your-username>/fritzhome:feature/short-description`
   - Title: short imperative sentence (e.g., "Add humidity sensor UI widget")
   - Description: 2-3 bullet points explaining the change (see PR template below)

### Commit Message Format

**Format:** `<verb> <noun>: <subject>`

- **Verb:** add, fix, refactor, docs, ci, test, style (lowercase, imperative mood)
- **Noun:** component affected (MainWindow, ChartWidget, FritzApi, CMakeLists, etc.)
- **Subject:** 1-2 sentences explaining "what" and "why" (not "how")

**Examples:**

```
add: MainWindow device history chart panel
  - Adds new "History" tab to device control panel
  - Uses ChartWidget::renderDeviceHistory() to display 24-hour statistics

fix: FritzApi request deduplication memory leak
  - Clear callback queue after invoking final callback
  - Prevents stale QPointer references in subsequent requests

refactor: ChartWidget energy history into separate builder class
  - Extracts 300-line energy history logic into EnergyHistoryBuilder
  - Improves testability; allows mocking chart data

docs: AGENTS.md update for cross-version Qt5/Qt6 testing
  - Add section on version compatibility guards
  - Include example unit tests for version-specific code

ci: GitHub Actions Qt6 cross-compilation validation
  - Build aarch64 binary as part of PR checks
  - Catches linker errors early
```

**Bad examples (avoid):**

```
Fixed stuff
Fix compilation error
Added new feature
Update
Change something
```

### Code Review Expectations

**Reviewer checks:**

1. **Follows code quality standards** (AGENTS.md §Code Quality Standards):
   - Naming conventions (`m_` prefix, `k` prefix, camelCase)
   - No nested if/else chains longer than 3 levels (extract methods)
   - OOP polymorphism over conditionals
   - Memory safety (proper parent/child ownership, no raw pointers except QPointer)

2. **Handles version compatibility** (Qt5, Qt6, KDE Frameworks):
   - Version-specific code guarded with `#if QT_VERSION_MAJOR == 6`
   - KDE-specific code guarded with `#if HAVE_KF`
   - Compiles and runs on both Qt5 and Qt6 Docker profiles

3. **Async and signal integrity:**
   - All FritzApi calls async; no blocking I/O
   - Signal/slot chains properly connected in constructors
   - No heap corruption from slot invocation after object deletion

4. **Tests (if new test suite exists):**
   - Unit tests for new logic (if testable)
   - Integration tests for UI changes
   - Tests pass in Docker builds (Debug mode)

5. **Documentation:**
   - Public class docstrings explain purpose and single responsibility
   - Complex methods documented with "why" not "what"
   - AGENTS.md updated if adding new architectural patterns

**Approval:**
- Repository owner (flunaras) approves before merge
- Automated checks (Docker builds, tests) must pass
- At least one code review required

### Local Development Workflow

**Setup dev environment:**

```bash
# Clone and setup remotes (if not already done)
git clone https://github.com/flunaras/fritzhome.git
cd fritzhome
git remote add upstream https://github.com/flunaras/fritzhome.git

# Ensure you're on main
git checkout main
git pull upstream main

# Create feature branch
git checkout -b feature/my-feature
```

**Build and test:**

```bash
# Build Debug version (fastest, includes qDebug output)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Run the binary
./out/opensuse/tumbleweed/x86_64/fritzhome --host fritz.box --username admin --password <pwd>

# Verify visually:
# - Device list loads
# - Can switch devices on/off
# - Charts render without crash
# - No segfaults in qDebug output
```

**Before pushing:**

```bash
# Build Release version (final validation)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# If touching async/signals/translation, also test Qt5:
./docker/build.sh --distro opensuse-leap-15.6-x86_64 --build-type Release

# Commit and push
git add <files>
git commit -m "feature: ComponentName clear description"
git push origin feature/my-feature

# Open PR on GitHub
```

## Quick-Reference Checklists

### Before Opening a Pull Request

- [ ] Code compiles without warnings (in Docker Release build)
- [ ] Follows naming conventions: `m_camelCase` for members, `kCamelCase` for constants
- [ ] No nested if/else chains longer than 3 levels; extracted methods as needed
- [ ] All async calls are truly async (no blocking I/O); FritzApi integration is non-blocking
- [ ] Version compatibility guards: `#if QT_VERSION_MAJOR == 6` for Qt6-specific code
- [ ] KDE Frameworks optional: `#if HAVE_KF` guards around KDE APIs; fallback codepath provided
- [ ] Memory safety: QObjects have parents (except top-level windows); no raw pointers (use QPointer)
- [ ] Signal/slot chains wired in constructor; slots properly connected
- [ ] Builds on at least one distro profile: `./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release`
- [ ] If modifying async/signals/translation, tested on both Qt5 and Qt6 profiles
- [ ] Commit message format: `verb: Component short description` (e.g., `fix: FritzApi memory leak`)
- [ ] Updated AGENTS.md if introducing new architectural patterns
- [ ] Added docstrings for public classes explaining single responsibility
- [ ] Complex methods documented with inline comments explaining "why"

### Adding a New Device Widget

1. [ ] Create `src/new_device_widget.h` and `src/new_device_widget.cpp`
2. [ ] Inherit from `DeviceWidget` (abstract base class)
3. [ ] Implement virtual methods:
   - `updateDevice(const FritzDevice &device)` — refresh UI with new device state
   - `onDeviceCommand(const QString &command)` — send command to FritzBox via FritzApi
4. [ ] Add constructor accepting `FritzApi *api` for network access
5. [ ] Wire signal/slot connections in constructor (not in loops)
6. [ ] Update `MainWindow::updateDevicePanel()` to create widget for new device type:
   ```cpp
   if (device.isNewDeviceType()) {
       panel = new NewDeviceWidget(m_api, this);
   }
   ```
7. [ ] Update `CMakeLists.txt` to include new `.cpp` and `.h` files in SOURCES
8. [ ] Test: build Debug, run binary, verify device list loads and widget displays correctly
9. [ ] Follow OOP guidelines: widget logic only; no deeply nested if/else
10. [ ] Add docstring explaining device type, capabilities, and async contract

### Fixing a Crash or Memory Leak

1. [ ] Reproduce crash: collect error message, stack trace (if available)
2. [ ] Identify code path: crash in FritzApi, MainWindow, or DeviceWidget?
3. [ ] Debug locally:
   ```bash
   ./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug
   ./out/opensuse/tumbleweed/x86_64/fritzhome --host fritz.box --username admin --password <pwd> 2>&1 | tee debug.log
   # Examine debug.log for qDebug messages and stack trace
   ```
4. [ ] Identify root cause:
   - **Memory corruption:** check parent/child ownership, QPointer usage, signal/slot safety
   - **Use-after-free:** object deleted before slot invoked; add null check in slot
   - **Null pointer dereference:** guard pointers with `if (ptr)` before use
   - **Resource leak:** ensure QTimer stopped, file handles closed, sockets destroyed
5. [ ] Write minimal fix (one issue per commit)
6. [ ] Test fix: rebuild Debug, reproduce original crash, verify no regression
7. [ ] Document fix in commit message: what was wrong and how it's fixed
8. [ ] Consider extracting root cause into separate refactoring (if part of larger pattern)

### Refactoring Long Function

1. [ ] Identify the long function (>40 lines or >3 nesting levels)
2. [ ] Analyze structure: find logical blocks that can be extracted
3. [ ] Create smaller private methods for each block:
   - `private: void setupLayouts();`
   - `private: void wireSignals();`
   - `private: void initializeData();`
4. [ ] Move code into new methods; update original to call them
5. [ ] Test: build, run, verify no behavior change
6. [ ] If applicable, consider OOP extraction:
   - Does this logic belong in a separate class? (builder pattern)
   - Can this logic be polymorphic? (virtual method in base class)
7. [ ] Document why extraction was done (improves testability, reduces complexity, etc.)
8. [ ] Commit: `refactor: ClassName extract method for clarity`

## Performance Profiling Notes

### When to Profile

- **New feature sluggish:** device list takes >2s to render, Energy History chart jittery, response to user input delayed
- **Regression suspected:** feature worked fast in previous version, now slow
- **Resource constraints:** running on lower-end hardware (older Raspberry Pi, embedded systems)
- **Release optimization:** before shipping, profile and identify bottlenecks

### Profiling Tools and Approaches

**Qt Performance Tools (built into Qt):**

1. **QElapsedTimer** (code-level timing):
   ```cpp
   QElapsedTimer timer;
   timer.start();
   
   // Code to measure
   fetchDeviceList();
   
   qDebug() << "fetchDeviceList took" << timer.elapsed() << "ms";
   ```
   - Compile with Debug build: `./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug`
   - Run binary, capture qDebug output: `./out/opensuse/tumbleweed/x86_64/fritzhome 2>&1 | grep "took.*ms"`

2. **Qt Creator Profiler** (if using Qt Creator IDE):
   - Build with Debug symbols
   - Run → Analyze → QML Profiler (or CPU profiler if available)
   - Visualize time spent in each function

3. **qDebug() call counting:**
   ```cpp
   static int callCount = 0;
   qDebug() << "updateDevice called" << ++callCount << "times";
   ```
   - Helps identify unexpected call patterns (e.g., updating same device 100 times)

**System-level profiling (inside Docker):**

```bash
# Build Debug version
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Run binary with timing analysis (using Linux 'time' command)
time ./out/opensuse/tumbleweed/x86_64/fritzhome --host fritz.box --username admin --password <pwd> &
sleep 5  # Let app run briefly
kill %1

# Output shows user CPU time, system CPU time, real elapsed time
```

**Memory profiling (valgrind, inside Docker container):**

```bash
# Valgrind is often pre-installed in Docker image
# Run binary under Valgrind (slower, high overhead)
valgrind --leak-check=full --show-leak-kinds=all \
  ./out/opensuse/tumbleweed/x86_64/fritzhome --host fritz.box --username admin --password <pwd>

# Output shows memory leaks, invalid accesses, heap summary
# WARNING: Valgrind slows execution significantly; interpret results carefully
```

### Common Bottlenecks and Solutions

**Symptom: Device list takes 2+ seconds to render**

- **Likely cause:** DeviceModel rebuild (`beginResetModel()/endResetModel()`) is expensive
- **Profile:** add QElapsedTimer in MainWindow when updating device model
- **Solution:** 
  - Reduce JSON parsing complexity (if parsing large arrays inefficiently)
  - Use incremental model updates instead of wholesale reset (if feasible)
  - Cache device grouping logic to avoid re-sorting on each poll

**Symptom: Energy History chart lags when scrolling or resizing**

- **Likely cause:** chart redraw logic is inefficient; too many geometry calculations
- **Profile:** add QElapsedTimer in `ChartWidget::updateEnergyStats()`
- **Solution:**
  - Cache chart axes, labels, series between redraws (skip-rebuild guard)
  - Use `setAnimationOptions(QChart::NoAnimation)` if animations are slow
  - Limit data points rendered (subsample if >1000 points)

**Symptom: UI unresponsive when connected to Fritz!Box**

- **Likely cause:** blocking I/O or long computation on UI thread
- **Profile:** check for `waitForReadyRead()`, `processEvents()`, or synchronous file I/O
- **Solution:**
  - Ensure FritzApi calls are async (with callbacks, not blocking)
  - Move heavy computation to background thread (QThread) and signal UI when done
  - Use `QCoreApplication::processEvents()` sparingly (last resort)

**Symptom: High CPU usage at idle (app spinning)**

- **Likely cause:** timer firing too frequently, infinite loop, or cache thrashing
- **Profile:** check QTimer intervals in FritzApi polling, DeviceModel update frequency
- **Solution:**
  - Verify polling interval is reasonable (default 5s, not 100ms)
  - Check for duplicate timers (same code path firing multiple times)
  - Verify cache TTL is appropriate (not causing unnecessary refetches)

### Profiling Checklist

- [ ] Identify slow operation (device list, chart render, response time)
- [ ] Build Debug version: `./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug`
- [ ] Add QElapsedTimer around suspected code; rebuild
- [ ] Run binary and capture qDebug output: `./out/opensuse/tumbleweed/x86_64/fritzhome 2>&1 | tee profile.log`
- [ ] Analyze timing: which function takes most time?
- [ ] Hypothesis: why is it slow? (algorithm, I/O, rendering, etc.)
- [ ] Implement optimization; rebuild
- [ ] Re-measure: timing should improve
- [ ] Document optimization in commit message

## References

- **ARCHITECTURE.md** — deep dive into class hierarchy, window layout, FritzApi flow, chart widget state, settings persistence, and cross-compilation details
- **README.md** — features, device support, password security, build instructions, running/CLI options
- **CMakeLists.txt** — build configuration, Qt/KF version selection, dependency discovery, packaging rules
