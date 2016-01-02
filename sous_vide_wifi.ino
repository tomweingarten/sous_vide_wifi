#include <PID_v1.h>
#include <PID_AutoTune_v0.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <OneWire.h>

#include <DallasTemperature.h>

#include <EEPROM.h>

#include <Ticker.h>

#include "settings.h"

ESP8266WebServer server(80);

int offset = 0;

// Heater parameters
#define SLOW_COOKER_DIGITAL     // Uncomment if you have a digital slow cooker 

#ifdef SLOW_COOKER_DIGITAL
#define MINIMUM_ONTIME 500
#else
#define MINIMUM_ONTIME 0
#endif

// Parameters for the temperature pins
// On ESP8266, I believe bus MUST be set to 2 (correct me if I'm wrong!)
#define ONE_WIRE_BUS 2
#define ONE_WIRE_PWR 14
#define ONE_WIRE_GND 12

// Output Relay
#define RelayPin 16

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// arrays to hold device address
DeviceAddress tempSensor;


// ************************************************
// PID Variables and constants
// ************************************************

//Define Variables we'll be connecting to
double Setpoint;
double Input;
double Output;

volatile long onTime = 0;

// pid tuning parameters
double Kp;
double Ki;
double Kd;

// EEPROM addresses for persisted data
const int SpAddress = 0;
const int KpAddress = 8;
const int KiAddress = 16;
const int KdAddress = 24;

//Specify the links and initial tuning parameters
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

// 10 second Time Proportional Output window
int WindowSize = 10000;
unsigned long windowStartTime;

// ************************************************
// Auto Tune Variables and constants
// ************************************************

byte ATuneModeRemember = 2;

// For more information on these parameters: http://playground.arduino.cc/Code/PIDAutotuneLibrary
double aTuneStep = 500; // Autotune in the range of +/- 500ms
double aTuneNoise = 1.0;
unsigned int aTuneLookBack = 20; // Peaks can be quite far apart, look back 20s

// Set a minimum autotune output, so the slow cooker does not turn off completely
double aTuneMinOutput = 10.0;

boolean tuning = false;

PID_ATune aTune(&Input, &Output);

// ************************************************
// States for state machine
// ************************************************
enum operatingState { OFF = 0, SETP, RUN};
operatingState opState = OFF;

// Use Ticker to handle timer
Ticker timer2;
Ticker timer_http;
void timer2_callback();
void timer_http_callback();

const int logInterval = 10000; // log every 10 seconds
long lastLogTime = 0;


// ************************************************
// HTML Setup
// ************************************************

const char DefaultHTMLStart[] PROGMEM = 
"<html><head><title>Sous-Wifide</title>"
"<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/css/bootstrap.min.css\">"
"<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/css/bootstrap-theme.min.css\">"
"<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/js/bootstrap.min.js\"></script>"
"</head>"
"<body>";

const char DefaultHTMLEnd[] PROGMEM = 
"</body></html>";

void LoadParameters();
void SaveParameters();
void handleRoot();  
void handleSetTemperature();
void handleGetStatus();
void handleSetRun();
void handleSetAutotune();
void handleSetDefaultParameters();
void Off();
void Run();
void DoControl();
void DriveOutput();
double EEPROM_readDouble(int);
void EEPROM_writeDouble(int, double);
void FinishAutoTune();


void setup(void) {

  //ESP.wdtDisable();                               // used to debug, disable wachdog timer,
  Serial.begin(115200);                           // full speed to monitor
  Serial.print("Let's get cooking!\n");

  // Initialize Relay Control:

  pinMode(RelayPin, OUTPUT);    // Output mode to drive relay
  digitalWrite(RelayPin, LOW);  // make sure it is off to start

  // Set up wifi
  WiFi.begin(SSID, PASS);                         // Connect to WiFi network
  while (WiFi.status() != WL_CONNECTED) {         // Wait for connection
    delay(500);
    Serial.print(".");
  }
  Serial.print("SSID: ");
  Serial.println(SSID);
  Serial.println("\n");

  // Set up Ground & Power for the sensor from GPIO pins
  pinMode(ONE_WIRE_GND, OUTPUT);
  digitalWrite(ONE_WIRE_GND, LOW);

  pinMode(ONE_WIRE_PWR, OUTPUT);
  digitalWrite(ONE_WIRE_PWR, HIGH);

  // Start up the DS18B20 One Wire Temperature Sensor

  sensors.begin();
  if (!sensors.getAddress(tempSensor, 0))
  {
    Serial.print(F("Sensor Error"));
  }
  sensors.setResolution(tempSensor, 12);
  sensors.setWaitForConversion(false);

  // Initialize the PID and related variables
  LoadParameters();
  myPID.SetTunings(Kp, Ki, Kd);

  myPID.SetSampleTime(1000);
  myPID.SetOutputLimits(0, WindowSize);

  // Set up the webserver
  //MDNS.begin(host);
  //Serial.println("mDNS responder started");
  server.on("/", handleRoot);
  server.on("/set_temperature/", handleSetTemperature);
  server.on("/get_status/", handleGetStatus);
  server.on("/set_run/", handleSetRun);
  server.on("/set_autotune/", handleSetAutotune);
  server.on("/set_default_parameters/", handleSetDefaultParameters);
  server.begin();
  //MDNS.addService("http", "tcp", 80);

  // Start the timers
  //timer_http.attach(0.1, timer_http_callback);
  timer2.attach(0.015, timer2_callback);

  // Temporarily set this to be running all the time
  myPID.SetMode(AUTOMATIC);
  opState = RUN;
  Setpoint = 50.0;
}


// ************************************************
// Main Control Loop
//
// All state changes pass through here
// ************************************************
void loop()
{
  switch (opState)
  {
    case OFF:
      Off();
      break;
    case RUN:
      Run();
      break;
  }
  delay(100); // Allow the ESP8266 to do other things
}

// ************************************************
// Initial State - press RIGHT to enter setpoint
// ************************************************
void Off()
{
  myPID.SetMode(MANUAL);
  digitalWrite(RelayPin, LOW);  // make sure it is off

  int keep_going = 1;
  while (keep_going) {
    // Check the webserver for updates
    server.handleClient();
    
    // Check to make sure we're still in off mode
    if (opState != OFF) {
      keep_going = 0;
    }
    delay(100);
  }

  // Prepare to transition to the RUN state
  sensors.requestTemperatures(); // Start an asynchronous temperature reading

  //turn the PID on
  myPID.SetMode(AUTOMATIC);
  windowStartTime = millis();
}

// ************************************************
// PID COntrol State
// SHIFT and RIGHT for autotune
// RIGHT - Setpoint
// LEFT - OFF
// ************************************************
void Run()
{
  Serial.print("Run state");
  SaveParameters();
  myPID.SetTunings(Kp, Ki, Kd);

  while (true)
  {
    // Check the webserver for updates
    server.handleClient();
    
    // Check to make sure we're still in run mode
    if (opState != RUN) {
      return;
    }

    DoControl();

    // periodically log to serial port in csv format
    if (millis() - lastLogTime > logInterval)
    {
      Serial.print(Input);
      Serial.print(",");
      Serial.println(Output);
      Serial.print(",");
      Serial.print(Setpoint);
      Serial.print("\n");
      lastLogTime = millis();
    }

    delay(100);
  }
}

void timer2_callback() {
  if (opState == OFF)
  {
    digitalWrite(RelayPin, LOW);  // make sure relay is off
  }
  else
  {
    DriveOutput();
  }
}

void LoadParameters()
{
  // Load from EEPROM
  EEPROM.begin(512); // On esp8266, we need to init EEPROM
  Setpoint = EEPROM_readDouble(SpAddress);
  Kp = EEPROM_readDouble(KpAddress);
  Ki = EEPROM_readDouble(KiAddress);
  Kd = EEPROM_readDouble(KdAddress);

  // Use defaults if EEPROM values are invalid
  if (isnan(Setpoint))
  {
    Setpoint = 60;
  }
  if (isnan(Kp))
  {
    Kp = 850;
  }
  if (isnan(Ki))
  {
    Ki = 0.5;
  }
  if (isnan(Kd))
  {
    Kd = 0.1;
  }
  Serial.print(Setpoint);
  Serial.print("\n");
  Serial.print(Kp);
  Serial.print("\n");
  Serial.print(Ki);
  Serial.print("\n");
  Serial.print(Kd);
  Serial.print("\n");
  EEPROM.end();
}

// ************************************************
// Save any parameter changes to EEPROM
// ************************************************
void SaveParameters()
{
  EEPROM.begin(512); // On esp8266, we need to init EEPROM
  if (Setpoint != EEPROM_readDouble(SpAddress))
  {
    EEPROM_writeDouble(SpAddress, Setpoint);
  }
  if (Kp != EEPROM_readDouble(KpAddress))
  {
    EEPROM_writeDouble(KpAddress, Kp);
  }
  if (Ki != EEPROM_readDouble(KiAddress))
  {
    EEPROM_writeDouble(KiAddress, Ki);
  }
  if (Kd != EEPROM_readDouble(KdAddress))
  {
    EEPROM_writeDouble(KdAddress, Kd);
  }
  EEPROM.commit(); // Another esp8266 thing
  EEPROM.end();
}

// ************************************************
// Write floating point values to EEPROM
// ************************************************
void EEPROM_writeDouble(int address, double value)
{
  byte* p = (byte*)(void*)&value;
  for (int i = 0; i < sizeof(value); i++)
  {
    EEPROM.write(address++, *p++);
  }
}

// ************************************************
// Read floating point values from EEPROM
// ************************************************
double EEPROM_readDouble(int address)
{
  double value = 0.0;
  byte* p = (byte*)(void*)&value;
  for (int i = 0; i < sizeof(value); i++)
  {
    *p++ = EEPROM.read(address++);
  }
  return value;
}


// ************************************************
// Execute the control loop
// ************************************************
void DoControl()
{
  // Read the input:
  Input = sensors.getTempC(tempSensor);
  sensors.requestTemperatures(); // prime the pump for the next one - but don't wait

  if (tuning) // run the auto-tuner
  {
    if (aTune.Runtime()) // returns 'true' when done
    {
      FinishAutoTune();
    }
  }
  else // Execute control algorithm
  {
    myPID.Compute();
  }

  // Time Proportional relay state is updated regularly via timer interrupt.
  onTime = Output;

  // If a digital slow cooker goes off for too long, it loses its settings
  if (onTime < MINIMUM_ONTIME) {
    onTime = MINIMUM_ONTIME;
  }
}

// ************************************************
// Called by ISR every 15ms to drive the output
// ************************************************
void DriveOutput()
{
  long now = millis();
  // Set the output
  // "on time" is proportional to the PID output
  if (now - windowStartTime > WindowSize)
  { //time to shift the Relay Window
    windowStartTime += WindowSize;
  }
  if ((onTime > 100) && (onTime > (now - windowStartTime)))
  {
    digitalWrite(RelayPin, HIGH);
  }
  else
  {
    digitalWrite(RelayPin, LOW);
  }
}


// ************************************************
// Start the Auto-Tuning cycle
// ************************************************

void StartAutoTune()
{
  // Remember the mode we were in
  ATuneModeRemember = myPID.GetMode();

  // Set up the auto-tune parameters
  aTune.SetNoiseBand(aTuneNoise);
  aTune.SetOutputStep(aTuneStep);
  aTune.SetLookbackSec((int)aTuneLookBack);
  aTune.SetControlType(1);

  // Turn tuning mode on
  tuning = true;
  
  // We don't want Output to go below 0, so set the initial
  // value of Output before we autotune
  if (Output - aTuneStep < aTuneMinOutput) {
    Output = aTuneStep + aTuneMinOutput;
  }
  // Don't let the auto tuner go beneath the minimum output either
  if (Output - aTuneStep < double(MINIMUM_ONTIME)) {
    Output = aTuneStep + double(MINIMUM_ONTIME);
  }
}

// ************************************************
// Return to normal control
// ************************************************
void FinishAutoTune()
{
  tuning = false;

  // Extract the auto-tune calculated parameters
  Kp = aTune.GetKp();
  Ki = aTune.GetKi();
  Kd = aTune.GetKd();

  // Re-tune the PID and revert to normal control mode
  myPID.SetTunings(Kp, Ki, Kd);
  myPID.SetMode(ATuneModeRemember);

  // Persist any changed parameters to EEPROM
  SaveParameters();
}

void handleRoot() {
  //String message(DefaultHTMLStart);
  String message = "";
  message += "<b>Welcome to Sous-Wifide!</b><br/><br/>";

  // Give the status of the system
  message += "Temperature: ";
  message += Input;
  message += "<br/>";
  message += "Running: ";
  if (opState == RUN) {
    message += "true";
  } else {
    message += "false";
  }
  message += "<br/>";
  message += "Auto-tuning: ";
  if (tuning) {
    message += "true";
  } else {
    message += "false";
  }
  message += "<br/>";
  message += "Setpoint temperature: ";
  message += Setpoint;
  message += "<br/>";
  message += "Kp: ";
  message += Kp;
  message += "<br/>";
  message += "Ki: ";
  message += Ki;
  message += "<br/>";
  message += "Kd: ";
  message += Kd;
  message += "<br/>";
  
  // Debug info
  message += "<br/><br/><br/>";
  message += "millis: ";
  message += millis();
  message += "<br/>";
  message += "WindowSize: ";
  message += WindowSize;
  message += "<br/>";
  message += "windowStartTime: ";
  message += windowStartTime;
  message += "<br/>";
  message += "Input: ";
  message += Input;
  message += "<br/>";
  message += "Output: ";
  message += Output;
  message += "<br/>";
  message += "onTime: ";
  message += onTime;
  message += "<br/>";
  //message += String(DefaultHTMLEnd);
  
  server.send(200, "text/html", message);
}

void handleSetTemperature() {
  String message = "";//DefaultHTMLStart;
  char new_setpoint[200];
  server.arg("value").toCharArray(new_setpoint, 200);
  Setpoint = double(atof(new_setpoint));
  message += "Target Temperature: ";
  message += Setpoint;
  //message += DefaultHTMLEnd;
  server.send(200, "text/html", message);
}

void handleGetStatus() {
  String message = "{\"temperature\":";
  message += Input;
  message += ",\"output\":";
  message += Output;
  message += ",\"opstate\":";
  message += opState;
  message += ",\"autotune\":";
  if (tuning) {
    message += "true";
  }else {
    message += "false";
  }
  message += "}";
  server.send(200, "application/json", message);
}

void handleSetRun() {
  String value = server.arg("value");
  String message = "";//DefaultHTMLStart;
  if (value == "true") {
    opState = RUN;
  }else if (value == "false") {
    opState = OFF;
  }else {
    value = "Invalid value given";
  }
  message += "Run: ";
  message += value;
  //message += DefaultHTMLEnd;
  server.send(200, "text/html", message);
}

void handleSetAutotune() {
  String value = server.arg("value");
  String message = "";//DefaultHTMLStart;
  if (value == "true") {
    if (tuning){
      message += "Autotune is already started. Please wait for it to complete.<br/>";
    } else if (abs(Input - Setpoint) < 0.5) {
      message += "Starting autotune.<br/>This may take some time, go enjoy a tea.<br/>";
      StartAutoTune();
    } else {
      message += "Can't start autotune yet. Please wait until the current temperature is within 0.5C";
    }
  }else if (value == "false") {
    message += "Ending autotune.<br/>WARNING: This may result in invalid parameters. For best operation, let autotune finish on its own<br/>";
    FinishAutoTune(); // Not necessary to call this, autotune will turn itself off when it's done
  }else {
    message += "Invalid value given. Value must be true or false";
  }
  //message += DefaultHTMLEnd;
  server.send(200, "text/html", message);
}

void resetDefaultParameters() {
  Setpoint = 60;
  // These are the Adafruit default parameters:
  Kp = 850;
  Ki = 0.5;
  Kd = 0.1;
}

void handleSetDefaultParameters() {
  String confirm = server.arg("confirm");
  String message = "";//DefaultHTMLStart;
  if (confirm == "true") {
    resetDefaultParameters();
    SaveParameters();
    message += "Parameters reset to defaults<br/>";
  }else {
    message += "Please set confirm to true<br/>";
  }
  //message += DefaultHTMLEnd;
  server.send(200, "text/html", message);
}
