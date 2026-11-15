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
uint8_t decypher[CYPHER_LENGHT];
char decypheredText[STRING_LENGTH];
size_t totalLength = 0;
CBC<AES128> aes;

size_t removePKCS7(uint8_t *buffer, size_t dataLength){
  uint8_t padding = buffer[dataLength-1];
  if(padding == 0 || padding > AES_BYTES) return 0;
  for(int i=0; i<padding; i++){
    if(buffer[dataLength-1-i] != padding){
      return 0;
    }
  }
  return dataLength-padding;
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
    Serial.print(" ");
  }
}

void setup(){
  Serial.begin(115200);
  aes.setKey(key, sizeof(key));

  Serial.println("Reading NFC Card =======");
  nfc.begin();
  nfc.setPassiveActivationRetries(0xFF);
  nfc.SAMConfig();
  Serial.println("Waiting for an NFC card...");

}

void loop(){

  uint8_t success;
  uint8_t uid[] = {0,0,0,0,0,0,0};
  uint8_t uidLength;
  uint8_t dataToRead[16];
  uint8_t keya[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  if(success){
    Serial.println(">> Authenticating blocks 4~39 with KEYA key...");
    success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya);
    success = nfc.mifareclassic_ReadDataBlock(4, dataToRead);
    totalLength = ((size_t)dataToRead[0] << 8) | dataToRead[1];

    int blocks = 1 + sizeof(iv)/4 + totalLength/4;
    for(int addr=0; addr< blocks;addr++){
      uint8_t blockdata[16] = {0};
      uint8_t bNum = addr+4;
      success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, bNum, 0, keya);
      if(success){
        success = nfc.mifareclassic_ReadDataBlock(bNum, blockdata);
        Serial.print("\n>> Succesfully authenticated block ");
        Serial.println(addr);
        if(success){

          if(addr >= 1 && addr <= 4){
            memcpy(&iv[(addr-1)*4], blockdata,4);
          }else{
            memcpy(&decypher[(addr-5)*4], blockdata,4);
          }
          Serial.print(">> Data Read from Block ");
          Serial.println(bNum);
          Serial.print(" (");

          for(int i = 0; i< sizeof(blockdata);i++){
            Serial.print("0x");
            printHex(&blockdata[i], 1);
            Serial.print(" ");
          }
          Serial.println(")");
        }else{
          Serial.println("Failed to Read NFC Card");
          delay(2500);
          return;
        }
      }else{
        Serial.print("Failed to authenticate block ");
        Serial.println(bNum);
        delay(2500);
        return;
      }
    }
    aes.clear();
    aes.setKey(key, sizeof(key));
    aes.setIV(iv, sizeof(iv));
    aes.decrypt(decypher, decypher, totalLength);
    size_t length = removePKCS7(decypher, totalLength);
    memcpy(decypheredText, decypher, length);
    decypheredText[length] = '\0';

    Serial.println("\nIV Read =======");
    Serial.print(">> ");
    printHex(iv, 16);
    Serial.println();

    Serial.println("\nDecyphered string =======");
    Serial.print(">> ");
    Serial.println(decypheredText);
  }
  delay(2500);
}