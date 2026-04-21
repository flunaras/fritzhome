# Fritz!Home — Architecture Documentation

## Overview

**Fritz!Home** (`fritzhome`) is a Qt desktop application for monitoring and controlling AVM Fritz!Box
Smart Home devices via the Fritz!Box Smart Home REST API. It is a single-window application with a
two-panel layout: a grouped device tree on the left and a context-sensitive control/chart
panel on the right.

Supported build targets:

| Distro | Qt | KDE Frameworks | `USE_KF` | Package | Arch |
|---|---|---|---|---|---|
| openSUSE Leap 15.6 | Qt 5 | None (libsecret fallback) | `0` | RPM | x86_64 |
| openSUSE Leap 16.0 | Qt 6 | KF6 via KDE:Extra OBS repo | `6` | RPM | x86_64 |
| openSUSE Tumbleweed | Qt 6 | KF6 (standard repo) | `6` | RPM | x86_64 |
| openSUSE Tumbleweed (aarch64) | Qt 6 | KF6 (ports mirror) | `6` | RPM | aarch64 |
| Ubuntu 24.04 LTS | Qt 6 | None (libsecret fallback) | `qt6` | DEB | x86_64 |

Optional KDE Frameworks support is compiled in via `HAVE_KF=1` when `USE_KF` is 5 or 6.

---

## Source File Map

```
src/
├── main.cpp                     Entry point; CLI parsing; creates MainWindow
├── mainwindow.h / .cpp          Top-level window; owns all major objects
├── loginwindow.h / .cpp         Modal credentials dialog (host, username, password, auto-login)
│
├── fritzdevice.h                Plain data structs: FritzDevice, all *Stats
├── fritzapi.h / .cpp            Network layer — Fritz!Box Smart Home REST API client (async, cached, deduplicated)
│
├── devicemodel.h / .cpp         QAbstractItemModel — 2-level tree model
├── devicewidget.h / .cpp        Abstract base for all control panels
│
├── switchwidget.h / .cpp        Panel: switchable outlet
├── thermostatwidget.h / .cpp    Panel: radiator controller (HKR)
├── energywidget.h / .cpp        Panel: energy meter (read-only)
├── dimmerwidget.h / .cpp        Panel: dimmer / bulb level
├── blindwidget.h / .cpp         Panel: roller blind / jalousie
├── colorwidget.h / .cpp         Panel: RGBW color bulb
├── humiditysensorwidget.h / .cpp Panel: humidity sensor (read-only)
├── alarmwidget.h / .cpp         Panel: door/window alarm sensor
│
├── chartwidget.h / .cpp         Qt Charts time-series and energy history
├── secretstore.h / .cpp         Cross-backend password storage (KWallet / libsecret / QSettings)
└── i18n_shim.h                  i18n() macro — maps to KI18n or QCoreApplication::translate() depending on HAVE_KF
```

---

## Class Hierarchy

```
QObject
└── QMainWindow  (or KXmlGuiWindow when HAVE_KF=1)
    └── MainWindow

QAbstractItemModel
└── DeviceModel

QWidget
└── QDialog
    └── LoginWindow
└── DeviceWidget  (abstract)
    ├── SwitchWidget
    ├── ThermostatWidget
    ├── EnergyWidget
    ├── DimmerWidget
    ├── BlindWidget
    ├── ColorWidget
    ├── HumiditySensorWidget
    └── AlarmWidget
└── ChartWidget

QObject
└── FritzApi
```

Data structures (not QObject-derived, plain structs in `fritzdevice.h`):

```
FritzDevice
├── SwitchStats
├── EnergyStats
├── ThermostatStats
├── DimmerStats
├── ColorStats
├── BlindStats
├── HumidityStats
├── AlarmStats
├── DeviceBasicStats
│   └── StatSeries[]
└── History lists (temperatureHistory, powerHistory, humidityHistory)
```

---

## Window Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│  MenuBar:  File > Connect… | Refresh | Quit                             │
├────────────────────────┬────────────────────────────────────────────────┤
│                        │  [icon 32x32]  Device Name  (bold, large)      │
│  QTreeView             ├────────────────────────────────────────────────┤
│  (m_deviceTree)        │  QStackedWidget (m_controlStack)               │
│                        │   index 0: "Select a device…" placeholder      │
│  Root level:           │   index 1: SwitchWidget        (in QScrollArea)│
│   ▶ Smart Plugs  (3)   │   index 2: ThermostatWidget    (in QScrollArea)│
│   ▼ Thermostats  (2)   │   index 3: EnergyWidget        (in QScrollArea)│
│     • Living Room      │   index 4: DimmerWidget        (in QScrollArea)│
│     • Bedroom          │   index 5: BlindWidget         (in QScrollArea)│
│   ▶ Blinds       (1)   │   index 6: ColorWidget         (in QScrollArea)│
│   ▶ Color Bulbs  (2)   │   index 7: HumiditySensorWidget(in QScrollArea)│
│   ...                  │   index 8: AlarmWidget         (in QScrollArea)│
├────────────────────────┤                                                │
│  Refresh interval: [▲] ├────────────────────────────────────────────────┤
│  spinbox (2–300 s)     │  ChartWidget (m_chartWidget)                   │
│                        │   QTabWidget:                                  │
│                        │    • Temperature  (QChartView + time-window    │
│                        │                   combo overlay, top-left)     │
│                        │    • Power        (QChartView + time-window    │
│                        │                   combo overlay, top-left)     │
│                        │    • Humidity     (QChartView)                 │
│                        │    • Energy       (gauge labels + optional    │
│                        │                   per-member pie chart)       │
│                        │    • Energy History (bar chart + resolution    │
│                        │                     combo box)                 │
├────────────────────────┴────────────────────────────────────────────────┤
│  StatusBar:  m_statusLabel  (connection / error messages)               │
└─────────────────────────────────────────────────────────────────────────┘
```

The left/right panels are separated by a `QSplitter` (horizontal).  
The left panel (`leftPanel`) contains the `QTreeView` and a "Refresh interval" `QSpinBox` row below it.  
Default sizes: left 340 px, right 760 px.

---

## Qt Object Hierarchy (runtime parent tree)

```
MainWindow  (QMainWindow)
├── QSplitter  (m_splitter, central widget child)
│   ├── QWidget  (leftPanel)                      — left panel
│   │   ├── QTreeView  (m_deviceTree)
│   │   └── QHBoxLayout  (intervalRow)
│   │       ├── QLabel  "Refresh interval:"
│   │       └── QSpinBox  (m_intervalSpin, 2–300 s)
│   └── QWidget  (rightPanel)
│       ├── QLabel  (m_deviceIconLabel)    — 32×32 device heading icon
│       ├── QLabel  (m_deviceNameLabel)    — device name heading
│       ├── QStackedWidget  (m_controlStack)
│       │   ├── [0] QLabel  "Select a device…"
│       │   ├── [1] QScrollArea → SwitchWidget
│       │   ├── [2] QScrollArea → ThermostatWidget
│       │   ├── [3] QScrollArea → EnergyWidget
│       │   ├── [4] QScrollArea → DimmerWidget
│       │   ├── [5] QScrollArea → BlindWidget
│       │   ├── [6] QScrollArea → ColorWidget
│       │   ├── [7] QScrollArea → HumiditySensorWidget
│       │   └── [8] QScrollArea → AlarmWidget
│       └── ChartWidget  (m_chartWidget)
│           ├── QTabWidget  (m_tabs)
│           │   ├── Tab "Temperature"  → QChartView
│           │   ├── Tab "Power"        → QChartView
│           │   ├── Tab "Humidity"     → QChartView
│           │   ├── Tab "Energy"       → QWidget (outer panel, grey background)
│           │   │                         └── QWidget (inner, white background)
│           │   │                              ├── QLabel (gauge labels: kWh, W, V)
│           │   │                              └── QChartView (m_groupEnergyPieView, optional pie chart)
│           │   └── Tab "Energy History" (index m_energyHistoryTabIndex)
│           │       ├── QComboBox  (m_energyResCombo)
│           │       └── QChartView
│           └── QComboBox / QLabel  (m_windowCombo / m_windowComboTemp + caption)
│               — direct children of the chartStack widget inside each Temperature/Power
│               chart tab, positioned absolutely via a ResizeFilter event filter
├── QMenuBar
│   └── QMenu  "&File"
│       ├── QAction  "Connect…"
│       ├── QAction  "Refresh"
│       └── QAction  "Quit"
├── QStatusBar
│   └── QLabel  (m_statusLabel)
├── FritzApi  (m_api)                      — QObject, not a widget
└── DeviceModel  (m_model)                 — QAbstractItemModel, not a widget
```

---

## DeviceModel — Tree Structure

`DeviceModel` is a two-level `QAbstractItemModel`.

```
(invisible root)
├── Group header row  [row=0, internalId=0xFFFFFFFF]  "Smart Plugs  (3)"
│   ├── Device leaf   [row=0, internalId=0]           FritzDevice "Plug A"
│   ├── Device leaf   [row=1, internalId=0]           FritzDevice "Plug B"
│   └── Device leaf   [row=2, internalId=0]           FritzDevice "Plug C"
├── Group header row  [row=1, internalId=0xFFFFFFFF]  "Thermostats  (2)"
│   ├── Device leaf   [row=0, internalId=1]           FritzDevice "Living Room"
│   └── Device leaf   [row=1, internalId=1]           FritzDevice "Bedroom"
└── ...
```

**`internalId` encoding:**
- Group header: `internalId == 0xFFFFFFFF` (sentinel `kGroupSentinel`)
- Device leaf: `internalId == group_index`

**Columns** (same for group headers and leaves; non-name columns empty for headers):

| Index | Name        | Notes                                  |
|-------|-------------|----------------------------------------|
| 0     | Name        | Icon (DecorationRole) + text           |
| 1     | Type        | Bucket label, e.g. `"Smart Plugs"`     |
| 2     | Status      | On/Off/°C target/ALARM/Offline/…       |
| 3     | Temperature | `"21.5 °C"` or `"-"`                  |
| 4     | Power       | `"12.3 W"` or `"-"`                   |
| 5     | Availability| `"Online"` / `"Offline"`               |

**Group bucket priority** (first matching rule wins per device):

1. Groups (Fritz!Box device groups)
2. Color Bulbs
3. Dimmers
4. Smart Plugs (Switch + EnergyMeter)
5. Switches
6. Thermostats
7. Blinds
8. Alarms
9. Humidity Sensors
10. Sensors (catch-all)

**Special roles:**
- `Qt::UserRole` on a group header → raw label string (used for expansion-state restore)
- `Qt::FontRole` on group header → bold font
- `Qt::ForegroundRole` on group header → `Qt::darkGray`
- `Qt::ItemFlags` on group header → `ItemIsEnabled` only (not selectable)

---

## FritzApi — Network / Authentication Flow

```
login()
  │
  ├─► GET /login_sid.lua?version=2
  │       └─► onLoginChallengeReply()
  │               ├── parse challenge type ("pbkdf2" or "md5")
  │               ├── computeResponse() → computePbkdf2Response() / computeMd5Response()
  │               └─► GET /login_sid.lua?version=2&username=…&response=…
  │                       └─► onLoginResponseReply()
  │                               ├── SID valid  → m_reloginAttempts=0, emit loginSuccess()
  │                               └── SID "000…" → emit loginFailed()
  │
startPolling(intervalMs)
  └─► QTimer fires onPollTimer() every interval
          └─► fetchDeviceList()  [backward-compat overload, only if isLoggedIn()]
                  └─► fetchDeviceList(successCb, errorCb)  [async + cache + dedup]
                          ├── cache hit   → successCb(cachedDevices) immediately
                          ├── in-flight   → queue callback, share pending request
                          └── cache miss  → GET /api/v0/smarthome/overview
                                  └─► onAsyncReply(cacheKey, false, "")
                                          ├── HTTP 401 → handleSessionExpiry()
                                          ├── cache response data + timestamp
                                          ├── parseDeviceListJson() → FritzDeviceList
                                          ├── carry history forward from previous poll
                                          └── invoke all queued callbacks

fetchDeviceStats(ain)  [backward-compat overload]
  └─► fetchDeviceStats(ain, successCb, errorCb)  [async + cache + dedup]
          ├── cache hit   → successCb(cachedStats) immediately
          ├── in-flight   → queue callback, share pending request
          └── cache miss  → GET /api/v0/smarthome/overview/units/{unitUID}
                  └─► onAsyncReply(cacheKey, true, ain)
                          ├── HTTP 401 → handleSessionExpiry()
                          ├── parseUnitStatsJson() → DeviceBasicStats
                          ├── stats.valid → cache response data + timestamp,
                          │                 update device's basicStats
                          ├── !stats.valid → remove cache entry (no caching)
                          └── invoke all queued callbacks

setSwitchOn/Off/Toggle, setThermostatTarget, setLevel, setColor, setBlind, …
  └─► putUnitInterfaces() → PUT /api/v0/smarthome/overview/units/{unitUID}
          └─► [lambda reply handler]           [JSON body: {"interfaces": {…}}]
                  ├── HTTP 401 → handleSessionExpiry()
                  ├── success  → emit commandSuccess(ain, cmd)
                  │               clearAllCaches()
                  │               QTimer::singleShot(500) → fetchDeviceList()
                  └── failure  → emit commandFailed(ain, error)
```

### Response caching and request deduplication

`FritzApi` implements a TTL-based response cache with request deduplication to minimise
network traffic to the Fritz!Box:

**Cache structure:**  `QMap<QString, CacheEntry> m_cache` where each `CacheEntry` holds:
- `data` — raw JSON response bytes
- `timestamp` — when the response was received
- `ttlSeconds` — validity duration (2 s for device list, 5 s for device stats)
- `pendingReply` — non-null pointer to in-flight `QNetworkReply` (dedup sentinel)
- `deviceListCallbacks` / `deviceStatsCallbacks` — queued callback pairs

**Request flow (e.g. `fetchDeviceList(onSuccess, onError)`):**

1. **Cache hit:** if `isCacheValid(entry)` returns true (data non-empty, age < TTL),
   the cached JSON is re-parsed and `onSuccess` is invoked synchronously.
2. **In-flight dedup:** if `entry.pendingReply != nullptr`, the request is already
   in progress — `{onSuccess, onError}` is appended to the callback queue and no new
   network request is started.
3. **Cache miss:** a new `GET` is issued, the `CacheEntry` is initialised with
   `pendingReply` set, and the callback pair is queued. When the reply finishes,
   `onAsyncReply()` stores the response in the cache, invokes **all** queued callbacks,
   and clears the callback lists.

**Signal emission:** `onAsyncReply` does not emit any signals directly. All signal
emission (e.g. `deviceListUpdated`, `networkError`) is the responsibility of the
callbacks registered by callers. The backward-compatible `fetchDeviceList()` and
`fetchDeviceStats(ain)` overloads register callbacks that emit the legacy signals.
The `fetchDeviceStats(ain)` backward-compat overload emits `deviceStatsUpdated` when
`stats.valid` is true, or `deviceStatsError` when it is false (e.g. the Fritz!Box
returned an empty `statistics` object). This ensures the UI always receives either a
success or error signal — no silent drops.

**Cache invalidation:** `clearAllCaches()` / `invalidateAllCaches()` empties the entire
cache. This is called after every successful command (`putUnitInterfaces`) to ensure the
next poll picks up fresh state from the Fritz!Box. Additionally, device stats responses
that parse with `stats.valid == false` are never stored in the cache; the cache entry is
removed so the next `fetchDeviceStats` call for that AIN retries from the network.

### Session expiry and automatic re-login

The Fritz!Box silently expires sessions after a period of inactivity (typically 20 minutes).
When this happens, any REST API request returns **HTTP 401 Unauthorized**.

`handleSessionExpiry()` is called whenever a 401 response is detected:

1. Resets `m_sid` to the null sentinel so `isLoggedIn()` returns false.
2. Emits `sessionExpired()` — MainWindow shows "reconnecting…" in the status bar, **without opening the login dialog**.
3. Calls `login()` to start a new authentication cycle with the stored credentials.
4. If login succeeds, `loginSuccess()` fires as normal, polling resumes, and the device list is fetched immediately.
5. If login fails (wrong credentials, network error), `loginFailed()` fires as normal and the login dialog is shown.

A retry counter (`m_reloginAttempts`, reset to 0 on each successful login) limits automatic re-login attempts to `kMaxReloginAttempts` (= 3) to prevent infinite loops when credentials have permanently changed.


### TLS / SSL error flow

When `FritzApi::m_ignoreSsl == false` (default), each `QNetworkReply` created in
`get()` has a `sslErrors` lambda connected before it is started:

```
QNetworkReply::sslErrors(errors)    [emitted before finished() when TLS fails]
  └─► lambda in FritzApi::get()
          ├── collects QSslError::errorString() for each error → details string
          ├── sets reply property "sslErrorReported" = true
          └── emit FritzApi::sslError(details)

FritzApi::sslError(details)  →  MainWindow::onSslError(details)
  └── shows KMessageBox::error (HAVE_KF=1) / QMessageBox::critical (HAVE_KF=0)
       with: host, certificate error details, hint to enable
       "Ignore TLS certificate warnings" in the login dialog
  └── calls showLoginDialog()  — so user can immediately tick the checkbox

FritzApi::onLoginChallengeReply / onLoginResponseReply
  └── if reply->property("sslErrorReported").toBool()
          skip loginFailed emission  (SSL error already reported via sslError signal)
```

When `m_ignoreSsl == true`, `QNetworkAccessManager::ignoreSslErrors()` is called on
the reply in `get()` and the `sslErrors` lambda is **not** connected, so all TLS
certificate errors are silently ignored.

---

## MainWindow — Signal / Slot Wiring

```
FritzApi::loginSuccess          → MainWindow::onLoginSuccess
FritzApi::loginFailed           → MainWindow::onLoginFailed
FritzApi::sslError(details)     → MainWindow::onSslError(details)
FritzApi::sessionExpired()      → MainWindow::onSessionExpired  (silent reconnect status)
FritzApi::deviceListUpdated     → MainWindow::onDeviceListUpdated
FritzApi::deviceStatsUpdated    → [lambda] → ChartWidget::updateEnergyStats (single device)
                                  or accumulate into m_groupMemberStats + call
                                  ChartWidget::updateGroupEnergyStats (group device)
FritzApi::deviceStatsError      → [lambda] → ChartWidget::updateEnergyStatsError (single device)
                                  or ChartWidget::updateGroupEnergyStatsError (group member)
FritzApi::networkError          → MainWindow::onNetworkError
FritzApi::commandSuccess        → MainWindow::onCommandSuccess  (triggers refresh)
FritzApi::commandFailed         → MainWindow::onCommandFailed

QItemSelectionModel::currentChanged
    (m_deviceTree->selectionModel())  → MainWindow::onDeviceSelected

QSpinBox::valueChanged
    (m_intervalSpin)  → [lambda] → update m_pollingInterval, persist to QSettings,
                                    call FritzApi::startPolling() if logged in

DeviceWidget buttons / sliders → FritzApi::setSwitch*, setThermostat*, setLevel*, …
    (wired inside each DeviceWidget constructor via m_api)
```

---

## MainWindow — Key Slots

### `onDeviceListUpdated(devices)`

1. Saves `m_selectedAin` before model reset clears it.
2. Snapshots expanded group labels (`Qt::UserRole` via `QSet<QString>`) from the tree view.
3. Calls `m_model->updateDevices(devices)` — triggers `beginResetModel`/`endResetModel`.
4. On first load: restores saved header state or auto-sizes columns; sets `m_initialColumnSizeDone`.
5. Restores group expansion: expands all on first load, or re-expands only previously expanded groups.
6. Iterates the new tree model to find the device matching `previousAin`, restores selection and updates the right panel in-place (avoiding flicker).
7. Throttles `fetchDeviceStats` to at most once per 5 minutes for energy-meter devices.

### `onDeviceSelected(current, previous)`

1. Returns early (clears right panel) if `!current.isValid()` or `m_model->isGroupHeader(current)`.
2. Calls `m_model->deviceAt(current)` to retrieve the `FritzDevice`.
3. Updates heading icon (`m_deviceIconLabel`) and name (`m_deviceNameLabel`).
4. Calls `updateDevicePanel(device)` to switch `m_controlStack` to the correct panel and push data into it.
5. Calls `m_chartWidget->updateDevice(device)` to rebuild all chart tabs.
6. Triggers `fetchDeviceStats` immediately for energy-meter devices.

### `updateDevicePanel(device)`

Determines panel index by device capability priority (same order as group buckets), switches `m_controlStack`, then calls `DeviceWidget::updateDevice(device)` on the newly shown panel.

---

## ChartWidget — Tab Structure

| Tab             | Series type          | Data source                             | Time-window combo |
|-----------------|---------------------|-----------------------------------------|-------------------|
| Temperature (single device) | QLineSeries | `dev.temperatureHistory` (poll-rolling) | Yes |
| Temperature (group) | QLineSeries × N members | `member.temperatureHistory` per temp-capable member (poll-rolling) | Yes |
| Power           | QAreaSeries (single) or N×QAreaSeries layers (group ≥2 energy members) + QLineSeries net overlay (group only) | `dev.powerHistory` / `member.powerHistory` (poll-rolling) | Yes |
| Humidity        | QLineSeries          | `dev.humidityHistory` (poll-rolling)    | No          |
| Energy          | Static labels + optional QPieSeries (groups only) | `dev.energyStats` (live); per-member `energyStats.energy` for pie | No          |
| Energy History  | QBarSeries (single) or QStackedBarSeries (group) + QGraphicsRectItem ghost bars + QGraphicsLineItem cap lines (group only) — rebuilt on resolution change | `DeviceBasicStats` per member from API | No          |

Additional UI: For group devices the Energy tab shows a per-group
"Member Energy Distribution" pie chart (`QPieSeries` in a `QChartView`) summarising
each member's share (Wh or kWh, auto-scaled).  Slice labels always show both
absolute and percentage values; an overlap-aware visibility system
(`updatePieSliceLabels()`) hides labels of small neighbouring slices (angular
proximity < 22°, keeping the larger slice's label).  When a slice is hovered it
explodes outward (distance factor 0.10, label arm factor 0.05) and only that
slice's label is shown; on mouse leave all non-overlapping labels are restored.
Per-slice hover tooltips are implemented via a `PieTooltipFilter` event filter on
the chart viewport, using angular hit-testing against the pie geometry — this avoids
`QPieSlice::setToolTip` which is not available across all Qt versions.  The chart
uses `QChart::ChartThemeQt` (white background) with 10 px margins; the chart view
has a minimum height of 280 px.  The Energy tab uses a two-layer widget structure:
an outer panel with the default system background (so the `QTabWidget` grey frame
stays visible) containing an inner widget with a white `QPalette::Window` background
where labels and chart reside — matching the visual style of the other chart tabs.

The time-window combo box (overlaid top-left on Temperature and Power chart tabs) controls
how much rolling history to show. It has **9 steps**: 5 min, 15 min, 30 min, 1 h, 2 h, 4 h,
8 h, 16 h, 24 h (indices 0–8 into `kWindowMs[]`/`kWindowLabels[]`). Two combo box instances
(`m_windowCombo` for Power, `m_windowComboTemp` for Temperature) are kept in sync via
`blockSignals` cross-connect lambdas. The current index is persisted to `QSettings` under
`ui/chartSlider`.

Both members are declared as `QPointer<QComboBox>` (not raw pointers). Each chart-tab's
`QStackedWidget` (`chartStack`) takes ownership of the combo via `setParent(chartStack)`.
When the tab is destroyed on a device switch, Qt automatically zeroes both `QPointer`s.
The rescue guard at the top of each builder (`buildTemperatureChart`,
`buildGroupTemperatureChart`, `buildPowerChart`) checks the `QPointer`: if still valid it
reparents the combo back to `this` (so it survives the tab teardown); if null it
recreates the combo with items and signal wiring from scratch.

### Energy History bar chart details

- Bars are oldest-left, newest-right; the rightmost bar represents the still-accumulating
  current period and is rendered in a half-opacity tint (Qt6 only, via `QBarSet::selectBar()`).
- Hover tooltips are implemented via an `eventFilter` on `chartView->viewport()` that
  responds to `QEvent::ToolTip` (`QHelpEvent`), matching the behaviour of `QAbstractItemView`.
  `QBarSet::hovered` stores the current tooltip text in `m_energyBarTooltip`; the event
  filter calls `QToolTip::showText()` from there.  When moving between bars (text changes
  while tooltip is visible), `showText(pos, "")` is called first to force the old popup
  to close before the new one is created — required on Qt5 where the popup is reused
  in-place and geometry becomes corrupted when text width changes.
  **`comboOverlay` is also registered with the same event filter** (`comboOverlay->installEventFilter(this)`)
  because it has `WA_TransparentForMouseEvents=false` and sits above `chartView` in the
  `QStackedLayout::StackAll` stack, intercepting mouse events (including `QHelpEvent`)
  before they reach `chartView->viewport()`.  Without this second registration, hovering
  over most of the chart area produces no tooltip.
  **Mouse-event forwarding:** The overlay also blocks `QEvent::MouseMove` from reaching the
  chart viewport, which prevents Qt Charts from firing `QBarSet::hovered` signals (these
  populate `m_energyBarTooltip`).  To fix this, `comboOverlay` has `setMouseTracking(true)`
  (so it receives `MouseMove` even without a button press), and the `eventFilter` forwards
  every `MouseMove` event from the overlay to the chart viewport via
  `QCoreApplication::sendEvent()`, mapping the position from overlay to viewport coordinate
  space.  The `m_energyChartView` member (set by both `buildEnergyHistoryChart` and
  `buildEnergyHistoryChartStacked`, cleared on teardown) stores the target viewport.
  The forwarded event is not consumed (`return false`), so combo-box hover effects and
  cursor shapes continue to work normally on the overlay.
- **Layout:** the view selector (`QComboBox`) and the window-total energy value (`QLabel`)
  are overlaid directly on top of the `QChartView` using `QStackedLayout::StackAll`,
  so the chart fills the entire tab area. The combo is anchored top-left (transparent
  background, `WA_TransparentForMouseEvents=false` so it stays interactive); the total
  label is anchored top-right (`WA_TransparentForMouseEvents=true`, receives no mouse
  events).
- **Y-axis units:** the "Last 24 hours" (grid=900) view always uses Wh to prevent the
  Y-axis labels collapsing to `"0.0 kWh"` when per-slot values are small (< 1 kWh).
  Daily and monthly views auto-switch to kWh when the window total ≥ 1000 Wh.
- **All-zero / all-NaN fallback:** when every bar value in the selected view is zero
  (which includes the case where all original API values were NaN — e.g. device was
  unreachable for the entire period), both `buildEnergyHistoryChart` and
  `buildEnergyHistoryChartStacked` fall back to `buildEnergyHistoryPlaceholder` instead
  of rendering invisible zero-height bars. The placeholder shows a "No energy data
  available for {view} yet" message with the view selector combo, matching the existing
  placeholder style for views with no series data.
- **Animations:** disabled for all energy history views (`QChart::NoAnimation`, the default
  from `makeBaseChart`). The "grow from zero" effect on bar rebuilds is distracting.

### Producer / consumer device classification

Individual energy-capable devices can be marked as **power producers** (e.g. solar panels)
via the "Power producer" checkbox in `SwitchWidget` (top-right of the control panel) or
`EnergyWidget`. The flag is stored as `FritzDevice::isProducer` and persisted to `QSettings`
under `devices/<ain>/isProducer`. Groups never have a producer flag — each member carries its own.

**Effect on charts and views:**

- **Tree view** (`DeviceModel::ColPower`): power value is negated for producer devices; tooltip reflects the negated sign.
- **Rolling power chart** (single device): values negated; area extends below zero.
- **Rolling power chart** (group, stacked): consumer bands stack upward from zero; producer bands stack downward from zero independently, so they never cross. A black `QLineSeries` ("Net") is drawn on top of all bands **only when members have mixed producer/consumer roles** — showing the signed sum per timestamp (consumers positive, producers negative), updated on every rolling poll.
- **Energy history chart** (single device): bar values negated; Y-axis range flipped to `[−maxVal, 0]`; total label shows the absolute value prefixed with "−" (e.g. `−1166.0 Wh`) — `qAbs(rawTotal)` is formatted first so the sign is never doubled.
- **Energy history chart** (group, stacked): per-member values negated for producers; `minStack`/`maxStack` track the negative/positive extremes for the Y-axis range. A **net overlay** and legend entry are shown **only when members have mixed producer/consumer roles** (`hasMixedProducers = hasProducer && hasConsumer`):
  - `QGraphicsRectItem` (zValue 9, behind stacked bars at ~10): semi-transparent black fill from zero to the net value, matching bar width exactly (`halfBar = slotWidth × 0.4`).
  - `QGraphicsLineItem` (zValue 12, above everything): 2 px black cap line at the net value, width `halfBar − 1 px` to stay strictly inside the bar edges at any zoom level.
  - An empty `QLineSeries` named "Net" (black 2 px pen, no data points) is added to the chart and attached to its axes solely to register a **legend marker** in the Qt Charts built-in legend. The ghost-bar overlay provides the actual visual; the series renders nothing.
- **Energy gauge** (single device): power label is negated (negative = produced); kWh label shows the **absolute** value (positive number) under the heading "Total Energy Produced" / "Total Energy Consumed" — the heading already communicates direction. For groups: net signed energy/power computed from members.
- **Pie chart** (group energy tab): `QPieSeries` requires positive values — absolute magnitude is used for slice sizing; the signed value is stored as a `QVariant` property `"signedEnergy"` on each slice and used for label rendering so negative (producer) shares display correctly.

**Immediate chart refresh on toggle:** `MainWindow::setDeviceProducerStatus` calls
`ChartWidget::updateForDeviceProducerStatusChange` which rebuilds the Energy gauge tab
and Energy History tab in-place (without a full device switch) so the user sees the
effect immediately without waiting for the next poll.
- **No-op refresh guard:** `updateEnergyStats` and `updateGroupEnergyStats` compare the
  incoming `StatSeries::values` list for the currently displayed grid against the cached
  copy. If the values are identical the expensive remove-rebuild cycle is skipped; only
  the cache is updated (so `fetchTime` etc. stay current). This applies to all three
  resolutions (900 / 86400 / 2678400).
- **Refresh throttle:** `MainWindow::onDeviceListUpdated` uses a 60-second throttle for
  `fetchDeviceStats` when `ChartWidget::activeEnergyGrid() == 900` (the 15-min view),
  a 5-minute throttle for daily/monthly views, and a 30-second throttle when
  `activeEnergyGrid() == 0` (placeholder state — no chart built yet), via the public
  `activeEnergyGrid()` accessor on `ChartWidget`. The shorter placeholder throttle
  ensures the chart populates quickly after the first stats fetch, even if the initial
  response was invalid.
- **"Last 24 hours" hourly view (index 0):** uses the `grid=900` (15-minute) energy series.
  Available via the REST API (`GET /smarthome/overview/units/{unitUID}`, `statistics.energies[]`
  with `interval=900`).  Displays up to 96 bars (96 × 900 s = 24 h), oldest-left /
  newest-right, with `dd.MM.yy HH:mm` tooltip timestamps.
  X-axis labels: every bar whose `slotDt.time().minute() == 0` (exact `:00` hour boundary)
  gets a plain hour label (e.g. `"9"`, `"22"`); additionally the rightmost bar (`bar == 0`,
  i.e. the most-recently-completed 15-minute slot) always gets a label showing the current
  hour, ensuring the newest hour is always visible regardless of where `newestBoundary`
  falls within the hour.  Non-labelled bars use unique space strings so
  `QBarCategoryAxis` does not drop duplicates.  Labels are rotated −90° and use a
  reduced font size to prevent truncation at 96-bar density.
  Thin vertical `QGraphicsLineItem` separator lines are drawn at each hour boundary;
  their x-positions are computed via `chart->mapToPosition(QPointF(idx, 0), barSeries)`
  minus half a slot width, and are repositioned on every `plotAreaChanged` signal.
  The default per-category `QBarCategoryAxis` grid lines are disabled via
  `setGridLineVisible(false)` to avoid visual clutter.
- **"Rolling month" daily view (index 1):** the daily resolution shows a window from
  *(previous month's same day-of-month + 1)* through *today*, which equals exactly
  `daysInPreviousMonth` data bars (28–31 depending on the previous month).
  Example: today = 22 Mar → window is 23 Feb … 22 Mar = 28 bars.
  Category strings use `slotDate.toString("d MMM")` format (e.g. `"23 Feb"`, `"1 Mar"`)
  rather than plain day numbers, because `QBarCategoryAxis` silently deduplicates
  identical strings and day numbers repeat across the month boundary (e.g. two "1"s).
  The built-in axis labels are hidden and replaced by the same two-row
  `QGraphicsTextItem` overlay system used by the "Last 2 years" view:
  - **Row 1 (day numbers):** one label per bar showing the day-of-month (e.g. `"23"`,
    `"24"`, …, `"1"`, `"2"`), centered on its bar's x-position.
  - **Row 2 (month names):** one bold abbreviated month name per contiguous span of bars
    belonging to the same calendar month (e.g. `"Feb"` spanning days 23–28, `"Mar"`
    spanning days 1–22), centered horizontally over the span.  Month names are
    produced by the static `monthAbbr(int month)` helper which uses `i18n()`,
    so they follow the application language (not `QLocale::system()`).
  Both rows reposition themselves on every `QChart::plotAreaChanged` signal.  The chart's
  bottom margin and vertical positioning logic are shared with the monthly view (see
  "Last 2 years" above).  There is no separator bar between months — the two-row overlay
  makes the month boundary visually clear without wasting a bar slot.
  `lastBar` (the still-accumulating bar highlighted via `QBarSet::selectBar`) is always
  `categories.size() - 1`.
- **"Last 2 years" monthly view (index 2):** 24 bars, one per calendar month, oldest-left /
  newest-right.  Category strings use locale-formatted `"MMM yy"` format (e.g. `"Jan 24"`)
  rather than `"MMM"` alone, because `QBarCategoryAxis` silently deduplicates identical
  strings and the same month abbreviation repeats across the two-year span.
  Month assignment uses calendar arithmetic (`QDate::addMonths(-bar)` from the newest
  month, derived from `fetchTime.date()`) rather than subtracting `grid * bar` seconds,
  because the fixed grid value of 2678400 s (= 31 days) does not match actual month
  lengths and the drift accumulates over 24 bars.  The newest month is taken from the
  fetch timestamp's calendar date (not from `newestBoundary`, which is snapped to a
  31-day grid and can fall in the previous month).  The built-in axis labels
  are hidden and replaced by two rows of `QGraphicsTextItem` overlays drawn in the chart
  scene:
  - **Row 1 (month names):** one abbreviated month label per bar (produced by the
    static `monthAbbr()` helper via `i18n()`, following the application language),
    centered on its bar's x-position.
  - **Row 2 (year numbers):** one bold `"yyyy"` label per distinct calendar year,
    centered horizontally over the full span of bars belonging to that year.
  Both rows reposition themselves on every `QChart::plotAreaChanged` signal.
  The chart's bottom margin is increased to 36 px via `QChart::setMargins()` to give the
  two overlay rows enough room.  Vertical positions are computed as:
  `gap = (36 − kAxisOverhang − monthH − yearH) / 3`, where `kAxisOverhang = 6 px`
  accounts for the tick marks that extend below `plotArea().bottom()`.  This makes the
  three gaps (axis→month, month→year, year→frame) equal regardless of font metrics.

### Energy History error display

When `FritzApi::fetchDeviceStats()` fails (network error, timeout, etc.), the
`deviceStatsError(ain, error)` signal is emitted alongside `networkError`. MainWindow
routes this to ChartWidget based on the current selection:

- **Single device:** if `ain == m_selectedAin`, calls
  `ChartWidget::updateEnergyStatsError(error)`.
- **Group member:** if `ain` is in `m_groupMemberStats` (outstanding group fetch),
  resolves the member name and calls
  `ChartWidget::updateGroupEnergyStatsError(memberName, error)`.

Both methods replace the Energy History tab content with a formatted error display
widget wrapped in a grey-framed container (matching the visual style of chart tabs).
The error display is built by two file-static helpers in `chartwidget.cpp`:

- **`wrapInFramedContainer(innerWidget)`** — creates the two-layer panel structure
  (outer widget with default margins for the grey frame, inner widget with white
  `QPalette::Window` background) used by both the Energy gauge tab and error displays.
- **`createErrorDisplayWidget(caption, errors)`** — builds a formatted error widget
  with a `QIcon::fromTheme("dialog-error")` icon, bold caption label (font size +1),
  and an HTML `<ul>` bullet list of error messages. Margins are 16 px, spacing 12 px.

For groups, `updateGroupEnergyStatsError` accumulates errors in `m_groupEnergyErrors`
(deduplicated), so when multiple members fail, all error messages appear as separate
bullet items.

Error state is cleared in three situations:
1. **Device switch** — `updateDevice()` clears both `m_energyError` and
   `m_groupEnergyErrors`.
2. **Successful single-device data** — `updateEnergyStats()` clears `m_energyError`.
3. **Successful group data** — `updateGroupEnergyStats()` clears `m_groupEnergyErrors`.

When updating an existing error display (e.g. a second group member fails), the methods
detect the framed container structure, remove and delete the old error widget from the
inner layout, and insert a freshly built one — avoiding a full tab teardown/rebuild.

### Fritz!Box `statisticsState` propagation

The Fritz!Box REST API includes a `statisticsState` field in each statistics array
entry (energies, powers, temperatures, voltages).  Known values:

| State          | Meaning                                                |
|----------------|--------------------------------------------------------|
| `"valid"`      | Data is available (`interval`, `period`, `values` present) |
| `"unknown"`    | Device statistics not yet available (recently added/reset) |
| `"notConnected"` | Device is offline / unreachable                       |

When all energy series entries have a non-`"valid"` state and no usable data,
`parseUnitStatsJson` captures the first non-`"valid"` state in
`DeviceBasicStats::energyStatsState`.  This field propagates through the success
signal path (`deviceStatsUpdated` → `updateEnergyStats` → `buildEnergyHistoryChart`)
because the stats are technically valid (the JSON parsed correctly) — they just lack
energy data.

`buildEnergyHistoryChart` and `buildEnergyHistoryChartStacked` use
`energyStatsState` in their all-series-null early returns to display a context-aware
message:

- `"notConnected"` → "Device is not connected — statistics are unavailable."
- `"unknown"` → "Statistics for this device are not yet available."
- Other states → raw state value shown as fallback
- Empty (no statistics object / no energies array) → generic "No energy history available"

For group charts, the stacked builder collects `energyStatsState` from each member
and lists the per-member reasons (e.g. "Wohnzimmer 1: device not connected").

---

## Stacked Energy History Bar Chart for Groups

When a Fritz!Box group device is selected, the Energy History tab shows a
**`QStackedBarSeries`** bar chart with one `QBarSet` per member device, stacked
vertically so the total bar height equals the sum of all member contributions.

### Data flow

1. **Device selection** (`MainWindow::onDeviceSelected`): if the selected device
   `isGroup()`, iterates `dev.memberAins`, looks up each member via
   `DeviceModel::deviceByAin()`, and calls `FritzApi::fetchDeviceStats(memberAin)`
   for every member with `hasEnergyMeter()`.

2. **Per-member reply routing** (`MainWindow::deviceStatsUpdated` signal handler):
   - If `m_groupStatsPending > 0` and the `ain` is present in `m_groupMemberStats`,
     the reply is stored in `m_groupMemberStats[ain]`.
   - `m_groupStatsPending` counts outstanding member requests; when it reaches 0 (all
     members replied), the `memberStats` list is constructed with **human-readable device
     names** (resolved via `m_model->deviceByAin(memberAin).name`, falling back to the
     raw AIN if the name is empty) and passed to
     `ChartWidget::updateGroupEnergyStats(memberStats)`.

3. **Rendering** (`ChartWidget::updateGroupEnergyStats`):
   - Sets `m_groupHistoryMode = true` and caches the member stats in `m_lastGroupMemberStats`.
   - Removes the old Energy History tab widget and inserts a freshly built one by
     calling `buildEnergyHistoryChartStacked(memberStats)`.

4. **Resolution combo** (`ChartWidget::onEnergyResolutionChanged`): dispatches to
   `buildEnergyHistoryChartStacked` when `m_groupHistoryMode` is true, otherwise to
   the single-device `buildEnergyHistoryChart`.

5. **Poll-tick refresh** (`MainWindow::onDeviceListUpdated`): the same group-member
   `fetchDeviceStats` pattern is repeated (throttled to once per 5 minutes) so the
   stacked chart stays up to date during long sessions.

### ChartWidget state for group energy history

| Member | Type | Purpose |
|---|---|---|
| `m_groupHistoryMode` | `bool` | True when the last-shown energy history was a group chart |
| `m_lastGroupMemberStats` | `QList<QPair<QString,DeviceBasicStats>>` | Cached per-member stats, used for resolution changes |
| `m_memberDevices` | `FritzDeviceList` | Member device objects (name lookup for bar labels) |

### ChartWidget state for energy history error display

| Member | Type | Purpose |
|---|---|---|
| `m_energyError` | `QString` | Current error message for single-device energy stats; cleared on device switch or successful data arrival |
| `m_groupEnergyErrors` | `QStringList` | Accumulated error messages for group members; cleared on device switch or successful data arrival |

### ChartWidget state for energy history skip-rebuild guard

| Member | Type | Purpose |
|---|---|---|
| `m_activeEnergyGrid` | `int` | Grid of the currently displayed energy history view (900 / 86400 / 2678400); 0 = no chart built (placeholder or no-data state) |
| `m_lastEnergyStats` | `DeviceBasicStats` | Cached stats from the last successful single-device chart build; used for same-data comparison |
| `m_lastAvailableGrids` | `int` | Bitmask of available grids in the last build (bit 0 = 900, bit 1 = 86400, bit 2 = 2678400); 0 = none |

`updateEnergyStats` and `updateGroupEnergyStats` skip the expensive chart
teardown-and-rebuild when `m_activeEnergyGrid > 0` and the incoming data for that
grid matches the cached copy.  This guard relies on `m_activeEnergyGrid` being reset
to 0 whenever the chart enters a non-chart state, so that the next stats arrival
always triggers a full rebuild.

**Reset points for `m_activeEnergyGrid`:**

1. **`updateDevice`** — set to 0 (along with `m_lastAvailableGrids` and
   `m_lastEnergyStats`) during the tab-clear/rebuild cycle.  Prevents stale values
   from a previous device interfering with the skip guard on the new device.
2. **`buildEnergyHistoryPlaceholder`** — set to 0 so that the placeholder state
   is recognised as "no chart" by both the skip guard and the refresh throttle.
3. **`buildEnergyHistoryChart` all-series-null early return** — set to 0 before
   inserting the "No energy history" label, matching the placeholder semantics.
4. **`buildEnergyHistoryChartStacked` all-series-null early return** — same as above
   for the group chart path.
5. **`buildEnergyHistoryChart` / `buildEnergyHistoryChartStacked` success path** —
   set to the active view's grid value (900 / 86400 / 2678400) after the chart is
   fully built.

### ChartWidget state for group energy pie chart

| Member | Type | Purpose |
|---|---|---|
| `m_groupEnergyPie` | `QPieSeries *` | Pie series for member energy distribution; reused across rebuilds (cleared and repopulated) |
| `m_groupEnergyPieView` | `QChartView *` | Chart view hosting the pie chart; created once and inserted into the Energy tab layout |
| `m_groupPieTooltipFilter` | `PieTooltipFilter *` | Event filter on the chart viewport for hover tooltips and slice explode interaction |

### MainWindow state for group stats collection

| Member | Type | Purpose |
|---|---|---|
| `m_groupMemberStats` | `QMap<QString, DeviceBasicStats>` | Accumulates per-member stat replies (keyed by AIN) |
| `m_groupStatsPending` | `int` | Count of outstanding `fetchDeviceStats` calls; chart is built when this reaches 0 |

### Colour palette and tooltip format

`buildEnergyHistoryChartStacked` uses a built-in 8-colour palette (cycling with modulo)
and sets alpha=200 on each bar colour. The still-accumulating newest bar is highlighted
via `QBarSet::selectBar(categories.size() - 1)` (Qt6 only) for all three views (Last 24 h,
Rolling month, Last 2 years).  **Important:** `selectBar()` must be called *after* the
bar values have been appended to the `QBarSet`; calling it on an empty set silently
no-ops because the target bar index does not yet exist.  The two-row axis overlay (see single-device chart details
above) is also applied in the stacked builder for both the "Rolling month" (daily,
grid=86400) and "Last 2 years" (monthly, grid=2678400) views, using the same
`monthBarLabels` / `yearBarLabels` / `kAxisOverhang` logic.

Each `QBarSet` gets its own `hovered` lambda; the tooltip format is
`"%1\n%2: %3 %4"` (date, member name, value, unit) — one line per segment rather than
the single-device `"%1\n%2 %3"` (date, value, unit).  The same `comboOverlay` event
filter fix (see energy history bar chart details above) is applied here too.

### GCC 15 warning suppression

GCC 15's pessimistic alias/range analysis fires `-Wstringop-overflow` inside the
`QVector<double>(barsToShow, 0.0)` constructor calls because it carries a
potentially-negative range for the signed `int barsToShow` through Qt's template
machinery, even after `qMax(1, …)`. This is a confirmed false positive. It is
suppressed via a per-file `COMPILE_OPTIONS "-Wno-stringop-overflow"` applied only to
`src/chartwidget.cpp` in `CMakeLists.txt` (guarded by `CMAKE_CXX_COMPILER_ID STREQUAL "GNU"`
so it does not affect Clang builds).

---

## Settings Persistence (`QSettings`)

| Key                        | Content                                        |
|----------------------------|------------------------------------------------|
| `connection/host`          | Last used Fritz!Box hostname                   |
| `connection/username`      | Last used username                             |
| `connection/interval`      | Polling interval in seconds                    |
| `connection/autoLogin`     | `true` → skip login dialog on next startup     |
| `connection/ignoreSsl`     | `true` → ignore TLS certificate errors         |
| `ui/geometry`              | Window geometry (`saveGeometry()` bytes)       |
| `ui/windowState`           | Toolbar/dock state (`saveState()` bytes)       |
| `ui/splitterState`         | Left/right splitter position                   |
| `ui/headerState`           | QTreeView column widths / order                |
| `ui/chartSlider`           | Time-window combo index (0–8)                  |
| `ui/chartTab`              | Name of the last-active chart tab (restored on device switch) |
| `ui/energyResIdx`          | Energy History resolution combo index (0 = Last 24 h, 1 = Rolling month, 2 = Last 2 years) |

Password is never persisted.

---

## Cross-Compilation: tumbleweed-aarch64

The `tumbleweed-aarch64` target produces a native aarch64 ELF binary and `.aarch64.rpm`
package from an x86_64 host — no QEMU/binfmt required.

**Toolchain:** `cross-aarch64-gcc14` from the standard Tumbleweed OSS repo.
- Cross-compiler prefix: `aarch64-suse-linux-` (e.g. `/usr/bin/aarch64-suse-linux-g++`)
- Sysroot provided by `cross-aarch64-glibc-devel` (pulled in transitively) at
  `/usr/aarch64-suse-linux/sys-root/`

**Qt6 / KF6 aarch64 headers and stubs** are fetched from the openSUSE ports mirror
(`download.opensuse.org/ports/aarch64/tumbleweed/repo/oss/`) using `rpm2cpio | cpio`
and installed into the sysroot. The `qt6-qml-devel` and `kf6-kconfig-devel` packages
are installed as **host (x86_64) tools** so that CMake host-tool wrappers
(`qmlaotstats`, `kconfig_compiler_kf6`) point at x86_64 binaries.

**CMake toolchain file:** `cmake/toolchain-aarch64.cmake`
- Sets `CMAKE_SYSTEM_NAME=Linux`, `CMAKE_SYSTEM_PROCESSOR=aarch64`
- Sets `CMAKE_SYSROOT=/usr/aarch64-suse-linux/sys-root`
- Passes `-Wl,--allow-shlib-undefined` so the cross-linker does not error on transitive
  shared-library dependencies (e.g. `libdbus`, `libicu`, `libpng`) that are absent from
  the sysroot but present on the target system at runtime.
- Sets `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` to skip try_compile link steps
  that cannot produce an executable for aarch64 on an x86_64 host.
- Auto-detects `QT_HOST_PATH=/usr` so Qt's cmake configs find host moc/rcc/uic wrappers.

**RPM metadata:** `CPACK_RPM_PACKAGE_ARCHITECTURE` is set to `${CMAKE_SYSTEM_PROCESSOR}`
in `CMakeLists.txt` so the generated `.rpm` reports `aarch64` rather than the host arch.

---

## Build System

```
CMakeLists.txt
  USE_KF=0    → Qt5 only (Leap 15.6 standard repo)
  USE_KF=5    → Qt5 + KF5 (requires KDE:Frameworks5 OBS repo)
  USE_KF=6    → Qt6 + KF6 (Leap 16.0: KDE:Extra OBS repo; Tumbleweed: standard repo)
  USE_KF=qt6  → Qt6, no KDE Frameworks (Ubuntu 24.04 LTS)
  (empty)     → auto-detect: KF6 > KF5 > Qt5-only

Qt modules required: Core, Widgets, Network, Xml, Charts, DBus

Docker-based builds (recommended):
  ./docker/build.sh --distro opensuse-leap-15.6-x86_64   → out/opensuse/leap15.6/x86_64/fritzhome + *.rpm
  ./docker/build.sh --distro opensuse-leap-16.0-x86_64   → out/opensuse/leap16.0/x86_64/fritzhome + *.rpm
  ./docker/build.sh --distro opensuse-tumbleweed-x86_64  → out/opensuse/tumbleweed/x86_64/fritzhome + *.rpm
  ./docker/build.sh --distro opensuse-tumbleweed-aarch64 → out/opensuse/tumbleweed/aarch64/fritzhome + *.aarch64.rpm  (cross-compiled)
  ./docker/build.sh --distro ubuntu-24.04-x86_64         → out/ubuntu/24.04/amd64/fritzhome + *.deb
  ./docker/build.sh --distro all                         → all five

Dockerfiles:
  docker/Dockerfile.leap15.6            Qt5-only, libsecret, rpm-build
  docker/Dockerfile.leap16.0            Qt6 + KF6 via KDE:Extra OBS repo, rpm-build
  docker/Dockerfile.tumbleweed          Qt6 + KF6 from standard Tumbleweed repo, rpm-build
  docker/Dockerfile.tumbleweed-aarch64  Qt6 + KF6 cross-compiled for aarch64 (cross-aarch64-gcc14 + ports mirror sysroot)
  docker/Dockerfile.ubuntu24.04         Qt6-only, libsecret, dpkg-dev (DEB packaging)

RPM packaging (openSUSE targets):
  CPack generator: RPM
  CPack output:  build/opensuse/<distro>/<arch>/fritzhome-<version>-1.<arch>.rpm
  Final output:  out/opensuse/<distro>/<arch>/fritzhome-<version>-1.<arch>.rpm
  Filename convention: name-version-release.arch.rpm  (openSUSE/RPM standard)
  Installed files:
    /usr/bin/fritzhome
    /usr/share/applications/io.github.flunaras.fritzhome.desktop
    /usr/share/icons/hicolor/scalable/apps/fritzhome.svg
    /usr/share/metainfo/io.github.flunaras.fritzhome.metainfo.xml

DEB packaging (Ubuntu targets):
  CPack generator: DEB  (passed via -DCPACK_GENERATOR=DEB by build.sh)
  CPack output:  build/ubuntu/<distro>/amd64/fritzhome-<version>-1.Linux.deb  (CPack raw name)
  Final output:  out/ubuntu/<distro>/amd64/fritzhome_<version>-1_amd64.deb
  Filename convention: name_version-revision_arch.deb  (Debian policy)
  Same installed files as RPM targets.
```

`HAVE_KF` is a compile-time `#define` (0 or 1) used throughout the source to switch
between Qt-only and KDE-enhanced code paths (e.g. `QMainWindow` vs `KXmlGuiWindow`,
`QMessageBox` vs `KMessageBox`, `tr()` vs `KI18n`).

---

## Internationalisation (i18n)

All user-visible strings are wrapped in `i18n(...)` from `src/i18n_shim.h`.

### KF build (`HAVE_KF=1`)

- `i18n()` resolves to `KLocalizedString`'s `i18n()` directly.
- `KLocalizedString::setApplicationDomain("fritzhome")` is called in `main.cpp`
  before any `i18n()` call so KI18n knows which catalogue to load.
- `ki18n_install(po)` in `CMakeLists.txt` compiles `po/de/fritzhome.po` →
  `share/locale/de/LC_MESSAGES/fritzhome.mo` at install time.

### No-KF build (`HAVE_KF=0`)

- `i18n()` resolves to `QCoreApplication::translate("fritzhome", s)`.
- `translations/fritzhome_de.ts` is compiled to `fritzhome_de.qm` by
  `qt_add_translation` / `qt5_add_translation` in CMake.
- The `.qm` file is embedded as a Qt resource (`:/translations/fritzhome_de.qm`).
- `main.cpp` installs a `QTranslator` before any `i18n()` calls:
  it tries the exact system locale (`de_DE`) then the bare language code (`de`).

### Adding more languages

- **KF build:** add `po/<lang>/fritzhome.po` (same format as `po/de/`); `ki18n_install(po)`
  picks it up automatically.
- **No-KF build:** add `translations/fritzhome_<lang>.ts` and append it to `TS_FILES`
  in `CMakeLists.txt`; ensure the resource alias matches the loader in `main.cpp`.

### Menu / toolbar setup (KF path)

`MainWindow::setupActions()` registers three actions with `actionCollection()`:

| Name           | Action                             |
|----------------|------------------------------------|
| `file_connect` | Open login dialog (`actionConnect`) |
| `file_refresh` | Force poll (`actionRefresh`)        |
| `file_quit`    | `KStandardAction::quit` → `close()` |

`setupGUI(Keys | StatusBar | Save | Create)` is then called **without** a filename
argument, so `KXmlGuiWindow` discovers `fritzhomeui.rc` automatically via the KDE
data directory (`$KDE_INSTALL_KXMLGUIDIR/fritzhome/fritzhomeui.rc`).  The `.rc` file
defines the File menu order (Connect → Refresh → Separator → Quit) and contains
**no** `<ToolBar>` block, so no toolbar is created.

On the Qt5/plain-Qt path (`HAVE_KF=0`) the menu is built manually in C++ with the
same order; no `.rc` file is used.

---

## Fritz!Box Device Capability Bitmask

The `functionBitmask` field encodes device capabilities.  The bit positions originate
from the AHA HTTP Interface spec, but in practice the bitmask is **synthesized** from
the REST API's `interfaces` object — each interface name (e.g. `onOffInterface`,
`multimeterInterface`) is mapped to the corresponding bit:

| Bit  | Dec    | Capability          | Helper method       |
|------|--------|---------------------|---------------------|
| 4    | 16     | Alarm sensor        | `hasAlarm()`        |
| 6    | 64     | Blind / Thermostat  | `hasBlind()` / `hasThermostat()` |
| 7    | 128    | Energy meter        | `hasEnergyMeter()`  |
| 8    | 256    | Temperature sensor  | `hasTemperature()`  |
| 9    | 512    | Switchable outlet   | `hasSwitch()`       |
| 13   | 8192   | Dimmer              | `hasDimmer()`       |
| 14   | 16384  | Color bulb          | `hasColorBulb()`    |
| 18   | 262144 | Blind (Rollladen)   | `hasBlind()`        |
| 20   | 1048576| Humidity sensor     | `hasHumidity()`     |

Fritz!Box device groups are identified by the `group` boolean field (set during JSON
parsing of the `groups` array in the REST API response) rather than by a bitmask bit.
`isGroup()` returns `dev.group`.

---

## SwitchWidget — Button Design and Group Per-Member Control

`SwitchWidget` uses three `QToolButton` widgets (Turn On, Turn Off, Toggle), each
with `setPopupMode(QToolButton::MenuButtonPopup)`.  This gives every button a split
appearance:

- **Clicking the main area** — sends the command to the **group or device AIN**
  (acts on all members at once for groups, exactly like the old `QPushButton`).
- **Clicking the ▼ arrow** — opens a dropdown menu listing every switch-capable
  member device by name; clicking a member entry sends the command to **that member's
  AIN only**.  Members that are individually API-locked (`locked`) or **offline** (`present == false`)
  appear disabled in the menu.

For single (non-group) devices the menus remain empty and `QToolButton` renders
without the dropdown arrow.

`setMembers(const FritzDeviceList &)` is called by `MainWindow::updateDevicePanel()`
immediately after `updateDevice()` whenever a group switch panel is shown.  It stores
the filtered member list (`m_members`) and calls `rebuildMenus()` which discards any
existing menus and recreates them from the current member state.  An empty list clears
all menus (used when navigating to a single device).

Each menu entry carries a small 12×12 filled-circle icon rendered via `QPainter` on a
`QPixmap` that reflects the member's current on/off state: green = on, gray = off.
The icon is generated by the file-static helper `memberStateIcon(bool on)` and is
rebuilt on every `rebuildMenus()` call so the dropdown always shows fresh state.

## SwitchStats — Remote-Control Lock Flags and Group State

`SwitchStats` (in `fritzdevice.h`) carries lock flags and a group-state flag:

| Field                   | Source                  | Meaning |
|-------------------------|-------------------------|---------|
| `locked`                | JSON `isLockedDeviceApi`    | Remote control disabled via Fritz!Box web UI ("Software-Lock") — **prevents** API switching |
| `deviceLocked`          | JSON `isLockedDeviceLocal`  | Physical button on the device is disabled — does **not** prevent remote API control (informational only) |
| `mixedSwitchState`      | Synthesized (groups only)   | Controllable members have differing on/off states — status label shows PARTIAL |
| `hasLockedMembers`      | Synthesized (groups only)   | Group has ≥1 locked member alongside ≥1 controllable member — Toggle button uses InstantPopup |
| `allOn`                 | Synthesized (groups only)   | All controllable members are already on — Turn On button disabled |
| `allOff`                | Synthesized (groups only)   | All controllable members are already off — Turn Off button disabled |

**Physical devices:** `locked` and `deviceLocked` are populated directly from the
`onOffInterface` block of the REST API response (`GET /api/v0/smarthome/overview`).
`FritzApi::parseDeviceListJson()` assigns them at two sites: once for physical devices
(≈ line 482) and once for group member units (≈ line 629).

**Groups:** The group unit's own `onOffInterface` in the JSON **does not carry**
`isLockedDeviceApi` or `isLockedDeviceLocal`. `MainWindow::updateDevicePanel()`
therefore synthesizes all four flags from the member devices before passing the
device to `SwitchWidget`.  **Offline members** (`present == false`) are skipped
entirely — they do not count as switch-capable, locked, on, or off:

- `on` — `true` if **any** online member is on, **including locked members** (derived
  from members, not from the raw group `active` field — the Fritz!Box sets `active=true`
  if any member is on, but the raw value can briefly disagree with member state after a
  command).  Locked members contribute their real power state so that a fully-locked
  group (e.g. all members have `isLockedDeviceApi=1`) still shows the correct ON/OFF
  display state instead of always appearing as "OFF".
- `locked` — `true` if **all** switch-capable members are API-locked (group-level buttons disabled)
- `deviceLocked` — always `false` for groups (informational flag has no group-level meaning)
- `mixedSwitchState` — `true` if at least one online member (locked or controllable)
  is on and at least one is off — status label shows PARTIAL
- `hasLockedMembers` — `true` if there is at least one locked member **and** at least
  one controllable (non-locked) member — i.e. a partial lock situation
- `allOn` — `true` if **all** controllable members are already on (Turn On button disabled)
- `allOff` — `true` if **all** controllable members are already off (Turn Off button disabled)

`SwitchWidget::updateDevice()` gates buttons accordingly:

```cpp
// Only the API lock (sw.locked) prevents remote control.
// deviceLocked is the physical button lock — informational only.
bool canControl = device.present && !sw.locked;
// For groups: also disable Turn On if all members are already on,
// and Turn Off if all members are already off.
bool effectiveAllOn  = device.isGroup() ? sw.allOn  : sw.on;
bool effectiveAllOff = device.isGroup() ? sw.allOff : !sw.on;
m_onBtn->setEnabled(canControl && !effectiveAllOn);
m_offBtn->setEnabled(canControl && !effectiveAllOff);
// Toggle is always visible; disabled when locked or group has mixed on/off state.
m_toggleBtn->setEnabled(canControl && !sw.mixedSwitchState);
```

When either lock flag is set, an advisory label (`m_lockedLabel`) is shown beneath
the buttons with a human-readable explanation (translated into German in both
`po/de/fritzhome.po` and `translations/fritzhome_de.ts`).  Note that
`deviceLocked` produces an informational message only — it does **not** disable
the control buttons because the physical-button lock does not affect the
remote API.

### Status label — ON / OFF / PARTIAL

`SwitchWidget` shows a large status label (`m_statusLabel`) reflecting the current
power state.  For group devices where `mixedSwitchState` is `true`, the label
displays **"PARTIAL"** (German: "TEILWEISE") in dark-orange instead of the
misleading "ON"/"OFF":

```cpp
if (sw.mixedSwitchState) {
    m_statusLabel->setText(i18n("PARTIAL"));
    m_statusLabel->setStyleSheet("font-size: 16pt; font-weight: bold; color: darkorange;");
} else {
    m_statusLabel->setText(on ? i18n("ON") : i18n("OFF"));
    m_statusLabel->setStyleSheet("font-size: 16pt; font-weight: bold; color: " + color + ";");
}
```

The translation string `"PARTIAL"` → `"TEILWEISE"` is present in both
`po/de/fritzhome.po` and `translations/fritzhome_de.ts`.

### Toggle button popup mode — two separate concerns

The Toggle button's `QToolButton::popupMode` is controlled by **two independent
conditions** that are kept in separate fields to avoid conflating them:

| Condition | Field | Effect |
|-----------|-------|--------|
| Controllable members have mixed on/off states | `mixedSwitchState` | Label shows PARTIAL; Toggle disabled (ambiguous group command) |
| Some members are locked, others are not | `hasLockedMembers` | Toggle uses `InstantPopup` (group toggle always produces mixed result) |

When `mixedSwitchState || hasLockedMembers` is true, the Toggle button's `popupMode`
is set to `QToolButton::InstantPopup` (clicking the main button area opens the member
menu rather than sending an immediate group command).  When both are false, it reverts
to `QToolButton::MenuButtonPopup` (main area = group toggle, ▼ = member menu).

The reason `hasLockedMembers` triggers `InstantPopup` even when all controllable
members are currently in the same state: a group toggle command would always produce an
uncontrollable mixed result, because the locked members cannot follow the command.
Forcing the user to select an individual member from the dropdown is the only way to
achieve a predictable outcome.

## DeviceWidget — `setMembers()` hook

`DeviceWidget` declares a virtual `setMembers(const FritzDeviceList &)` with a
default no-op implementation.  Only `SwitchWidget` overrides it.  All other widget
subclasses inherit the no-op, so `MainWindow::updateDevicePanel()` can call
`dw->setMembers(...)` unconditionally without branching on widget type.

The `memberAins` field (`QStringList`) is populated for group devices from the
`memberUnitUids` JSON array in the REST API response (`GET /api/v0/smarthome/overview`),
parsed in `FritzApi::parseDeviceListJson()`.  Each `memberUnitUid` is resolved to a
device AIN via the `unitToDeviceAin` map (built from the `units` and `devices` arrays).
`MainWindow::collectMemberDevices()` looks up members **first by ID** via
`DeviceModel::deviceById()`, falling back to `DeviceModel::deviceByAin()` for
robustness.

---

## Stacked Power Chart for Groups

When a Fritz!Box group device is selected and has ≥ 2 energy-capable member devices,
`ChartWidget::buildPowerChart()` switches into **stacked area chart mode** instead of
the standard single-area chart.

### How it works

1. **`MainWindow::collectMemberDevices(groupDev)`** iterates `groupDev.memberAins`,
   resolves each entry first via `DeviceModel::deviceById()` (numeric ID lookup),
   falling back to `DeviceModel::deviceByAin()`, and returns a `FritzDeviceList`
   of member devices with their up-to-date histories.

2. **`ChartWidget::buildPowerChart(dev, memberDevices)`** detects stacked mode:
   - Filters `memberDevices` to only those with `hasEnergyMeter()`.
   - If `energyMembers.size() >= 2`: stacked mode; otherwise: single-area mode.

3. **Stacked layer construction:**  
   For N energy members, builds N `QAreaSeries` where:
   - `lower[i]` = cumulative sum of members 0..i-1 at each timestamp.
   - `upper[i]` = `lower[i]` + member i's value at that timestamp.
   - `lower[0]` = 0 (zero baseline).

   Timestamps are gathered into a `QMap<qint64, QVector<double>>` (the union of all
   member `powerHistory` timestamps), ensuring all layers share the same X coordinates.

4. **Legend:** shown with one entry per member device name, each in a distinct color
   from the built-in 8-color palette.

5. **Rolling updates:**  
   `ChartWidget::updateRollingCharts(dev, memberDevices)` rebuilds the stacked series
   data in-place (no chart rebuild) on every poll tick, using the same timestamp-union
   approach.

6. **Y-axis rescaling:**  
   `rescaleYPower()` in stacked mode sums all member values per timestamp to find the
   maximum cumulative height within the current time window.

### Member state in ChartWidget

- `m_powerStackedUpper` / `m_powerStackedLower`: parallel `QList<QXYSeries*>` holding
  pointers to the upper and lower boundary series for each layer. Owned by the `QChart`.
- `m_memberDevices`: cached `FritzDeviceList` of energy-capable members; refreshed on
  every `updateDevice()` and `updateRollingCharts()` call.
- In stacked mode, `m_powerSeries` and `m_powerLowerSeries` remain `nullptr`; the
  `updateRollingCharts` single-device path checks for these being non-null before
  proceeding.

### Fallback

If the group has 0 or 1 energy-capable members, or if `memberDevices` is empty
(e.g. `memberAins` was empty in the XML), `buildPowerChart` falls back to the standard
single-area chart showing the group's combined `powerHistory` (which is what the
Fritz!Box reports for the group AIN directly).

---

## Group Temperature Chart

When a Fritz!Box group device is selected and at least one member has temperature
capability (`hasTemperature() || hasThermostat()`), `ChartWidget::buildGroupTemperatureChart()`
builds a **multi-line temperature chart** with one `QLineSeries` per temperature-capable
member device.

### How it works

1. **Tab creation** (`updateDevice`): if `device.isGroup()`, `buildGroupTemperatureChart(memberDevices)`
   is called instead of `buildTemperatureChart(device)`. The function is a no-op if no
   member has temperature — in that case no Temperature tab is created.

2. **Series per member:** each temperature-capable member gets a `QLineSeries` populated
   from `member.temperatureHistory` (poll-driven rolling data). Members with an empty
   history but a valid `temperature` reading get a single fallback point at `now`.

3. **Colour palette:** the same built-in 8-colour palette used by the stacked power and
   energy history charts. Legend is always shown.

4. **Rolling updates** (`updateRollingCharts`): filters `memberDevices` to
   temperature-capable members, reloads each series in-place via `clear()+append()` (same
   pattern as single-device temperature). A member-count mismatch guard skips the update
   if the group composition changed since the last full rebuild.

5. **Y-axis rescaling** (`rescaleYGroupTemp`): iterates the `QPointF` lists of each
   `QXYSeries` in `m_groupTempSeries` to find the visible min/max within the current
   time window, with a 10% margin. Falls back to the full series range if no points lie
   within the window, and to [0, 30] if all series are empty.

### ChartWidget state for group temperature

| Member | Type | Purpose |
|---|---|---|
| `m_groupTempAxisX` | `QDateTimeAxis *` | Shared X axis; updated by `applyTimeWindow()` |
| `m_groupTempAxisY` | `QValueAxis *` | Shared Y axis; updated by `rescaleYGroupTemp()` |
| `m_groupTempSeries` | `QList<QXYSeries *>` | One entry per temperature-capable member; owned by the `QChart` |

All three are nulled / cleared in `updateDevice()` before each rebuild.

---

## Rolling History Retention

Poll-driven history (`temperatureHistory`, `powerHistory`, `humidityHistory` on each
`FritzDevice`) is trimmed on every successful device list response in `FritzApi::onAsyncReply()`
to keep only the last **24 hours**:

```cpp
const QDateTime cutoff = now.addSecs(-24 * 3600);
while (!dev.temperatureHistory.isEmpty() && dev.temperatureHistory.first().first < cutoff)
    dev.temperatureHistory.removeFirst();
// (same for powerHistory, humidityHistory)
```

This is a time-based trim (not a count-based cap), so the number of retained samples
depends on the configured polling interval. At the default 10 s interval this yields up
to ~8 640 samples per history list. The 24-hour window matches the maximum time-window
combo position (`kWindowMs[8]`).
