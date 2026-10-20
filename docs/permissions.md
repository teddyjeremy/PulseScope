# Permissions

| Action | Elevation |
| --- | --- |
| Open the UI, change theme, analyze a JSON fixture, export a report | No |
| Live process list, image name, `GetProcessTimes`, `GetProcessMemoryInfo` | No. Some protected processes return access denied; the row is marked limited and the counters are left empty. |
| Kernel ETW (`StartTrace` with system-logger flags) | Yes. `ERROR_ACCESS_DENIED` and `ERROR_PRIVILEGE_NOT_HELD` become `ElevationRequired` with an explanation. The rest of the app keeps running. |
| ETL import of a file the user can read | No, beyond normal file permissions |

The manifest requests `asInvoker`. The Live page can restart the process with `ShellExecuteW(..., "runas", ...)`.

`ERROR_ALREADY_EXISTS` means a session named PulseScope is still registered. Stop it from the app. If the process crashed, stop the leftover with an elevated `logman stop PulseScope -ets` or reboot. PulseScope does not kill an unrelated session automatically.

Only a limited number of system logger sessions can exist. If Windows refuses another one, the start error is shown as returned by `StartTrace`.
