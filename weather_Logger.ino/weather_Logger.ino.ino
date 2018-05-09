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
volatile unsigned long raintime, rainlast, raininterval, rain;

float rainin; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
volatile int dailyrainin; // [rain inches so far today in local time]
volatile int rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

byte minutes; //Keeps track of where we are in various arrays of data

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
    raintime = millis(); // grab current time
    raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
    {
        dailyrainin += 1; //Each dump is 0.011" of water
        rainHour[minutes] += 1; //Increase this minute's amount of rain

        rainlast = raintime; // set up for next event
    }
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
    if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
    {
        lastWindIRQ = millis(); //Grab the current time
        windClicks++; //There is 1.492MPH for each click per second.
        
    }
}


void setup() {
  // put your setup code here, to run once:

    Serial.begin(9600);

    pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
    pinMode(RAIN, INPUT_PULLUP); // input from rain gauge sensor


    // attach external interrupt pins to IRQ functions
    attachInterrupt(0, rainIRQ, FALLING);
    attachInterrupt(1, wspeedIRQ, FALLING);

    // turn on interrupts
    interrupts();

    if (! am2315.begin()) {
     Serial.println("AM2315 Sensor not found, check wiring & pullups!");
     while (1);
    }

    if(!tsl.begin())
    {
    /* There was a problem detecting the TSL2561 ... check your connections */
    Serial.print("Ooops, no TSL2561 detected ... Check your wiring or I2C ADDR!");
    while(1);
    }

      /* Setup the sensor gain and integration time */
    configureSensor();
    
    Serial.println("HoFarm Weather Station Setup Complete");

}

void loop() {

  float windspeedmph; // [mph instantaneous wind speed]
  int winddir;
  float humidity;
  float temp;
  float lux;

  //lux sensor
    /* Get a new lux sensor event */ 
  sensors_event_t event;
  tsl.getEvent(&event);

  lux = event.light;
  windspeedmph = get_wind_speed();
  winddir = get_wind_direction();
  humidity = get_humidity();
  temp = get_temp();
  

  //Total rainfall for the day is calculated within the interrupt
  //Calculate amount of rainfall for the last 60 minutes
  rainin = 0;  
  for(int i = 0 ; i < 60 ; i++)
        rainin += rainHour[i];

  float rainin_flt = rainin * 0.011;
  float dailyrainin_flt = dailyrainin *0.011;

  Serial.print("$winddir=");
  Serial.print(winddir);
  Serial.print(", windspeedmph= ");
  Serial.print(windspeedmph, 1);
  Serial.print(",humidity=");
  Serial.print(humidity, 1);
  Serial.print(",temp=");
  Serial.print(temp, 1);
  Serial.print(",rainin=");
  Serial.print(rainin_flt, 2);
  Serial.print(",dailyrainin=");
  Serial.print(dailyrainin_flt, 2);
  Serial.print(",lux=");
  Serial.println(lux, 2);

  delay(1000);

}

//Returns the instataneous temp in farenheit
float get_temp()
{
    float temp_c = am2315.readTemperature();
    float temp_f;

    temp_f = temp_c*1.8+32;

    return(temp_f);
}

//Returns the instataneous humidity
float get_humidity()
{
    return(am2315.readHumidity());
}



//Returns the instataneous wind speed
float get_wind_speed()
{
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

int get_wind_direction()
// read the wind direction sensor, return heading in degrees
{
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

//Takes an average of readings on a given pin
//Returns the average
int averageAnalogRead(int pinToRead)
{
    byte numberOfReadings = 8;
    unsigned int runningValue = 0;

    for(int x = 0 ; x < numberOfReadings ; x++)
        runningValue += analogRead(pinToRead);
    runningValue /= numberOfReadings;

    return(runningValue);
}

/**************************************************************************/
/*
    Configures the gain and integration time for the TSL2561
*/
/**************************************************************************/
void configureSensor(void)
{
  /* You can also manually set the gain or enable auto-gain support */
  // tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  // tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */
  
  /* Changing the integration time gives you better sensor resolution (402ms = 16-bit data) */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

  /* Update these values depending on what you've set above! */  
  Serial.println("------------------------------------");
  Serial.print  ("Gain:         "); Serial.println("Auto");
  Serial.print  ("Timing:       "); Serial.println("13 ms");
  Serial.println("------------------------------------");
}


