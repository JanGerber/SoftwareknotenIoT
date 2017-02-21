//Libraries
#include <DHT.h>       //Temperatur und Luftfeuchtigkeit
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <LowPower.h>
#include <EEPROM.h>
#include <AESLib.h>
#include <math.h>
#include <Base64.h>
#include <printf.h>

//Konstanten und Variablen

  //ID des Arduinos
  unsigned int arduinoId = 9999;
  

  //DHT22
  #define DHTPIN 4     // what pin we're connected to
  #define DHTTYPE DHT22  // DHT 22  (AM2302)
  DHT dht(DHTPIN, DHTTYPE); //// Initialize DHT sensor for normal 16mhz Arduino
  float humidity;
  float temperatur;
    
  //BPM 180
  Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);
  int pressure;

  // NRF24L01
  RF24 radio(9,10);
  // Example below using pipe5 for writing
  const uint64_t pipes[3] = { 0xF0F0F0F0E1LL, 0x7365727631LL, 0x7474747474LL };
  uint8_t addresses[][6] = {"1Node","2Node"};
   
  char receivePayload[32];
  unsigned long timeId;
  long addressTimeId;

  unsigned int messageId;
  int addressMessageId;

  const int SIZEBLACKLIST = 50;
  unsigned long blacklist[SIZEBLACKLIST];
  int listPointer = 0;
  

  //Sensor Daten
  struct sensorData{
    unsigned  int id;
    float value;
    int unit;
    unsigned long timeId;
  };

  //Mesh Daten Paket
  struct dataPacket{
    unsigned int destinationAddr;
    unsigned int originAddr;
    unsigned int lastHopAddr;
    unsigned int messageId;
    sensorData data; 
  };

  long startTime; // millis-Wert beim ersten Drücken der Taste
  long duration;  // Variable für die Dauer
  int countTimer = 0;

  boolean interruptHappened;

  
void setup() {  
  Serial.begin(9600);  
/*
  EEPROMWriteInt(12, 1000);
  delay(500);
*/
  interruptHappened = false; 
  
  arduinoId = EEPROMReadInt(12);
  delay(500);

  
  //DHT22
  dht.begin();


  Serial.println("init Radio");
  //nRF24L01

  initRadio();
  
  //EEPROM komplett loeschen
  /*
   for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0);
   } 
   */

 
  
  Serial.print(radio.getChannel());
  
  addressTimeId = 1;
  timeId = EEPROMReadlong(addressTimeId);
  delay(1000);

  Serial.print("timeId ID: \t");
  Serial.println(timeId);
  addressMessageId = 8;
 // addressMessageId += sizeof(addressMessageId);
  messageId = (int) EEPROMReadInt(addressMessageId);
  Serial.print("Message ID: \t");
  Serial.println(messageId);
  
  collectAndSendSensorData();
  radio.startListening();

  // initialize timer1 
  noInterrupts();           // disable all interrupts
  TCCR1A = 0;
  TCCR1B = 0;

  TCNT1 = 0;            // preload timer 65536-16MHz/256/2Hz
  TCCR1B |= (1 << CS12);    // 256 prescaler 
  TCCR1B |= (1 << CS10); 
  TIMSK1 |= (1 << TOIE1);   // enable timer overflow interrupt
  interrupts();             // enable all interrupts
    
}


ISR(TIMER1_OVF_vect)          // timer compare interrupt service routine
{
  if(countTimer <= 6){
    TCNT1 = 0; 
    countTimer++;
  }else{
    Serial.print("INTERRUPT: ");
    duration = millis() - startTime;
    Serial.println(duration);
    
    TCNT1 = 0; 
    countTimer = 0;  
    interruptHappened = true;
    startTime = millis(); 
    
  }
}

void loop() {
    dataPacket t_DataPacket_Loop;
    
   Serial.println("Anfang Loop");
   if(interruptHappened){
    //radio.stopListening();
    radio.openWritingPipe(addresses[0]);
  radio.openReadingPipe(1,addresses[0]); //1
    collectAndSendSensorData();
    interruptHappened = false;
   }
    
    while( radio.available()){
      radio.read( &t_DataPacket_Loop, sizeof(dataPacket) );
      Serial.print("Paket erhalten: \t");
      ausgabeDataPacket(t_DataPacket_Loop);
      Serial.println("2");
      processData(t_DataPacket_Loop);
      Serial.println("3");     
    }
    Serial.println("Ende Loop");
   
    

   
}

void collectAndSendSensorData(){
  radio.stopListening();
  // radio.closeReadingPipe(1);
  delay(500);
  //radio.openWritingPipe(pipes[0]);
 // radio.openReadingPipe(1,pipes[1]); //1
  timeId++;
  EEPROMWritelong(addressTimeId, timeId);
  
  getTemperatureHumidty();
  sendDataPacket(createSensorDataPacket(temperatur, 1));
  
  //radio.closeReadingPipe(1);
  //radio.openWritingPipe(pipes[1]); //1
 // radio.openReadingPipe(1,pipes[0]);
  delay(500);
  radio.startListening();
}

void getTemperatureHumidty(){  
  humidity = dht.readHumidity(); 
  temperatur = dht.readTemperature();  
}

dataPacket createSensorDataPacket(float value, int unit){

  //Sensor Daten uebernehmen
  sensorData t_sensorData;

  t_sensorData.timeId = timeId;
  t_sensorData.id = arduinoId;
  t_sensorData.value = value;
  t_sensorData.unit = unit;

  //Packet erstellen
  dataPacket t_dataPacket;
  t_dataPacket.destinationAddr = 1;
  t_dataPacket.originAddr = arduinoId;
  messageId++;
  EEPROMWriteInt(addressMessageId, messageId);
  t_dataPacket.messageId = messageId;
  t_dataPacket.lastHopAddr = 0;
  t_dataPacket.data = t_sensorData; 
  
  return t_dataPacket;
}

void sendDataPacket(dataPacket t_dataPacket){
  Serial.print("Paket senden: \t");
  ausgabeDataPacket(t_dataPacket);
  Serial.println("8");

  //initRadio();
  radio.SensorknotenIoT_resetRegister();
  printf_begin();
  radio.printDetails();
  

  Serial.println("8.1.1");
  radio.write( &t_dataPacket, sizeof(t_dataPacket));
  delay(50);
    Serial.println("8.1");
  for(int retry = 0; retry <= 40; retry++){
    Serial.println("8.2");
    //radio.write( &t_dataPacket, sizeof(t_dataPacket));
    radio.reUseTX();
   Serial.println("9");
   delayMicroseconds(130);
   Serial.println("10");
  }  
}

void processData(dataPacket t_dataPacket){
  if(!(t_dataPacket.destinationAddr == arduinoId)){
     Serial.println("4"); 
     unsigned long uniqueMessageId = (unsigned long) ((unsigned long) t_dataPacket.originAddr << 16) +  (unsigned long) t_dataPacket.messageId;
     
     if(!compareToList(uniqueMessageId)){
          Serial.println("5");
         insertInList(uniqueMessageId);
         Serial.println("6");
         t_dataPacket.lastHopAddr = arduinoId;
         sendDataPacket(t_dataPacket);
         Serial.println("7");
     }else{
        //Message verwefen
        Serial.println("Message verworfen");
     }    
  }else{
    //Arduino ist Empfaenger
    Serial.print("Arduino ist Empfaenger: \t");
    ausgabeDataPacket(t_dataPacket);
  }
}

void decrementPoiter() {
    if(listPointer < SIZEBLACKLIST )
        listPointer++;
    else
        listPointer=0;       
}

boolean compareToList(unsigned long uniqueMessageId){
   for (int i=0; i<  (SIZEBLACKLIST - 1); i++) {
        if (uniqueMessageId==blacklist[i])
            return true;
    }
  return false;
  
}
void insertInList(unsigned long uniqueMessageId){
    blacklist[listPointer]= uniqueMessageId;
    decrementPoiter();
}


void EEPROMWritelong(int address, long value){
      //Decomposition from a long to 4 bytes by using bitshift.
      //One = Most significant -> Four = Least significant byte
      byte four = (value & 0xFF);
      byte three = ((value >> 8) & 0xFF);
      byte two = ((value >> 16) & 0xFF);
      byte one = ((value >> 24) & 0xFF);

      //Write the 4 bytes into the eeprom memory.
      EEPROM.write(address, four);
      EEPROM.write(address + 1, three);
      EEPROM.write(address + 2, two);
      EEPROM.write(address + 3, one);
}

long EEPROMReadlong(int address)
{
      //Read the 4 bytes from the eeprom memory.
      long four = EEPROM.read(address);
      long three = EEPROM.read(address + 1);
      long two = EEPROM.read(address + 2);
      long one = EEPROM.read(address + 3);

      //Return the recomposed long by using bitshift.
      return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}
void EEPROMWriteInt(int address, int value){
      //Decomposition from a long to 4 bytes by using bitshift.
      //One = Most significant -> Four = Least significant byte
      byte two = (value & 0xFF);
      byte one = ((value >> 8) & 0xFF);

      //Write the 4 bytes into the eeprom memory.
      EEPROM.write(address, two);
      EEPROM.write(address + 1, one);
}

int EEPROMReadInt(int address)
{
      //Read the 4 bytes from the eeprom memory.
      long four = EEPROM.read(address);
      long three = EEPROM.read(address + 1);

      //Return the recomposed long by using bitshift.
      return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF);
}



void serielleAusgabe(){
    Serial.print("Luftfeuchte: ");
    Serial.print(humidity);
    Serial.print(" %\t");
    Serial.print("Temperatur: ");
    Serial.print(temperatur);
    Serial.print(" C\t");
    Serial.print("Pressure: ");
    Serial.print(pressure);
    Serial.print(" hPa\t");
    Serial.print(" TimeId: ");
    Serial.println(timeId);
    
}


void ausgabeSensorData(sensorData t_sensorData){
        Serial.print("ID: ");
        Serial.print(t_sensorData.id);
        Serial.print(" \t");
        Serial.print("Value: ");
        Serial.print(t_sensorData.value);
        Serial.print(" \t");
        Serial.print("Unit: ");
        Serial.print(t_sensorData.unit);// Get the payload
        Serial.print(" \t");
        Serial.print("TimeId: ");
        Serial.println(t_sensorData.timeId);// Get the payload
}
void ausgabeDataPacket(dataPacket t_dataPacket){
    Serial.print("Destination Addr: ");
    Serial.print(t_dataPacket.destinationAddr);
    Serial.print(" \t");
    Serial.print("Origin Addr: ");
    Serial.print(t_dataPacket.originAddr);
    Serial.print(" \t");
    Serial.print("Last Hop Addr: ");
    Serial.print(t_dataPacket.lastHopAddr);// Get the payload
    Serial.print(" \t");
    Serial.print("MessageID: ");
    Serial.print(t_dataPacket.messageId);// Get the payload
    Serial.print(" \t");
    ausgabeSensorData(t_dataPacket.data);
}
 void initRadio(){
      radio.begin();
      radio.setPayloadSize(sizeof(dataPacket));  //Groesse der gesendeten Daten
      radio.enableDynamicPayloads();
      radio.setAutoAck(true); 
      radio.setDataRate(RF24_250KBPS); //250kbs
      radio.setPALevel(RF24_PA_HIGH);
      radio.setChannel(90);
      radio.setRetries(15,15);
      radio.setCRCLength(RF24_CRC_16);
      radio.openWritingPipe(addresses[0]);
      radio.openReadingPipe(1,addresses[0]); //1
  }










