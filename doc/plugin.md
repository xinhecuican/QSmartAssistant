Plugins are stored by default in `~/.config/QSmartAssistant/plugins`. For first startup, you need to `mv Data/plugins_order_default.txt Data/plugins_order.txt`

# Build a Plugin

Here we take `quitrobot` as an example

1. Create a new directory under the Plugins folder, the directory name is arbitrary![image-20240317104541150](https://image.xinhecuican.tech/img/image-20240317104541150.png)

2. Add a `CMakeLists.txt` file in Plugins with the following content

   ```cmake
   cmake_minimum_required(VERSION 3.14)
   
   project(QuitRobot VERSION 0.1.0) # Project name can be defined by yourself, the name used in plugins_order.txt is the project name
   
   set(CMAKE_INCLUDE_CURRENT_DIR ON)
   
   set(CMAKE_AUTOMOC ON)
   
   find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
   find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia)
   PLUGIN_COMMON()
   target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Multimedia lpcommon)
   
   ```

3. Create a new file in the directory with the following format

   ```c++
   
   #include "../Plugin.h"
   #include <QObject>
   // Header file must be integrated
   class QuitRobot : public QObject, Plugin {
       // The following three lines must be present
       Q_OBJECT
       Q_INTERFACES(Plugin)
       Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
   public:
       QuitRobot();
       QString getName() override;
       bool handle(const QString& text,
                   const ParsedIntent& parsedIntent,
                   bool& isImmersive) override;
       void setPluginHelper(IPluginHelper* helper) override;
       void recvMessage(const PluginMessage& message) override;
   signals:
       void sendMessage(PluginMessage message) override;
   private:
       IPluginHelper* helper;
   };
   
   ```

4. After writing, re-run cmake and then make, it will automatically generate a new plugin in the plugin folder

# Plugin Configuration

## hass

Description: Home Assistant interaction plugin, can trigger scripts and switch commands

Option: `hass`

| Option            | Default              | Description                            |
| ----------------- | -------------------- | -------------------------------------- |
| url               | http://127.0.0.1:8123 | Home Assistant API server address      |
| key               | xxx                  | Long-term access token key, note that Bearer cannot be missing |
| subscribedEvents  | state_changed        | Subscribe to events in Home Assistant, can perform operations when events are triggered |
| services          | xxx                  | Triggered services, specific configuration can refer to the following |

Services configuration method:

```
"services": [
    {
        "intent": "OPEN_FURNITURE",
        "notify": false,
        "useAssistant": true,
        "params": {
            "entity_id": "script.unknown_3",
            "variables": {
                "duration": "{time|--}"
            }
        },
        "path": "/api/services/script/turn_on",
        "pattern": ".*热水器.*",
        "response": { // Reply when event is triggered
            "valid": false,
            "matchId": false,
            "event": "event/state_changed",
            "matchPath": "event/data/entity_id", // Match entity_id field in state_changed event
            "matchValue": "switch.cuco_v3_57c4_switch",
            "successPath": "event/data/new_state/state", // Additional field to match
            "successValue": "on",
            "responseText": "设备执行成功"
        }
    },
    {
        "intent": "OPEN_FURNITURE",
        "notify": true,
        "params": {
            "entity_id": "script.cuco_cp2d_46fc_switch_timer",
            "variables": {
                "duration": "{time|--}"
            }
        },
        "path": "/api/services/script/turn_on",
        "pattern": ".*热水器.*"
    }
]
When encountering intent, start matching
Matching can be based on slotName and slotValue extracted by NLU, or using regular expression pattern
path is the path to call the API, specific can refer to https://developers.home-assistant.io/docs/api/rest/
params are the parameters passed to Home Assistant, note that you can use {slotName} to reference the content of the slot
notify: Whether to prompt execution success after completion

Parameters can use the value in the slot in the format of {slotName|defaultValue},
if defaultValue is --, the parameter is not added when matching fails
```

Call examples:

- Turn on the light
- Turn off the light
- Turn on the water heater
- Turn off the water heater
- Turn on the water heater for 20 minutes

Script writing example in Home Assistant:
```
alias: Timed water heater
sequence:
  - type: turn_on
    device_id: 23403927ea9d0ff1c17a44871a1c75ee
    entity_id: 6afa4b81894ad5ac9e212a9b742150e6
    domain: switch
  - condition: template
    value_template: "{{duration is defined }}"
  - delay: "{{ duration }}"
  - type: turn_off
    device_id: 23403927ea9d0ff1c17a44871a1c75ee
    entity_id: 6afa4b81894ad5ac9e212a9b742150e6
    domain: switch
description: ""
```

## netease music

Description: NetEase Cloud Music plugin

Option: `netease_cloud`

| Option       | Default | Parameter               |
| ---------- | ------ | ------------------ |
| phone      | xxx    | Login phone number       |
| password   | xxx    | Login password         |
| volumeStep | 10     | Volume adjustment step each time |
| homeDir    | ""     | js API path; **empty = auto-detect** (portable across Linux/Windows) |
| port       | 34932  | Port used by js API |

Currently supports Qt version API and js version API. If you want to use Qt version API, you can run `scripts/install_QCloudMusicApi.sh`. However, due to login issues, many APIs cannot be called in the Qt version, so js API is currently recommended.

To enable js API, you can run `cmake -DNETEASE_USE_JS=ON ..`

**Lazy login**: when the plugin is loaded (i.e. at program startup) it does **not** start the node server and does **not** attempt to log in — it only parses the config. The node backend is started and the login flow runs only on first real use (when a music intent such as "play music" is handled). Benefits:

- users who never touch NetEase pay no node process, network request, or risk-control cost;
- avoids hitting NetEase's human-verification challenge (`code -462`) during startup;
- faster startup, and no warning spam when node is not installed.

Call examples:

- Play music
- Play a song
- Play "Gu Yong Zhe" (Lonely Warrior)
- Play Zhang Jie's "Three Kingdoms"

## weather

Description: Weather plugin, using QWeather API

Option: `weather`

| Option | Default | Description                                               |
| ---- | ------ | ------------------------------------------------------ |
| key  | xxx    | [QWeather](https://dev.qweather.com/) API               |
| home | xxx    | Home location, used for default weather forecast. For example, you can fill in `北京市朝阳区` (Chaoyang District, Beijing) |

Call examples:

- Tomorrow's weather
- Weather for the next three days
- How's the weather tonight