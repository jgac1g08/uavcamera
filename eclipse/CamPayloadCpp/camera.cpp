/*
 * main.cpp
 *
 *  Created on: Mar 5, 2010
 *      Author: ssmethurst
 */

#include "config.h"
#include <WProgram.h>

#include "spi_debug.h"
#include <avr/delay.h>

// Camera -> SD Card Code

// PIN LAYOUT:
// Digital 13  <->  SD CLK
// Digital 12  <->  SD MISO / Data 0 / SO
// Digital 11  <->  SD MOSI / CMD / DI
// Digital 4   <->  SD D3
// Arduino RX  <->  Camera TX
// Arduino TX  <->  Camera RX



#include "payload_base.h"

#include "SD/SD.h"
#include <stdio.h>

#include "camera.h"


//#define DLOG(...)
byte SYNC[] = {0xAA,0x0D,0x00,0x00,0x00,0x00};
byte RXtest[6];

// Serial PC cable to view error messages
#define sRxPin 2
#define sTxPin 3

char filePrefix[] = "test"; //Prefix of file name written to the SD card
char fileExt[] = ".jpg"; //File extension of the file being written to
char fileName[MAX_FILE_NAME_LENGTH]; //Contains full file name of file being written to
char numBuf[5]; //Number buffer, for the "find a file that doesn't exist" procedure. Could be a number up to 32767

// The following are all self-explanatory.
// Follows command structure described in the camera's datasheet

// sends commands to the camera
void sendCommand(byte ID1, byte ID2, byte par1, byte par2, byte par3, byte par4) {
  byte Command[] = {ID1,ID2,par1,par2,par3,par4};
  for(int i = 0; i < 6; i++)
	  Serial1.write(Command[i]);
}

// receives messages from the camera
void receiveComd() {
  for(int z = 0;z<6;z++){
      while (Serial1.available() <= 0) {}
      RXtest[z] = Serial1.read();
  }
}

void sendSYNC() {
  sendCommand(0xAA,0x0D,0x00,0x00,0x00,0x00);
}

void sendACK(byte comID, byte ACKcounter, byte pakID1, byte pakID2) {
 sendCommand(0xAA,0x0E,comID,ACKcounter,pakID1,pakID2);
}

void sendINITIAL(byte colourType, byte rawRes, byte jpegRes) {
 sendCommand(0xAA,0x01,0x00,colourType,rawRes,jpegRes);
}

void sendSETPACKAGESIZE(byte paklb, byte pakhb) {
 sendCommand(0xAA,0x06,0x08,paklb,pakhb,0x00);
}

void sendSNAPSHOT(byte snapType, byte skip1, byte skip2) {
 sendCommand(0xAA,0x05,snapType,skip1,skip2,0x00);
}

void sendGETPICTURE(byte picType) {
 sendCommand(0xAA,0x04,picType,0x00,0x00,0x00);
}



// verifies ACK
boolean isACK(byte byteundertest[],byte comID, byte pakID1, byte pakID2){
  byte testACK[] = {0xAA,0x0E,comID,0x00,pakID1,pakID2};
  for(int z = 0;z<6;z++){
    if((z != 3) && (byteundertest[z] != testACK[z]))
      return false;
  }
  return true;
}

// verifies SYNC
boolean isSYNC(byte byteundertest[]){
  for(int z = 0;z<6;z++){
    if(byteundertest[z] != SYNC[z])
      return false;
  }
  return true;
}

// verifies DATA
boolean isDATA(byte byteundertest[]){
    if((byteundertest[0] != 0xAA) || (byteundertest[1] != 0x0A)){
      return false; }
  return true;
}

// setup the file on the sd card

void setupSD(char fileName[]){
  // For Optimisation of the SD card writing process.
  // See http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1293975555
  sdFile = SD.open(fileName, O_CREAT | O_WRITE);
}

// synchronise with camera
void establishContact() {
  DLOG("Sending syncs\n\r\r"); // Send SYNC to the camera.
  //This could be up to 60 times
  while(1){
    while (Serial1.available() <= 0) {
      DLOG(".");
      sendSYNC();
      delay(50);
    }
    //DLOG("Data available\n\r");

    receiveComd();
 // Verify that the camera has sent back an ACK followed by SYNC
    if(!isACK(RXtest,0x0D,0x00,0x00))
      continue;
    DLOG("ACK received\n\r");
    receiveComd();

   if(!isSYNC(RXtest))
     continue;

   DLOG("SYNC received\n\r");
   // Send back an ACK
   sendACK(0x0D,0x00,0x00,0x00);

   break;
  }
}

// takes a snapshot
boolean takeSnapshot() {

  // setup image parameters
  DLOG("sending initial\n\r");
  delay(50);
  sendINITIAL(0x07,0x07,0x07);
  receiveComd();
  if(!isACK(RXtest,0x01,0x00,0x00))
      return false;
  DLOG("ACK received\n\r");

  // Package size = maximum of 512 bytes
  // Arduino serial.read() buffer size = 128 bytes
  unsigned int packageSize = 64;

  // sets the size of the packets to be sent from the camera
  DLOG("sending setpackagesize\n\r");
  delay(50);
  sendSETPACKAGESIZE((byte)packageSize,(byte)(packageSize>>8));
  receiveComd();
  if(!isACK(RXtest,0x06,0x00,0x00))
      return false;
  DLOG("ACK received\r");

  // camera stores a single frame in its buffer
  DLOG("sending snapshot\n\r");
  delay(50);
  sendSNAPSHOT(0x00,0x00,0x00);
  receiveComd();
  if(!isACK(RXtest,0x05,0x00,0x00))
      return false;
  DLOG("ACK received\n\r");

  // requests the image from the camera
  DLOG("sending getpicture\n\r");
  delay(50);
  sendGETPICTURE(0x01);
  receiveComd();
  if(!isACK(RXtest,0x04,0x00,0x00))
      return false;
  DLOG("ACK received\n\r");
  receiveComd();
  if(!isDATA(RXtest))
      return false;
  DLOG("DATA received\n\r");

  // Strips out the "number of packages" from the DATA command
  // then displays this over the serial link
  unsigned long bufferSize = 0;
  bufferSize = RXtest[5] | bufferSize;
  bufferSize = (bufferSize<<8) | RXtest[4];
  bufferSize = (bufferSize<<8) | RXtest[3];

  unsigned int numPackages = bufferSize/(packageSize-6);

  DLOG("Number of packages: ");
  DLOG(numPackages);
  DLOG("\n\r");

  byte dataIn[packageSize];
  int flushCount = 0;

  for(unsigned int package = 0;package<numPackages;package++){

    //DLOG("Sending ACK for package ");
    //DLOG(package,DEC);
    //DLOG("\n\r");
    sendACK(0x00,0x00,(byte)package,(byte)(package>>8));

      // receive package
      for(int dataPoint = 0; dataPoint<packageSize;dataPoint++){
        while (Serial1.available() <= 0) {} // wait for data to be available n.b. will wait forever...
        dataIn[dataPoint] = Serial1.read();
          if(dataPoint > 3 && dataPoint < (packageSize - 2)){ //strips out header data
          //sSerial.print(dataIn[dataPoint],BYTE);
          sdFile.print(dataIn[dataPoint]);
            if(flushCount == 511){
              sdFile.flush();
              flushCount = -1; // because it adds one straight away after
            }
          flushCount++;
          }
        //DLOG(dataPoint);
        //DLOG("\n\r");
      }
    //DLOG("Package ");
    //DLOG(package);
    //DLOG(" read successfully\n\r");

  }

  sendACK(0x0A,0x00,0xF0,0xF0);
  DLOG("Final ACK sent\n\r");
  return true;
}

void init_cam()
{
  // start serial port at 115200 bps
  Serial1.begin(115200);

  DLOG("Attemting to establish contact.\n\r");
  establishContact();  // send a byte to establish contact until receiver responds
  DLOG("Contact established, captain\n\r");

}

int take_picture()
{
	delay(2000);
  boolean fileExists = true;
  int fileNum = 0;
  while(fileExists){
    fileNum++;
    snprintf(fileName, sizeof(fileName), "%s%i%s", filePrefix, fileNum, fileExt); // prints "test{num},jpg" to fileName
    fileExists = SD.exists(fileName); // checks whether that file is on the SD card.
  }

  DLOG(fileName);
  DLOG("\n\r");
  setupSD(fileName); // creates the file on the sd for writing

  boolean wellness = takeSnapshot(); //Take a snapshot
  if(wellness){ //If no error is detected
   DLOG("All's well\n\r");
  }else{ //Error has been detected
    ILOG("Trouble at mill...\n\r");
    return -1;
  }
  sdFile.close(); // Close JPG file
  return fileNum;
}


