/*
  temperature logger 
  Brian Smith 01/2025
*/
//#define DEBUG_VERBOSE        // uncomment to output mode debugging text to terminal
#include "FS.h"                // for file system
#include "SD.h"                // for sd card read/write
#include "SPI.h"               // SPI bus (used by micorSD card)
#include <Wire.h>              // I2C bus (used for temp, OLED screen, RTC modules)
#include <Adafruit_GFX.h>      // OLED
#include <Adafruit_SSD1306.h>  // OLED driver
#include "SparkFunBME280.h"    // temp sensor
#include <DS3231-RTC.h>        // RTC
//
// 128x64 OLED display with Adafruit library
//
#define OLED_RESET     -1    // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C  /// I2C address of OLED display < See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 oledDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
String oledString[4] = {"", "", "", "Temp logger"};
// temp sensor
BME280 tempSensor;
// RTC and clock data
DS3231 rtcDevice;
byte year;
int actualYear;
byte month;
byte date;
byte dOW;
byte hour;
byte minute;
byte second;
bool h12Flag;
bool pmFlag;
char amPM[16];
bool century = false;
char dowName[7][16] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
// used for text output strings
char buffer512[512];
// log file info
char logFileName[32];
int fileCount = 0;
bool updateWritten = false;
//
//
//
void setup() 
{
  // serial output setup
  Serial.begin(115200);
  delay(1000);
  #ifdef DEBUG_VERBOSE
  Serial.println("Temperature Logger");
  #endif
  #ifdef DEBUG_VERBOSE
  Serial.println("I2C device setup");
  Serial.println("================");
  #endif
  // start I2C bus
  Wire.begin();
  #ifdef DEBUG_VERBOSE
  Serial.println("OLED");
  Serial.println("====");
  #endif
  // start OLED
  if(!oledDisplay.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    #ifdef DEBUG_VERBOSE
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    #endif
  }
  oledString[1] = "OLED OK";
  RefreshOLED(1);
  delay(1000);

  #ifdef DEBUG_VERBOSE
  Serial.println("Temperature");
  Serial.println("============");
  #endif

  // start temp sensor - warn to OLED on fail
  if (tempSensor.beginI2C() == false) //Begin communication over I2C
  {
    #ifdef DEBUG_VERBOSE
    Serial.println("The temperature sensor did not respond. Please check wiring.");
    #endif DEBUG_VERBOSE
    oledString[1] = "TEMP FAIL!";
    RefreshOLED(1);
    while(1); //Freeze
  }
  oledString[1] = "TEMP OK";
  RefreshOLED(1);
  delay(1000);
  #ifdef DEBUG_VERBOSE
  Serial.print(" Temp: ");
  Serial.print(tempSensor.readTempF(), 2);
  #endif

  #ifdef DEBUG_VERBOSE
  Serial.println("RTC");
  Serial.println("============");
  #endif

  // get time from RTC
  actualYear = 2000 + (century ? 100 : 0) + rtcDevice.getYear();
	month = rtcDevice.getMonth(century);
  date = rtcDevice.getDate();
	dOW = rtcDevice.getDoW();
	hour = rtcDevice.getHour(h12Flag, pmFlag);
	minute = rtcDevice.getMinute();
	second = rtcDevice.getSecond();
	// Add AM/PM indicator
	if (h12Flag) 
  {
		if (pmFlag) 
    {
			sprintf(amPM, "PM");
		} 
    else 
    {
			sprintf(amPM, "AM");
		}
	} 
  else 
  {
  	sprintf(amPM, "24H");
	}
  sprintf(buffer512, "%s %02d/%02d/%04d %02d:%02d:%02d %s", dowName[dOW], month, date, actualYear, hour, minute, second, amPM);
  #ifdef DEBUG_VERBOSE
  Serial.println("RTC OK");
  Serial.println(buffer512);
  #endif
  oledString[1] = "RTC OK";
  RefreshOLED(1);  
  delay(1000);

  #ifdef DEBUG_VERBOSE
  Serial.println("SD Card setup");
  Serial.println("=============");
  #endif
  // start micorSD card - warn to OLED on fail
  if (!SD.begin()) 
  {
    oledString[1] = "microSD FAIL!";
    RefreshOLED(1);
    #ifdef DEBUG_VERBOSE
    Serial.println("Card Mount Failed");
    #endif
    while(1);
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) 
  {
    oledString[1] = "No microSD present!";
    RefreshOLED(1);
    #ifdef DEBUG_VERBOSE
    Serial.println("No SD card attached");
    #endif
    while(1);
  }

  #ifdef DEBUG_VERBOSE
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) 
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD) 
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC) 
  {
    Serial.println("SDHC");
  } 
  else 
  {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  // list and count files
  Serial.println("Listing files....");
  #endif
  oledString[1] = "microSD OK!";
  RefreshOLED(1);
  delay(1000);
  listDir(SD, "/", 3);
  // create next file name (logfilenn.txt 'nn' starts at 0, increments by 1 for each restart)
  sprintf(logFileName, "/logfile%02d.txt", (fileCount + 1));

  #ifdef DEBUG_VERBOSE
  Serial.printf("%d files at root level - next file name %s\n", fileCount, logFileName);
  Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
  #endif
  writeFile(SD, logFileName, "Temperature log\n");
  // last string displayed to oled screen is the current log file name
  oledString[3] = logFileName;
  // done with setup
}
//
// loop repeats forever after setup completes
// as long as power is on
// and if you didn't do something in code that crashes :)
// 
void loop()
{
  // prepare strings for output to screen (oledString[0] - oledString[3])
  // oledString[0] - current temp
  sprintf(buffer512,"%0.2lfF %0.2lfC", tempSensor.readTempF(), tempSensor.readTempC());
  oledString[0] = buffer512;
  // get date/time from RTC
  actualYear = 2000 + (century ? 100 : 0) + rtcDevice.getYear();
  month = rtcDevice.getMonth(century);
  date = rtcDevice.getDate();
	dOW = rtcDevice.getDoW();
	hour = rtcDevice.getHour(h12Flag, pmFlag);
	minute = rtcDevice.getMinute();
	second = rtcDevice.getSecond();
	// Add AM/PM indicator
	if (h12Flag) 
  {
  if (pmFlag) 
    {
	 	sprintf(amPM, "PM");
    }
    else 
    {
  	sprintf(amPM, "AM");
		}
	} 
  else 
  {
  	sprintf(amPM, "24H");
  }
  sprintf(buffer512, "%s %02d/%02d/%04d", dowName[dOW], month, date, actualYear);
  // oledString[1] - current date
  oledString[1] = buffer512;
  sprintf(buffer512, "%02d:%02d:%02d %s", hour, minute, second, amPM);
  // oledString[2] - current time
  oledString[2] = buffer512;
  // output the strings to the oled screen
  RefreshOLED(1);
  // if minute is divisible by 5, second = 0, and haven't written temp to microSD, do it now 
  if(((minute % 5) == 0) && (second == 0) && (updateWritten == false))
  {
    updateWritten = true;
    sprintf(buffer512, "%02d/%02d/%04d\t%02d:%02d %s\t%0.2lfF\t%0.2lfC\n", month, date, actualYear, hour, minute, amPM, tempSensor.readTempF(), tempSensor.readTempC());
    #ifdef DEBUG_VERBOSE
    Serial.print(buffer512);
    #endif
    appendFile(SD, logFileName, buffer512);
  }
  // second is not 0, change updateWritten to false so it will output at next 5 minute 0 seconds mark
  if(second != 0)
  {
    updateWritten = false;
  }
  // wait 100 milliseconds before repeating loop (no need to hammer the CPU to death)
  delay(100);
}
//
// count files on microSD
//
int countFiles(fs::FS &fs, const char *dirname, uint8_t levels)
{
  int fileCount = 0;
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) 
  {
    Serial.println("Failed to open directory");
    return fileCount;
  }
  if (!root.isDirectory()) 
  {
    Serial.println("Not a directory");
    return fileCount;
  }

  File file = root.openNextFile();
  while (file) 
  {
    if (file.isDirectory()) 
    {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) 
      {
        listDir(fs, file.path(), levels - 1);
      }
    } 
    else 
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
      fileCount++;
    }
    file = root.openNextFile();
  }
}
//
// list files on microSD
//
void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) 
  {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) 
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) 
  {
    if (file.isDirectory()) 
    {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) 
      {
        listDir(fs, file.path(), levels - 1);
      }
    } 
    else 
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
      fileCount++;
    }
    file = root.openNextFile();
  }
}
//
// create a folder on microSD
//
void createDir(fs::FS &fs, const char *path) 
{
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}
//
// delete a folder on microSD
//
void removeDir(fs::FS &fs, const char *path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}
//
// read a file
//
void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}
//
// write a file (creates a new file)
//
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}
//
// append to an existing file
//
void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}
//
// change file name
//
void renameFile(fs::FS &fs, const char *path1, const char *path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}
//
// delete a file
//
void deleteFile(fs::FS &fs, const char *path) 
{
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}
//
// test file read/write speed
//
void testFileIO(fs::FS &fs, const char *path) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %lu ms\n", flen, end);
    file.close();
  } else {
    Serial.println("Failed to open file for reading");
  }

  file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %lu ms\n", 2048 * 512, end);
  file.close();
}
//
// output oledString[0] - oledString[3] to lines on screen
//
void RefreshOLED(int fontSize)
{
  #ifdef DEBUG_EXTRA_VERBOSE
    Serial.println("RefreshOLED");
    for(int idx = 0; idx < 4; idx++)
    {
      Serial.println(oledString[idx]);
    }
  #endif
  oledDisplay.clearDisplay();
  oledDisplay.setTextSize(fontSize);             // Draw 2X-scale text
  oledDisplay.setTextColor(SSD1306_WHITE);
  int y = 0;
  for(int idx = 0; idx < 4; idx++)
  {
    oledDisplay.setCursor(0,y);             // Start at top-left corner
    oledDisplay.print(oledString[idx].c_str());
    y+= 16;
  }
  oledDisplay.display();
}