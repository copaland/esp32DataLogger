# esp32DataLogger

## 파일 시스템

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

### platformio.ini  
```
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs

lib_deps = 
    ArduinoJson
	ModbusMaster
	NTPClient
	WiFiManager
	HTTPClient
	me-no-dev/AsyncTCP
	paulstoffregen/Time@^1.6.1
	adafruit/RTClib@^2.1.4
	SPI
	adafruit/Adafruit BusIO@^1.17.2
```
### main.cpp  
```
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>    // #include <SPIFFS.h>
```
 
중요 참고 사항: SPIFFS는 기능적으로 작동하지만, ESP32용으로 권장되고 더 현대적인 파일 시스템인 LittleFS는 더 나은 성능과 디렉토리 지원을 제공합니다. SPIFFS 대신 LittleFS를 사용하려면 platformio.ini 파일에 board_build.filesystem = littlefs를 추가하고 코드에서 LittleFS.h 라이브러리를 사용해야 합니다.  

## 빌드시 현재 날자와 시간 파일 생성

### platformio.ini 파일의 extra_scripts에 extra_script.py 파일 추가  
```
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs

extra_scripts = 
	pre:extra_script.py
	extra_script.py

lib_deps = 
	ArduinoJson
```

### extra_script.py
```
import os
from datetime import datetime

def create_rtc_config_header():
    """현재 PC 시간으로 rtc_config.h 헤더 파일 생성"""
    
    now = datetime.now()
    date_str = now.strftime("%Y-%m-%d")
    time_str = now.strftime("%H:%M:%S")
    
    header_file = os.path.join("include", "rtc_config.h")
    
    # 디렉토리 생성
    os.makedirs("include", exist_ok=True)
    
    # 헤더 파일 내용
    header_content = f'''#ifndef RTC_CONFIG_H
#define RTC_CONFIG_H

#define PRESET_DATE "{date_str}"
#define PRESET_TIME "{time_str}"

#endif /* RTC_CONFIG_H */
'''
    
    # 파일 쓰기
    with open(header_file, 'w', encoding='utf-8') as f:
        f.write(header_content)
    
    print(f"✓ Updated {header_file} - Date: {date_str}, Time: {time_str}")

def before_build(source, target, env):
    """빌드 전에 항상 실행"""
    print("=" * 60)
    print("UPDATING RTC CONFIG HEADER BEFORE BUILD")
    print("=" * 60)
    create_rtc_config_header()
    print("=" * 60)

# PlatformIO 환경에서 스크립트 등록
try:
    Import("env")
    
    # 빌드 전 여러 시점에 등록하여 확실히 실행
    env.AddPreAction("buildprog", before_build)
    env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", before_build)
    
    # 스크립트 로드 시점에서도 한 번 실행
    create_rtc_config_header()
    
    print("✓ RTC config auto-update script registered")
    
except Exception as e:
    print(f"⚠ Script registration error: {e}")
    # 등록에 실패해도 일단 헤더는 생성
    create_rtc_config_header()

# 독립 실행용
if __name__ == "__main__":
    create_rtc_config_header()
```

### rtc_config.h 빌드시 생성된 파일  
```
#ifndef RTC_CONFIG_H
#define RTC_CONFIG_H

#define PRESET_DATE "2025-09-04"
#define PRESET_TIME "22:13:16"

#endif /* RTC_CONFIG_H */
```

### main.cpp 에서 활용용
```
// RTC 설정 함수 (rtc_config.h의 값으로 설정)
void setRTCFromConfig() {
  Serial.println("Setting RTC from config file...");
  
  // rtc_config.h에서 날짜와 시간 파싱
  String dateStr = PRESET_DATE; // "2025-09-03"
  String timeStr = PRESET_TIME; // "23:21:23"
  
  Serial.print("Config Date: %s, Time: %s\n", dateStr.c_str(), timeStr.c_str());
  
  // 날짜 파싱 (YYYY-MM-DD)
  int year = dateStr.substring(0, 4).toInt();
  int month = dateStr.substring(5, 7).toInt();
  int day = dateStr.substring(8, 10).toInt();
  
  // 시간 파싱 (HH:MM:SS)
  int hour = timeStr.substring(0, 2).toInt();
  int minute = timeStr.substring(3, 5).toInt();
  int second = timeStr.substring(6, 8).toInt();
  
  Serial.print("Parsed DateTime: %04d-%02d-%02d %02d:%02d:%02d\n", 
                year, month, day, hour, minute, second);
  
  // DateTime 객체 생성
  DateTime configDateTime(year, month, day, hour, minute, second);
  
  // RTC에 시간 설정
  rtc.adjust(configDateTime);
  
  Serial.println("RTC set from config file successfully!");
  
  // 설정된 시간 확인
  DateTime now = rtc.now();
  Serial.print("RTC Current Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
}
```
