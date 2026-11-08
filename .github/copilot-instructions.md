# Workspace Instructions

## ESP-IDF builds on Windows

When validating firmware changes, build from the repository root with `idf.py build`. The Espressif VS Code extension's **Open ESP-IDF Terminal** already activates the ESP-IDF environment. In another PowerShell, use the selected setup from `%USERPROFILE%\.espressif\esp_idf.json` as below. This invokes ESP-IDF's activation script with its configured Python directly, avoiding changes to `PATH` before activation. `IDF_TOOLS_PATH` points to the tools directory for this process. Keep activation and build in the same shell.

This follows Espressif's [ESP-IDF v5.5.2 Windows setup](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/get-started/windows-setup.html), which describes launching the ESP-IDF PowerShell environment to set up tool paths and environment variables.

```powershell
$config = Get-Content "$env:USERPROFILE\.espressif\esp_idf.json" -Raw | ConvertFrom-Json
$idf = $config.idfInstalled.PSObject.Properties[$config.idfSelectedId].Value
$env:IDF_TOOLS_PATH = $config.idfToolsPath
. (& $idf.python "$($idf.path)\tools\activate.py" --export)
idf.py build
```
