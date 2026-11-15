#include <Crypto.h>
#include <AES.h>
#include <CBC.h>
#include <string.h>
#include <stdio.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

#define PN532_SCK  (2)
#define PN532_MOSI (3)
#define PN532_SS   (4)
#define PN532_MISO (5)

#define TEST "JustATestingMessage"
#define AES_BYTES 16
#define STRING_LENGTH 256
#define CYPHER_LENGHT 256

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

uint8_t key[AES_BYTES] = {0xd,0x2,0xd,0xf,0x2,0x7,0xf,0x8,
                     0x4,0xf,0x3,0xa,0xf,0xc,0x6,0xe};
uint8_t iv[AES_BYTES];
char generatedString[STRING_LENGTH];
uint8_t cypher[CYPHER_LENGHT];
size_t totalLength;
CBC<AES128> aes;

void stringGeneratorWorkerAdmin(char state[], char hq[], char department[], char userName[], char userName2[], char surname1[], char surname2[], char surname3[], char jobPos[], char userID[], char isAdmin[]){
  snprintf(generatedString, STRING_LENGTH, "STATE=%s,HQ=%s,DEPARTMENT=%s,USERNAME=%s,USERNAME2=%s,SURNAME1=%s,SURNAME2=%s,SURNAME3=%s,JOBPOS=%s,USERID=%s,ISADMIN=%s", state, hq, department, userName, userName2, surname1, surname2, surname3, jobPos, userID, isAdmin);
  Serial.println(generatedString);
}

void stringGeneratorWifi(char ssid[], char pwd[]){
  snprintf(generatedString, STRING_LENGTH, "SSID=%s,PWD=%s", ssid, pwd);
  Serial.println(generatedString);
}

void testing(char text[]){
  for(int i=0; i < sizeof(text); i++){
      Serial.print("Character ");
      Serial.print(i+1);
      Serial.print(": ");
      Serial.println(text[i]);
  }
}

size_t addPKCS7(uint8_t *buffer, size_t dataLength){
  size_t padding = AES_BYTES - (dataLength%AES_BYTES);
  for(int i=0; i<padding; i++){
    buffer[dataLength+i] = padding;
  }
  return dataLength+padding;
}

void randomIv(uint8_t *iv, size_t length){
  for(int i=0; i<length;i++){
    iv[i]=random(0,256);
  }
}

void printHex(const uint8_t *data, size_t lenght){
  for(int i = 0; i<lenght;i++){
    if(data[i] < 0x10){
      Serial.print("0");
    }
    Serial.print(data[i], HEX);
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  aes.setKey(key, sizeof(key));
  randomIv(iv, AES_BYTES);
  aes.setIV(iv, AES_BYTES);

  //testing(TEST);

  Serial.println("Formatting input text to cypher =======");
  Serial.print(">> ");
  stringGeneratorWorkerAdmin("state", "hq", "dp", "name", "name2", "sn1", "sn2", "sn3", "jpos", "userId", "isAdmin");
  //stringGeneratorWifi("ssid", "pwd");

  memset(cypher, 0, CYPHER_LENGHT);
  memcpy(cypher, generatedString, strlen(generatedString));
  totalLength = addPKCS7(cypher, strlen(generatedString));
  aes.encrypt(cypher, cypher, totalLength);

  Serial.println("\nCyphered string =======");
  Serial.print(">> ");
  printHex(cypher, totalLength);
  Serial.println();

  Serial.println("\nSending data to NFC Card =======");

  nfc.begin();
  nfc.setPassiveActivationRetries(0xFF);
  nfc.SAMConfig();

  Serial.println("Waiting for an NFC card... (v2)");
}

void loop() {
  // put your main code here, to run repeatedly:

  uint8_t success;
  uint8_t uid[] = {0,0,0,0,0,0,0};
  uint8_t uidLength;
  uint8_t dataToWrite[16];
  int blocks = 1 + totalLength/4 + sizeof(iv)/4;

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  if(success){
    Serial.println(">> Authenticating blocks 4~39 with KEYA key...");
    uint8_t keya[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    Serial.print("\n>> Writting IV");
    Serial.print(">> ");
    printHex(iv, 16);
    Serial.println();

    for(int addr=0; addr< blocks;addr++){

      success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, addr+4, 0, keya);
      if(success){
        Serial.print("\n>> Succesfully authenticated block ");
        Serial.println(addr+4);

        memset(dataToWrite, 0, 16);
        if(addr == 0){
          dataToWrite[0] = (totalLength >> 8) & 0xFF;
          dataToWrite[1] = totalLength & 0xFF;
          dataToWrite[2] = 0x00;
          dataToWrite[3] = 0x00;
        }else if(addr >= 1 && addr <= 4){
          memcpy(dataToWrite, &iv[(addr-1)*4],4);
        } else{
          memcpy(dataToWrite, &cypher[(addr-5)*4],4);
        }
        success = nfc.mifareclassic_WriteDataBlock(addr+4, dataToWrite);

        if(success){
          Serial.print(">> Data written in block ");
          Serial.print(addr+4);
          Serial.print(" ( ");
          for(int j=0; j< sizeof(dataToWrite); j++){
            Serial.print("0x");
            printHex(&dataToWrite[j], 1);
            Serial.print(" ");
          }
          Serial.println(")");
          delay(50);
        } else{
          Serial.println("Failed to write NFC Card");
          break;
        }
      }
    }
    Serial.println("NFC Successfully written");
  }
  delay(2500);
}
