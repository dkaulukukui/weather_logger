/*
 By: Donald Kaulukukui
 Date: May 5th, 2018
 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
 version: 0.10
 
 Based off of the Sparkfun Weather Station by Nathan Seidle which is based off of Mike Grusin's USB Weather Board code.
<https://learn.sparkfun.com/tutorials/weather-station-wirelessly-connected-to-wunderground>
<https://github.com/sparkfun/Wimp_Weather_Station>


 Current:
 TBD for 2 seconds while transmitting
 ~TBDmA during sleep

 Todo:
 - rain
 - Pressure
 - light level
 - DS1307 integration
 - SD card logging
 - watchdog timer

 */

#include <avr/wdt.h> //We need watch dog for this program
#include <Wire.h>
#include <Adafruit_AM2315.h>  //temp_humidty sensor
#include <Adafruit_Sensor.h>  //adafruits unified sensor library required for lux sensor
#include <Adafruit_TSL2561_U.h>
#include <RTClib.h> 
#include <SPI.h>
#include <SD.h>

#define DEBUG  //debug to spit out serial debugging information

#ifdef DEBUG
 #define DEBUG_PRINT(x)  Serial.println (x)
#else
 #define DEBUG_PRINT(x)
#endif


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Hardware pin definitions
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// digital I/O pins
const byte WSPEED = 3;
const byte RAIN = 2;

// analog I/O pins
const byte WDIR = A0;  //wiring is 5V to vane to A0

//TEMP and Humidity Sensor
// Connect RED of the AM2315 sensor to 5.0V
// Connect BLACK to Ground
// Connect SCL/WHITE to i2c clock - SCL pin with  10Kohm pullup resistors to 5v
// Connect /SDA/YELLOW to i2c data - SDA pin with external 10Kohm pullup resistors to 5v

RTC_DS1307 rtc; //RTC object

//DS1307 is connected via I2C pins
/*SDA on DS1307 to UNO SDA
 * SCL on DS1307 to UNO SCL
 */

  /* SD card attached to SPI bus as follows:
 ** MOSI - pin 11 on Arduino Uno/Duemilanove/Diecimila, 51 on mega
 ** MISO - pin 12 on Arduino Uno/Duemilanove/Diecimila, 50 on mega
 ** CLK - pin 13 on Arduino Uno/Duemilanove/Diecimila,  52 on mega
 ** CS - depends on your SD card shield or module.*/
const byte chipSelect = 10;  //

Adafruit_AM2315 am2315;

//Lux sensor
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastWindCheck = 0;
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;
float windspeedmph; // [mph instantaneous wind speed]

// volatiles are subject to modification by IRQs
volatile unsigned long raintime, rainlast, raininterval;
volatile unsigned long rain = 0;  //rain counter

String filename;
File dataFile;  //data file object
byte file_date = 0;  //date of file currently active 
uint8_t logged_min = 0;  //minute flag for logging

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Configuration settings
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
const char delimeter = ',';  //data log file delimeter
const byte LOG_INTERVAL = 2; //logging interval in minutes

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ(){
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2

    raintime = millis(); // grab current time
    raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
    {
        //dailyrainin += 1; //Each dump is 0.011" of water
        rain += 1; //Increase this minute's amount of rain

        rainlast = raintime; // set up for next event
    }
}

void wspeedIRQ() { 
  // Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3

    if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
    {
        lastWindIRQ = millis(); //Grab the current time
        windClicks++; //There is 1.492MPH for each click per second.
        
    }
}

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//SETUP
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void setup() {
  // put your setup code here, to run once:

    #ifdef DEBUG
    Serial.begin(9600);
    #endif

    pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
    pinMode(RAIN, INPUT_PULLUP); // input from rain gauge sensor


    // attach external interrupt pins to IRQ functions
    attachInterrupt(0, rainIRQ, FALLING);
    attachInterrupt(1, wspeedIRQ, FALLING);

    // turn on interrupts
    interrupts();

///System initializations and checks

   DEBUG_PRINT("Init SD");

  if (!SD.begin(chipSelect)) {
    DEBUG_PRINT("SD init failed!");
  } else {
    DEBUG_PRINT("SD card good");
  }

  if (! rtc.begin()) {
    DEBUG_PRINT("No RTC");
    while (1);
  }

  if (! rtc.isrunning()) {
    DEBUG_PRINT("start RTC");
    // following line sets the RTC to the date & time this sketch was compiled
     rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));   
    DEBUG_PRINT("Time set to compile time"); 
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
     //rtc.adjust(DateTime(2018, 4, 8, 18, 24, 0));
  }

    if (! am2315.begin()) {
      DEBUG_PRINT("No AM2315");
     while (1);
    }

    if(!tsl.begin()){
    /* There was a problem detecting the TSL2561 ... check your connections */
    DEBUG_PRINT("no TSL2561");
    while(1);
    }

      /* Setup the sensor gain and integration time */
    configureSensor();

    DEBUG_PRINT("Setup Complete");

}

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//MAIN LOOP
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void loop() {

  DateTime now = rtc.now();
    
  String log_string;

  //Everyday create a new file and zeroize daily counts
  if(now.day() != file_date){
      filename = build_filename(now); //set filename to use for this day

      DEBUG_PRINT("filename created:");
      DEBUG_PRINT(filename);
      
      log_to_SD(filename, build_header());  //log data header to file
      file_date = now.day(); //update day of current file being logged to
  }

  //Every interval amount of time log data to the file
  if(now.minute()%LOG_INTERVAL == 0 && now.minute() != logged_min){

      //build string to log to file
      log_string = build_time_stamp(now); //add time_stamp
      log_string += build_data_fields(); //append data fields to string
      
      log_to_SD(filename, log_string); //log to file

      logged_min = now.minute(); //update logged minute variable to check

  }

}

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//SENSOR FUNCTIONS
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Returns the instataneous temp in farenheit
float get_temp(){
    float temp_c = am2315.readTemperature();
    float temp_f;

    temp_f = temp_c*1.8+32;

    return(temp_f);
}

//Returns the instataneous humidity
float get_humidity(){
    return(am2315.readHumidity());
}

//Returns the instataneous wind speed
float get_wind_speed(){
    float deltaTime = millis() - lastWindCheck; //750ms

    deltaTime /= 1000.0; //Covert to seconds

    float windSpeed = (float)windClicks / deltaTime; //3 / 0.750s = 4

    windClicks = 0; //Reset and start watching for new wind
    lastWindCheck = millis();

    windSpeed *= 1.492; //4 * 1.492 = 5.968MPH

    /* Serial.println();
     Serial.print("Windspeed:");
     Serial.println(windSpeed);*/

    return(windSpeed);
}

// read the wind direction sensor, return heading in degrees
int get_wind_direction(){
    unsigned int adc;

    adc = averageAnalogRead(WDIR); // get the current reading from the sensor

    //Serial.println(adc);

    // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
    // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
    // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

    if (adc < 100) return (90);
    if (adc < 200) return (135);
    if (adc < 300) return (180);
    if (adc < 500) return (45);
    if (adc < 650) return (225);
    if (adc < 800) return (0);
    if (adc < 900) return (315);
    if (adc < 1000) return (270);
    return (-1); // error, disconnected?
}

int get_light(){    
    float lux;

    /* Get a new lux sensor event */ 
    sensors_event_t event;
    tsl.getEvent(&event);

    lux = event.light;

    return(lux);  
}

float get_rain(){    
  
    //int rainin;  
    float rainin_flt;
/*   old get rain code
      //do math to calc cumulative rain
    rainin = 0;  
    for(int i = 0 ; i < 60 ; i++)
        rainin += rainHour[i];
 */

    rainin_flt = rain * 0.011; //convert rain value into float number in inches
    rain = 0;     //reset rain counter

    return(rainin_flt);
  
}


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Utility FUNCTIONS
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Takes an average of readings on a given pin //Returns the average
int averageAnalogRead(int pinToRead){
    byte numberOfReadings = 8;
    unsigned int runningValue = 0;

    for(int x = 0 ; x < numberOfReadings ; x++)
        runningValue += analogRead(pinToRead);
    runningValue /= numberOfReadings;

    return(runningValue);
}

//Configures the gain and integration time for the TSL2561
void configureSensor(void){
  /* You can also manually set the gain or enable auto-gain support */
  // tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  // tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */
  
  /* Changing the integration time gives you better sensor resolution (402ms = 16-bit data) */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

  /* Update these values depending on what you've set above! */  
  //Serial.println("------------------------------------");
  //Serial.print  ("Gain:         "); Serial.println("Auto");
  //Serial.print  ("Timing:       "); Serial.println("13 ms");
  //Serial.println("------------------------------------");
}

//builds and returns logfile timestamp string
String build_time_stamp(DateTime now){
     String time_stamp;
     
      //build string to log to file
      time_stamp = now.month();
      time_stamp += "/";
      time_stamp += now.day();
      time_stamp += "/";
      time_stamp += now.year();
      time_stamp += delimeter;
      time_stamp += now.hour();
      time_stamp += ":";
      
      if(now.minute() <10){ time_stamp +="0";}
      time_stamp += now.minute();      
      /*time_stamp += ":";

      if(now.second() <10){ time_stamp +="0";} 
      time_stamp += now.second();*/
      time_stamp += delimeter;
      
      return time_stamp;  
}

//builds and returns filename string
String build_filename(DateTime now){
     String time_stamp;
     
      //build string to log to file
      //filename needs to comply with the 8.3 filenaming convention
      //example:  12345678.123, textfile.txt, 
      //current filename format is: 20180525.csv, or YYYYMMDD.csv

      time_stamp = now.year(); 
      if(now.month() <10){ time_stamp +="0";} 
      time_stamp += now.month(); 
      if(now.day() <10){ time_stamp +="0";} 
      time_stamp += now.day();
      time_stamp += ".csv";

      return time_stamp;  
}

//builds and returns logfile data field string
String build_data_fields(){

     //init variables
    String data_fields;    
    char buff[8];
    float windspeedmph; // [mph instantaneous wind speed]
    int winddir;
    float humidity;
    float temp;
    float lux;
    float rain;

    //get values from sensors
    lux = get_light();
    windspeedmph = get_wind_speed();
    winddir = get_wind_direction();
    humidity = get_humidity();
    temp = get_temp();
    rain = get_rain();


    //build string
    dtostrf(temp, 3, 1, buff);          //convert Temp float to string min width 3, precision 1
    data_fields = buff;                  //Temp
    data_fields += delimeter;
    dtostrf(humidity, 2, 0, buff);      //convert Hum float to string min width 3, precision 0
    data_fields += buff;                //Hum
    data_fields += delimeter;
    dtostrf(windspeedmph, 3, 1, buff);   //convert Wsp float to string min width 3, precision 1
    data_fields += buff;                 //Wind speed
    data_fields += delimeter;
    data_fields += winddir;              //Wind direction
    dtostrf(rain, 4, 2, buff);   //convert rainin_flt float to string min width 3, precision 2  
    data_fields += delimeter;
    data_fields += buff;                 //Rain
    data_fields += delimeter;
    dtostrf(lux, 4, 2, buff);   //convert rainin_flt float to string min width 3, precision 2
    data_fields += buff;                 //Rain

    return data_fields;  
}

//builds and returns header string
String build_header(){
     String header;
     
      //build log file header

      header = "Date";
      header += delimeter;
      header += "Time";
      header += delimeter;
      header += "Temp";
      header += delimeter;
      header += "Hum";
      header += delimeter;
      header += "Wsp";
      header += delimeter;
      header += "Wdir";
      header += delimeter;
      header += "Rain";
      header += delimeter;
      header += "Light";

      return header;  
}

//Opens filename on SD card and logs log_string to file
void log_to_SD(String file_name, String log_string){

               dataFile = SD.open(file_name, FILE_WRITE);
              // if the file is available, write to it:
              if (dataFile) {
                dataFile.println(log_string);
                dataFile.close();
                // print to the serial port too:
                DEBUG_PRINT(log_string);  //send string to serial connection
              }
              // if the file isn't open, pop up an error:
              else {
                DEBUG_PRINT("error opening: ");
                DEBUG_PRINT(file_name);
              }

}





