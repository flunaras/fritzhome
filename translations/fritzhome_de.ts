<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE" sourcelanguage="en">
<!--
  Qt translation file for fritzhome — no-KF build (HAVE_KF=0).
  This file is compiled by lrelease / qt_add_translation → fritzhome_de.qm
  and embedded in the binary via a Qt resource.

  All strings below come from the i18n() shim which resolves to
  FritzI18n::tr0/tr1/tr2/tr3; we use a synthetic context "fritzhome".
-->

<context>
    <name>fritzhome</name>

    <!-- ── Application / About ───────────────────────────────────────────── -->
    <message>
        <source>Fritz!Box Smart Home</source>
        <translation>Fritz!Box Smart Home</translation>
    </message>
    <message>
        <source>Fritz!Home</source>
        <translation>Fritz!Home</translation>
    </message>
    <message>
        <source>Fritz!Box Smart Home manager for KDE Plasma</source>
        <translation>KDE-Plasma Smart-Home-Verwaltung für die Fritz!Box</translation>
    </message>
    <message>
        <source>© 2026 Ulf Saran</source>
        <translation>© 2026 Ulf Saran</translation>
    </message>
    <message>
        <source>Ulf Saran</source>
        <translation>Ulf Saran</translation>
    </message>
    <message>
        <source>Fritz!Box Smart Home Manager</source>
        <translation>Fritz!Box Smart Home Manager</translation>
    </message>

    <!-- ── Command-line options ──────────────────────────────────────────── -->
    <message>
        <source>Fritz!Box hostname or IP address (default: fritz.box)</source>
        <translation>Hostname oder IP-Adresse der Fritz!Box (Standard: fritz.box)</translation>
    </message>
    <message>
        <source>Fritz!Box username (leave blank for the default admin account)</source>
        <translation>Benutzername der Fritz!Box (leer lassen für das Standard-Admin-Konto)</translation>
    </message>
    <message>
        <source>Fritz!Box password</source>
        <translation>Passwort der Fritz!Box</translation>
    </message>
    <message>
        <source>Polling interval in seconds (default: 10)</source>
        <translation>Abfrageintervall in Sekunden (Standard: 10)</translation>
    </message>

    <!-- ── Main window — toolbar / menus ────────────────────────────────── -->
    <message>
        <source>&amp;File</source>
        <translation>&amp;Datei</translation>
    </message>
    <message>
        <source>&amp;Connect…</source>
        <translation>&amp;Verbinden …</translation>
    </message>
    <message>
        <source>&amp;Refresh</source>
        <translation>&amp;Aktualisieren</translation>
    </message>
    <message>
        <source>&amp;Quit</source>
        <translation>&amp;Beenden</translation>
    </message>

    <!-- ── Main window — sidebar / status bar ───────────────────────────── -->
    <message>
        <source>Refresh interval:</source>
        <translation>Aktualisierungsintervall:</translation>
    </message>
    <message>
        <source> s</source>
        <translation> s</translation>
    </message>
    <message>
        <source>How often to refresh device states (2–300 seconds)</source>
        <translation>Wie oft die Gerätezustände aktualisiert werden (2–300 Sekunden)</translation>
    </message>
    <message>
        <source>Select a device from the list.</source>
        <translation>Bitte wählen Sie ein Gerät aus der Liste aus.</translation>
    </message>

    <!-- ── Connection status strings ────────────────────────────────────── -->
    <message>
        <source>Not connected</source>
        <translation>Nicht verbunden</translation>
    </message>
    <message>
        <source>Connecting to %1…</source>
        <translation>Verbinde mit %1 …</translation>
    </message>
    <message>
        <source>Connected to %1</source>
        <translation>Verbunden mit %1</translation>
    </message>
    <message>
        <source>Connected to %1 — %2 device(s)</source>
        <translation>Verbunden mit %1 – %2 Gerät(e)</translation>
    </message>
    <message>
        <source>Session expired — reconnecting to %1…</source>
        <translation>Sitzung abgelaufen – Verbinde neu mit %1 …</translation>
    </message>

    <!-- ── Login / error dialogs ─────────────────────────────────────────── -->
    <message>
        <source>Login Failed</source>
        <translation>Anmeldung fehlgeschlagen</translation>
    </message>
    <message>
        <source>Login failed: %1</source>
        <translation>Anmeldung fehlgeschlagen: %1</translation>
    </message>
    <message>
        <source>Could not log in to Fritz!Box:
%1

Please check your credentials.</source>
        <translation>Anmeldung an der Fritz!Box fehlgeschlagen:
%1

Bitte überprüfen Sie Ihre Zugangsdaten.</translation>
    </message>
    <message>
        <source>Network error: %1</source>
        <translation>Netzwerkfehler: %1</translation>
    </message>
    <message>
        <source>Command failed for %1: %2</source>
        <translation>Befehl für %1 fehlgeschlagen: %2</translation>
    </message>

    <!-- ── TLS errors ────────────────────────────────────────────────────── -->
    <message>
        <source>TLS Certificate Error</source>
        <translation>TLS-Zertifikatsfehler</translation>
    </message>
    <message>
        <source>TLS error — see dialog for details</source>
        <translation>TLS-Fehler – Details im Dialog</translation>
    </message>
    <message>
        <source>If your Fritz!Box uses a self-signed certificate you can enable
&quot;Ignore TLS certificate warnings&quot; in the connection dialog.</source>
        <translation>Falls Ihre Fritz!Box ein selbstsigniertes Zertifikat verwendet, können Sie
„TLS-Zertifikatswarnungen ignorieren" im Verbindungsdialog aktivieren.</translation>
    </message>
    <message>
        <source>A TLS/SSL certificate error occurred while connecting to %1:

%2

%3</source>
        <translation>Beim Verbinden mit %1 ist ein TLS/SSL-Zertifikatsfehler aufgetreten:

%2

%3</translation>
    </message>

    <!-- ── Device tree column headers ────────────────────────────────────── -->
    <message>
        <source>Name</source>
        <translation>Name</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>Typ</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>Status</translation>
    </message>
    <message>
        <source>Temperature</source>
        <translation>Temperatur</translation>
    </message>
    <message>
        <source>Power</source>
        <translation>Leistung</translation>
    </message>
    <message>
        <source>Availability</source>
        <translation>Verfügbarkeit</translation>
    </message>

    <!-- ── Device type group labels ──────────────────────────────────────── -->
    <message>
        <source>Groups</source>
        <translation>Gruppen</translation>
    </message>
    <message>
        <source>Color Bulbs</source>
        <translation>Farblampen</translation>
    </message>
    <message>
        <source>Dimmers</source>
        <translation>Dimmer</translation>
    </message>
    <message>
        <source>Smart Plugs</source>
        <translation>Zwischenstecker</translation>
    </message>
    <message>
        <source>Switches</source>
        <translation>Schalter</translation>
    </message>
    <message>
        <source>Thermostats</source>
        <translation>Heizkörperregler</translation>
    </message>
    <message>
        <source>Blinds</source>
        <translation>Rollläden</translation>
    </message>
    <message>
        <source>Alarms</source>
        <translation>Alarme</translation>
    </message>
    <message>
        <source>Humidity Sensors</source>
        <translation>Feuchtesensoren</translation>
    </message>
    <message>
        <source>Sensors</source>
        <translation>Sensoren</translation>
    </message>

    <!-- ── Generic device status values ──────────────────────────────────── -->
    <message>
        <source>Active</source>
        <translation>Aktiv</translation>
    </message>
    <message>
        <source>ALARM</source>
        <translation>ALARM</translation>
    </message>
    <message>
        <source>Online</source>
        <translation>Online</translation>
    </message>
    <message>
        <source>Offline</source>
        <translation>Offline</translation>
    </message>
    <message>
        <source>On</source>
        <translation>Ein</translation>
    </message>
    <message>
        <source>Off</source>
        <translation>Aus</translation>
    </message>
    <message>
        <source>Comfort</source>
        <translation>Komfort</translation>
    </message>
    <message>
        <source>On (Comfort)</source>
        <translation>Ein (Komfort)</translation>
    </message>
    <message>
        <source>n/a</source>
        <translation>k. A.</translation>
    </message>

    <!-- ── Switch widget ──────────────────────────────────────────────────── -->
    <message>
        <source>Switch Control</source>
        <translation>Schaltsteuerung</translation>
    </message>
    <message>
        <source>Status: Unknown</source>
        <translation>Status: Unbekannt</translation>
    </message>
    <message>
        <source>Turn On</source>
        <translation>Einschalten</translation>
    </message>
    <message>
        <source>Turn Off</source>
        <translation>Ausschalten</translation>
    </message>
    <message>
        <source>Toggle</source>
        <translation>Umschalten</translation>
    </message>
    <message>
        <source>ON</source>
        <translation>EIN</translation>
    </message>
    <message>
        <source>OFF</source>
        <translation>AUS</translation>
    </message>
    <message>
        <source>PARTIAL</source>
        <translation>TEILWEISE</translation>
    </message>
    <message>
        <source>Locked via Fritz!Box UI. </source>
        <translation>Über Fritz!Box-Oberfläche gesperrt. </translation>
    </message>
    <message>
        <source>Locked on device.</source>
        <translation>Am Gerät gesperrt.</translation>
    </message>
    <message>
        <source>Members have different states — use the menu to toggle individually.</source>
        <translation>Mitglieder haben unterschiedliche Zustände – Menü zum Einzelschalten verwenden.</translation>
    </message>

    <!-- ── Thermostat widget ──────────────────────────────────────────────── -->
    <message>
        <source>Current Readings</source>
        <translation>Aktuelle Messwerte</translation>
    </message>
    <message>
        <source>Current Temp:</source>
        <translation>Aktuelle Temp.:</translation>
    </message>
    <message>
        <source>Battery:</source>
        <translation>Batterie:</translation>
    </message>
    <message>
        <source>Window:</source>
        <translation>Fenster:</translation>
    </message>
    <message>
        <source>Comfort Temp:</source>
        <translation>Komforttemperatur:</translation>
    </message>
    <message>
        <source>Eco Temp:</source>
        <translation>Absenktemperatur:</translation>
    </message>
    <message>
        <source>Set Target Temperature</source>
        <translation>Solltemperatur einstellen</translation>
    </message>
    <message>
        <source>Target: --</source>
        <translation>Soll: --</translation>
    </message>
    <message>
        <source>Set</source>
        <translation>Einstellen</translation>
    </message>
    <message>
        <source>Target: Off</source>
        <translation>Soll: Aus</translation>
    </message>
    <message>
        <source>Target: On (Comfort)</source>
        <translation>Soll: Komfort</translation>
    </message>
    <message>
        <source>Target: %1 °C</source>
        <translation>Soll: %1 °C</translation>
    </message>
    <message>
        <source>Open (heating paused)</source>
        <translation>Offen (Heizung pausiert)</translation>
    </message>
    <message>
        <source>Closed</source>
        <translation>Geschlossen</translation>
    </message>

    <!-- ── Energy meter widget ────────────────────────────────────────────── -->
    <message>
        <source>Energy Meter</source>
        <translation>Energiemessgerät</translation>
    </message>
    <message>
        <source>Current Power:</source>
        <translation>Aktuelle Leistung:</translation>
    </message>
    <message>
        <source>Total Energy:</source>
        <translation>Gesamtenergie:</translation>
    </message>
    <message>
        <source>Voltage:</source>
        <translation>Spannung:</translation>
    </message>

    <!-- ── Dimmer widget ──────────────────────────────────────────────────── -->
    <message>
        <source>Dimmer Control</source>
        <translation>Dimmersteuerung</translation>
    </message>
    <message>
        <source>Level: --</source>
        <translation>Stufe: --</translation>
    </message>
    <message>
        <source>Set Level</source>
        <translation>Stufe einstellen</translation>
    </message>
    <message>
        <source>On (100%)</source>
        <translation>Ein (100 %)</translation>
    </message>
    <message>
        <source>Off (0%)</source>
        <translation>Aus (0 %)</translation>
    </message>
    <message>
        <source>Level: %1%</source>
        <translation>Stufe: %1 %</translation>
    </message>

    <!-- ── Blind widget ───────────────────────────────────────────────────── -->
    <message>
        <source>Blind / Roller Shutter</source>
        <translation>Rollo / Rollladen</translation>
    </message>
    <message>
        <source>Status: --</source>
        <translation>Status: --</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>Öffnen</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>Schließen</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>Stopp</translation>
    </message>
    <message>
        <source>Opening</source>
        <translation>Öffnet</translation>
    </message>
    <message>
        <source>Closing</source>
        <translation>Schließt</translation>
    </message>
    <message>
        <source>Stopped</source>
        <translation>Gestoppt</translation>
    </message>
    <message>
        <source>Status: %1</source>
        <translation>Status: %1</translation>
    </message>

    <!-- ── Color widget ───────────────────────────────────────────────────── -->
    <message>
        <source>Hue / Saturation</source>
        <translation>Farbton / Sättigung</translation>
    </message>
    <message>
        <source>Color Temperature</source>
        <translation>Farbtemperatur</translation>
    </message>
    <message>
        <source>Hue &amp;&amp; Saturation</source>
        <translation>Farbton &amp;&amp; Sättigung</translation>
    </message>
    <message>
        <source>Apply Color</source>
        <translation>Farbe anwenden</translation>
    </message>
    <message>
        <source>Hue (0-359°):</source>
        <translation>Farbton (0–359°):</translation>
    </message>
    <message>
        <source>Saturation (0-255):</source>
        <translation>Sättigung (0–255):</translation>
    </message>
    <message>
        <source>Apply Color Temperature</source>
        <translation>Farbtemperatur anwenden</translation>
    </message>
    <message>
        <source>Temperature (K):</source>
        <translation>Temperatur (K):</translation>
    </message>
    <message>
        <source>Current: Hue=%1°, Saturation=%2</source>
        <translation>Aktuell: Farbton=%1°, Sättigung=%2</translation>
    </message>
    <message>
        <source>Current: Color Temperature=%1 K</source>
        <translation>Aktuell: Farbtemperatur=%1 K</translation>
    </message>

    <!-- ── Humidity sensor widget ─────────────────────────────────────────── -->
    <message>
        <source>Humidity Sensor</source>
        <translation>Feuchtesensor</translation>
    </message>
    <message>
        <source>Relative Humidity:</source>
        <translation>Relative Feuchte:</translation>
    </message>
    <message>
        <source>Comfort Level:</source>
        <translation>Komfortniveau:</translation>
    </message>
    <message>
        <source>Too Dry</source>
        <translation>Zu trocken</translation>
    </message>
    <message>
        <source>Too Humid</source>
        <translation>Zu feucht</translation>
    </message>
    <message>
        <source>Comfortable (30-70%)</source>
        <translation>Angenehm (30–70 %)</translation>
    </message>

    <!-- ── Alarm widget ───────────────────────────────────────────────────── -->
    <message>
        <source>Alarm Sensor</source>
        <translation>Alarmsensor</translation>
    </message>
    <message>
        <source>Alarm State:</source>
        <translation>Alarmzustand:</translation>
    </message>
    <message>
        <source>Last Alert:</source>
        <translation>Letzter Alarm:</translation>
    </message>
    <message>
        <source>ALARM TRIGGERED</source>
        <translation>ALARM AUSGELÖST</translation>
    </message>
    <message>
        <source>OK - No Alarm</source>
        <translation>OK – Kein Alarm</translation>
    </message>

    <!-- ── Login window ──────────────────────────────────────────────────── -->
    <message>
        <source>Connect to Fritz!Box</source>
        <translation>Mit Fritz!Box verbinden</translation>
    </message>
    <message>
        <source>&lt;b&gt;Fritz!Box Smart Home&lt;/b&gt;&lt;br&gt;&lt;small&gt;Enter your Fritz!Box connection details.&lt;/small&gt;</source>
        <translation>&lt;b&gt;Fritz!Box Smart Home&lt;/b&gt;&lt;br&gt;&lt;small&gt;Bitte geben Sie die Verbindungsdaten Ihrer Fritz!Box ein.&lt;/small&gt;</translation>
    </message>
    <message>
        <source>Connection</source>
        <translation>Verbindung</translation>
    </message>
    <message>
        <source>e.g. 192.168.178.1 or fritz.box</source>
        <translation>z. B. 192.168.178.1 oder fritz.box</translation>
    </message>
    <message>
        <source>Host:</source>
        <translation>Host:</translation>
    </message>
    <message>
        <source>Ignore TLS certificate warnings</source>
        <translation>TLS-Zertifikatswarnungen ignorieren</translation>
    </message>
    <message>
        <source>When checked, TLS/SSL certificate errors are silently ignored.
Use this only if your Fritz!Box uses a self-signed certificate
and you trust the network you are connected to.</source>
        <translation>Wenn aktiviert, werden TLS/SSL-Zertifikatsfehler stillschweigend ignoriert.
Verwenden Sie diese Option nur, wenn Ihre Fritz!Box ein selbstsigniertes Zertifikat verwendet
und Sie dem Netzwerk vertrauen, mit dem Sie verbunden sind.</translation>
    </message>
    <message>
        <source>Credentials</source>
        <translation>Anmeldedaten</translation>
    </message>
    <message>
        <source>Fritz!Box username (leave blank for default)</source>
        <translation>Fritz!Box-Benutzername (leer lassen für Standardkonto)</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation>Benutzername:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation>Passwort:</translation>
    </message>
    <message>
        <source>Connect automatically on startup</source>
        <translation>Beim Start automatisch verbinden</translation>
    </message>
    <message>
        <source>When checked, the application connects immediately on next launch
without showing this dialog.</source>
        <translation>Wenn aktiviert, verbindet sich die Anwendung beim nächsten Start sofort,
ohne diesen Dialog anzuzeigen.</translation>
    </message>
    <message>
        <source>Connect</source>
        <translation>Verbinden</translation>
    </message>

    <!-- ── Chart widget ───────────────────────────────────────────────────── -->
    <message>
        <source>Lock Y scale</source>
        <translation>Y-Achse fixieren</translation>
    </message>
    <message>
        <source>When checked, the vertical axis range is frozen at its
current scale and will not auto-adjust as new data arrives
or the time window is scrolled.</source>
        <translation>Wenn aktiviert, wird der Wertebereich der Y-Achse auf die aktuelle Skalierung fixiert
und passt sich nicht mehr automatisch an neue Daten oder
Änderungen des Zeitfensters an.</translation>
    </message>
    <message>
        <source>Last %1</source>
        <translation>Letzte %1</translation>
    </message>
    <message>
        <source>Time window:</source>
        <translation>Zeitfenster:</translation>
    </message>
    <message>
        <source>Temperature History</source>
        <translation>Temperaturverlauf</translation>
    </message>
    <message>
        <source>Power Consumption</source>
        <translation>Stromverbrauch</translation>
    </message>
    <message>
        <source>Humidity History</source>
        <translation>Feuchtigkeitsverlauf</translation>
    </message>
    <message>
        <source>Energy History</source>
        <translation>Energieverlauf</translation>
    </message>
    <message>
        <source>Time</source>
        <translation>Zeit</translation>
    </message>
    <message>
        <source>°C</source>
        <translation>°C</translation>
    </message>
    <message>
        <source>Watts</source>
        <translation>Watt</translation>
    </message>
    <message>
        <source>% RH</source>
        <translation>% rF</translation>
    </message>
    <message>
        <source>kWh</source>
        <translation>kWh</translation>
    </message>
    <message>
        <source>Wh</source>
        <translation>Wh</translation>
    </message>
    <message>
        <source>Percent</source>
        <translation>Prozent</translation>
    </message>
    <message>
        <source>Absolute</source>
        <translation>Absolut</translation>
    </message>
    <message>
        <source>Member Energy</source>
        <translation>Mitgliederenergie</translation>
    </message>
    <message>
        <source>Member Energy Distribution</source>
        <translation>Energieverteilung der Mitglieder</translation>
    </message>
    <message>
        <source>Target</source>
        <translation>Sollwert</translation>
    </message>
    <message>
        <source>Power (W)</source>
        <translation>Leistung (W)</translation>
    </message>
    <message>
        <source>Humidity (%)</source>
        <translation>Feuchte (%)</translation>
    </message>
    <message>
        <source>Humidity</source>
        <translation>Feuchte</translation>
    </message>
    <message>
        <source>Energy</source>
        <translation>Energie</translation>
    </message>
    <message>
        <source>Fetching energy history…</source>
        <translation>Energieverlauf wird geladen …</translation>
    </message>
    <message>
        <source>No chart data available for this device.</source>
        <translation>Für dieses Gerät sind keine Diagrammdaten verfügbar.</translation>
    </message>
    <message>
        <source>Info</source>
        <translation>Info</translation>
    </message>
    <message>
        <source>No energy history available from Fritz!Box.</source>
        <translation>Keine Energieverlaufsdaten von der Fritz!Box verfügbar.</translation>
    </message>
    <message>
        <source>Total Energy Consumed</source>
        <translation>Gesamtenergieverbrauch</translation>
    </message>
    <message>
        <source>Current Power</source>
        <translation>Aktuelle Leistung</translation>
    </message>
    <message>
        <source>Voltage</source>
        <translation>Spannung</translation>
    </message>
    <message>
        <source>No energy data available</source>
        <translation>Keine Energiedaten verfügbar</translation>
    </message>
    <message>
        <source>View:</source>
        <translation>Ansicht:</translation>
    </message>
    <message>
        <source>Last 24 hours</source>
        <translation>Letzte 24 Stunden</translation>
    </message>
    <message>
        <source>Rolling month</source>
        <translation>Rollierender Monat</translation>
    </message>
    <message>
        <source>Last 2 years</source>
        <translation>Letzte 2 Jahre</translation>
    </message>
    <message>
        <source>No energy data available for %1 yet.
Data will appear as it becomes available from Fritz!Box.</source>
        <translation>Keine Energiedaten für %1 verfügbar.
Daten werden angezeigt, sobald sie von der Fritz!Box verfügbar sind.</translation>
    </message>

    <!-- ── Fritz!Box API error messages ──────────────────────────────────── -->
    <message>
        <source>No host configured</source>
        <translation>Kein Host konfiguriert</translation>
    </message>
    <message>
        <source>Empty challenge from Fritz!Box</source>
        <translation>Leere Challenge von der Fritz!Box empfangen</translation>
    </message>
    <message>
        <source>Login failed.</source>
        <translation>Anmeldung fehlgeschlagen.</translation>
    </message>
    <message>
        <source> Fritz!Box is blocking login for %1 seconds.</source>
        <translation> Fritz!Box blockiert die Anmeldung für %1 Sekunden.</translation>
    </message>
    <message>
        <source>Session expired and automatic re-login failed %1 time(s). Please check your credentials.</source>
        <translation>Sitzung abgelaufen und automatische Neuanmeldung ist %1-mal fehlgeschlagen. Bitte überprüfen Sie Ihre Zugangsdaten.</translation>
    </message>

    <!-- ── Abbreviated month names (energy history chart axis labels) ──── -->
    <message>
        <source>Jan</source>
        <translation>Jan</translation>
    </message>
    <message>
        <source>Feb</source>
        <translation>Feb</translation>
    </message>
    <message>
        <source>Mar</source>
        <translation>Mär</translation>
    </message>
    <message>
        <source>Apr</source>
        <translation>Apr</translation>
    </message>
    <message>
        <source>May</source>
        <translation>Mai</translation>
    </message>
    <message>
        <source>Jun</source>
        <translation>Jun</translation>
    </message>
    <message>
        <source>Jul</source>
        <translation>Jul</translation>
    </message>
    <message>
        <source>Aug</source>
        <translation>Aug</translation>
    </message>
    <message>
        <source>Sep</source>
        <translation>Sep</translation>
    </message>
    <message>
        <source>Oct</source>
        <translation>Okt</translation>
    </message>
    <message>
        <source>Nov</source>
        <translation>Nov</translation>
     </message>
     <message>
         <source>Dec</source>
         <translation>Dez</translation>
     </message>
     <message>
         <source>Error fetching energy history: %1</source>
         <translation>Fehler beim Abrufen des Energieverlaufs: %1</translation>
     </message>
     <message>
         <source>Error fetching energy history for group:
%1</source>
         <translation>Fehler beim Abrufen des Energieverlaufs für die Gruppe:
%1</translation>
     </message>

 </context>
 </TS>
