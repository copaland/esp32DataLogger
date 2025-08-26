# esp32DataLogger

ESP32를 사용한 VS Code 및 PlatformIO에서 SPIFFS(Serial Peripheral Interface Flash File System)를 사용하려면 ESP32의 플래시 메모리에 파일을 관리하기 위한 특정 단계가 필요합니다.  

1. 데이터 폴더 생성:  
VS Code의 PlatformIO 프로젝트 폴더 내에서 src 폴더와 동일한 수준에 data라는 이름의 새 폴더를 생성합니다. 

3. 데이터 폴더에 파일 추가:  
이 data 폴더에는 ESP32의 SPIFFS에 업로드할 모든 파일(예: HTML, CSS, JavaScript, 구성 파일, 이미지 등)을 이 data 폴더 내에 배치합니다.

4. 파일 시스템 이미지 빌드 및 업로드:  
VS Code에서 PlatformIO 사이드바(개미 아이콘)로 이동합니다.
“PROJECT TASKS” 아래에서 특정 ESP32 환경(예: env:esp32dev)을 찾아주세요.
‘esp32dev’ 아래 ‘Platform’ 섹션을 확장하고 “Build Filesystem Image”를 선택하세요. 이 작업은 데이터 폴더의 내용을 포함하는 .bin 파일을 생성합니다.
이미지를 빌드한 후 동일한 메뉴에서 “Upload Filesystem Image”를 선택하여 이 .bin 파일을 ESP32의 SPIFFS에 플래시합니다.

5. ESP32에서 파일 액세스:  
ESP32 코드(예: 아두이노 스케치)에서 SPIFFS.h 라이브러리를 사용하여 업로드된 파일과 상호작용할 수 있습니다.
SPIFFS.h 헤더를 포함하고 파일을 열거나 읽거나 쓰기 전에 SPIFFS.begin()을 호출하여 파일 시스템을 마운트해야 합니다.

platformio.ini  
```
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs

lib_deps = 
	lorol/LittleFS_esp32@^1.0.6
	WiFiManager
	HTTPClient
	me-no-dev/AsyncTCP
```
main.cpp  
```
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>    // #include <SPIFFS.h>
```
 
중요 참고 사항: SPIFFS는 기능적으로 작동하지만, ESP32용으로 권장되고 더 현대적인 파일 시스템인 LittleFS는 더 나은 성능과 디렉토리 지원을 제공합니다. SPIFFS 대신 LittleFS를 사용하려면 platformio.ini 파일에 board_build.filesystem = littlefs를 추가하고 코드에서 LittleFS.h 라이브러리를 사용해야 합니다.
