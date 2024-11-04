# m5wificom-Source Code
compile code with Arduino IDE <br>
Or use the ready-to-flash and configuration at<br>
https://m5wificom.n3gp.com/
<h4>Library requirements</h4>
AsyncTCP @ 1.1.4 <br>
ESPAsyncWebServer @ 3.1.0 <br>
PubSubClient @ 2.8.0 <br>
ArduinoJson @ 7.2.0 <br>
StringStream @ arcao <br>
ImprovWiFiLibrary @ 0.0.1 <br>
M5GFX @ 0.1.17 <br>
M5Unified @ 0.1.17 <br>
M5StickCPlus2 @ 1.0.1 <br>
DMComm @ 0.4.2 <br>
<h4>Board requirements</h4>
M5Stack @ 2.1.2<br><hr>
<h4>How to change pin interface</h4>
change PIN at line 32 and 33<br>
Source (m5stick_cp2.ino)

    int INPUT_PIN = 36;
    int OUTPUT_PIN = 26;

<h4>Enable BtnPWR to M5StickCPlus2</h4>
add Button_Class &BtnPWR   = M5.BtnPWR; to class M5StickCPlus2 in file M5StickCPlus2.h <br>
\Arduino\libraries\M5StickCPlus2\src\M5StickCPlus2.h <br>
Source (M5StickCPlus2.h)

    #ifndef _M5_STICKC_PLUS2_H_
    #define _M5_STICKC_PLUS2_H_
    
    #include "M5Unified.h"
    #include "M5GFX.h"
    
    namespace m5 {
    class M5StickCPlus2 {
       private:
        /* data */
    
       public:
        void begin();
        void begin(m5::M5Unified::config_t cfg);
    
        M5GFX &Display = M5.Display;
        M5GFX &Lcd     = Display;
    
        IMU_Class &Imu         = M5.Imu;
        Power_Class &Power     = M5.Power;
        RTC8563_Class &Rtc     = M5.Rtc;
        Speaker_Class &Speaker = M5.Speaker;
        Mic_Class &Mic         = M5.Mic;
        Button_Class &BtnA     = M5.BtnA;
        Button_Class &BtnB     = M5.BtnB;
        Button_Class &BtnPWR   = M5.BtnPWR;
    
        /// for internal I2C device
        I2C_Class &In_I2C = m5::In_I2C;
    
        /// for external I2C device (Port.A)
        I2C_Class &Ex_I2C = m5::Ex_I2C;
        void update(void);
    };
    }  // namespace m5
    
    extern m5::M5StickCPlus2 StickCP2;
    
    #endif
<hr>
*credit for*<br>
base library form <a href="https://wificom.dev/">BrassBolt@wificom</a>, <a href="https://github.com/dmcomm">BladeSabre@DMComm</a>, Arduino<br>
base drigirom form <a href="https://humulos.com/">humulos</a>, <a href="https://www.youtube.com/@joushiikuta/videos">jyoshiikuta</a>, Feanan<br>
caft by <a href="mailto:naynan@n3gp.com">naynan</a><br>
