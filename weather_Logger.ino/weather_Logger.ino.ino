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
 - Temp sensor
 - Humidity
 - Pressure
 - light level
 - SD card logging
 - watchdog timer

 */

#include <avr/wdt.h> //We need watch dog for this program
#include <Wire.h>
#include <Adafruit_AM2315.h>  //temp_humidty sensor



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


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastWindCheck = 0;
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;

float windspeedmph; // [mph instantaneous wind speed]

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
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


    // attach external interrupt pins to IRQ functions
    //attachInterrupt(0, rainIRQ, FALLING);
    attachInterrupt(1, wspeedIRQ, FALLING);

    // turn on interrupts
    interrupts();

    if (! am2315.begin()) {
     Serial.println("AM2315 Sensor not found, check wiring & pullups!");
     while (1);
    }

}

void loop() {

  float windspeedmph; // [mph instantaneous wind speed]
  int winddir;
  float humidity;
  float temp;

  windspeedmph = get_wind_speed();
  winddir = get_wind_direction();
  humidity = get_humidity();
  temp = get_temp();

  Serial.print("$winddir=");
  Serial.print(winddir);
  Serial.print(", windspeedmph= ");
  Serial.print(windspeedmph, 1);
  Serial.print(",humidity=");
  Serial.print(humidity, 1);
  Serial.print(",temp=");
  Serial.println(temp, 1);

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