#include <WiFiManager.h>
#include <WiFiManagerTz.h>
#include <time.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <PicoSyslog.h>

#define CLOCK_NAME "trclock1"
#define SEEED true

#ifdef SEEED
#define IN1 D0 
#define IN2 D1 
#define SLEEPPIN D2 
#define MINPULSEDELAY 250
#define BUTTON D3
#else
#define IN1 A0 
#define IN2 A1 
#define SLEEPPIN A2 
#define MINPULSEDELAY 250
#define BUTTON A3
#endif

const int tickTime = 30; // how many seconds between ticks of the physical clock... usually 60 or 30
const int pulseTime = 40; //milliseconds

WiFiManager wifiManager;
PicoSyslog::SimpleLogger syslog;
char syslogServer[40];

bool configSaved = false;
bool timeUpdated = false;
uint16_t ticksPast12 = 0;
const int ticksIn12hours = 12*60*60/tickTime;
int timeLastExplained = 0;

WiFiManagerParameter custom_HourHand("HourHand", "Current position of Hour Hand", "0", 6);
WiFiManagerParameter custom_MinuteHand("MinuteHand", "Current position of Minute Hand", "0", 6);
WiFiManagerParameter custom_MinuteHandSeconds("Seconds", "Seconds e.g. if Minute hand does half minutes", "0", 6);
WiFiManagerParameter custom_SyslogServer("SyslogServer", "Syslog Server ip address", syslogServer, 40);

Preferences prefs;

bool explainNow() {
  if (timeLastExplained == 0) {
    timeLastExplained = millis();
    return true;
  }
  if ((millis() - timeLastExplained) < 0) { // overflow
    timeLastExplained = millis();
    return true;
  }
  if ((millis() - timeLastExplained)>10000) {
    timeLastExplained = millis();
    return true;
  }
  return false;
}

void on_time_available(struct timeval *t)
{
  // syslog.println("Received time adjustment from NTP");
  struct tm timeInfo;
  getLocalTime(&timeInfo, 1000);
  // syslog.println(&timeInfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
  timeUpdated = true;
}

void setup()
{
  // WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP    
  prefs.begin("SlaveClock", false);
  if (prefs.isKey("ticksPast12")) {
    ticksPast12 = prefs.getUShort("ticksPast12");
  }
  else {
    prefs.putUShort("ticksPast12", 0); // assume hands at 12 if not previously saved    
  }
  strcpy(syslogServer, prefs.getString("syslog", "192.168.1.72").c_str());
  setupHW();
  int secondsSince12 = ticksPast12 * tickTime;
  int hours = secondsSince12 / (60*60);
  int minutes = (secondsSince12 - (hours*60*60)) / 60;
  int seconds = secondsSince12 - (hours*60*60) - (minutes*60);
  custom_HourHand.setValue(String(hours).c_str(), 6);
  custom_MinuteHand.setValue(String(minutes).c_str(), 6);
  custom_MinuteHandSeconds.setValue(String(seconds).c_str(), 6);
  
  custom_SyslogServer.setValue(syslogServer, 40);
  Serial.begin(115200);
  Serial.printf("%i h, %i m, %i s\n", hours, minutes, seconds);
  
  delay(1000);
  Serial.println("Welcome to the updated slave clock example");
  Serial.print("tickTime ");Serial.println(tickTime);
  Serial.print("ticksPast12 ");Serial.println(ticksPast12);
  
  Serial.print("Mac Address: ");
  Serial.println(WiFi.macAddress());
  delay(1000);
  
    // optionally attach external RTC update callback
  WiFiManagerNS::NTP::onTimeAvailable( &on_time_available );
  
  // attach NTP/TZ/Clock-setup page to the WiFi Manager
  WiFiManagerNS::init( &wifiManager, nullptr );

  // /!\ make sure "custom" is listed there as it's required to pull the "Setup Clock" button
  std::vector<const char *> menu = {"wifi", "info", "custom", "param", "sep", "restart", "exit"};
  wifiManager.setMenu(menu);
  // wifiManager.setSaveConfigCallback(saveConfigCallback); // restart on credentials save, ESP32 doesn't like to switch between AP/STA
  // wifiManager.setBreakAfterConfig(true);
  wifiManager.setSaveParamsCallback([](){saveConfig();}); // restart on credentials save, ESP32 doesn't like to switch between AP/STA
  wifiManager.addParameter(&custom_HourHand);
  wifiManager.addParameter(&custom_MinuteHand);
  wifiManager.addParameter(&custom_MinuteHandSeconds);
  wifiManager.addParameter(&custom_SyslogServer);
  wifiManager.setConnectTimeout(10);
  wifiManager.setConnectRetries(10);          // necessary with sluggish routers and/or hidden AP
  wifiManager.setCleanConnect(true);          // ESP32 wants this
  wifiManager.setConfigPortalBlocking(true); // /!\ false=use "wifiManager.process();" in the loop()
  wifiManager.setConfigPortalTimeout(600);    // will wait 10min before closing portal
  

  // use button to start portal even if wifi correctly configured
  bool startPortal = false;
  if (checkButton()) {
    startPortal = true;
  }
  

  if(wifiManager.autoConnect("ClockSetupAP")){
    // Serial.print("Connected to Access Point, visit http://");
    Serial.print(WiFi.localIP());
    if (startPortal)
    {
      wifiManager.startConfigPortal("ClockSetupAP");
    }
  } else {

    Serial.println("Configportal is running, no WiFi has been set yet");
  }

  if(WiFi.status() == WL_CONNECTED){

    Serial.println("CONNECTED!");
    WiFiManagerNS::configTime();
    syslog.server = syslogServer;
    syslog.printf("mdns response %i \n", MDNS.begin(CLOCK_NAME));
    MDNS.addService("http", "tcp", 80);  
  
  
  }
  else {
    Serial.println("not connected what now?");
  }
  
  
}

void setupHW() {
  ///based on DRV8833
  pinMode(SLEEPPIN,OUTPUT);
  digitalWrite(SLEEPPIN, HIGH); // standby off
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

bool checkButton() {
  if ( digitalRead(BUTTON) == LOW ) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if( digitalRead(BUTTON) == LOW ){
      Serial.println("Button Pressed");
      return true;
    }   
  }
  return false;
}
      
void saveConfig() {
    syslog.println("save config callback");
    strcpy(syslogServer, custom_SyslogServer.getValue());
    syslog.server = syslogServer;
    syslog.print("syslog "); syslog.println(syslogServer);
    prefs.putString("syslog", syslogServer);
    syslog.println(prefs.getString("syslog"));
    int hours = String(custom_HourHand.getValue()).toInt();
    int minutes = String(custom_MinuteHand.getValue()).toInt();
    int seconds = String(custom_MinuteHandSeconds.getValue()).toInt();
    int secondsPast12 = hours*60*60 + minutes*60 + seconds;
    ticksPast12 = secondsPast12 / tickTime;
    syslog.print("ticksPast12 ");syslog.println(ticksPast12);
    prefs.putUShort("ticksPast12", ticksPast12);  
}

void loop()
{

 

  if (timeUpdated) {
    struct tm timeInfo;
    getLocalTime(&timeInfo, 1000);
    if (explainNow()) { syslog.println(&timeInfo, "%A, %B %d %Y %H:%M:%S zone %Z %z "); }
    int nowTP12 = calcTicksPast12(timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);
    // syslog.printf("hours %i minutes %i seconds %i",timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);
    // syslog.print("we are currently ticksPast12:");
    // syslog.println(nowTP12);

    // syslog.print("clock is: ");
    // syslog.println(ticksPast12);


    int pulses = howManyPulses(nowTP12, ticksPast12);
    // syslog.print("Pulses: ");
    // syslog.println(pulses);

    if ((pulses > (30000/ MINPULSEDELAY)) && wifiManager.getConfigPortalActive()) {
       // don't catch up if horribly wrong while config portal active (it will take more than 30 secs to fix
        if (explainNow()) { syslog.println("don't catch up - portal active"); }
        return; // wait till portal times out before catching up
    }
    if (pulses >= ((659*60)/tickTime)) { 
      if (explainNow()) { syslog.println("don't catch up - wait till time catches up"); }
      // 10 hours and 59m until back in sync, just wait (DST adjustment) rather than clicking forward for ages
        delay(MINPULSEDELAY);
        return; //loop
    }
    if (pulses >= 1) {
      // need to catch up
        if (explainNow()) { syslog.printf("catching up %i\n", pulses); }
        delay(MINPULSEDELAY);
        pulse();
        return; // loop
    }
    
    // pulse = 0
    //

    int secondsTillPulse = tickTime - (timeInfo.tm_sec % tickTime); // calculate when next pulse is due
    if (secondsTillPulse < 3) {
      syslog.print("getting ready to pulse in ");
      int d = (secondsTillPulse*1000)-pulseTime;
      syslog.printf("%i msec\n",d);
      delay(d);
      pulse();
    }   
  }
  else {
    if (explainNow()) { syslog.print("."); }
    delay(10);
  }
}
  // if (wifiManager.getConfigPortalActive()) { 
  //       // syslog.print("."); 
  //       return; // dont delay if active
  // }
  // else {
  //   delay(200); // nothing to do for a bit
  // }
// }

int calcTicksPast12(int hours, int minutes, int seconds) {
  return (((hours % 12)*60*60 + minutes*60 + seconds) / tickTime);
}

int howManyPulses(int tp12Now, int physTp12) {
  // syslog.printf("tp12Now = %i \n", tp12Now);
  
  // syslog.printf("ticksIn12hours = %i", ticksIn12hours);
  // syslog.printf("physTp12 = %i \n", physTp12);
  return (tp12Now - physTp12 + ticksIn12hours)%ticksIn12hours;
}

void pulse() {
  // send signal to clock
  struct tm timeInfo;
  
  int otp = ticksPast12;   
  ticksPast12 = (ticksPast12 +1)%ticksIn12hours;
  prefs.putUShort("ticksPast12", ticksPast12);
  
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  delay(pulseTime);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
// print out after the time is correct
  getLocalTime(&timeInfo, 1000);
  syslog.println(&timeInfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
  int nowTP12 = calcTicksPast12(timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);
  syslog.printf("***pulse*** %i -> %i\n",otp,nowTP12);

  
}