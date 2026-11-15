#include <Arduino.h>
#include <string.h>
#include <stddef.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>

IPAddress hostIP(192,168,1,129);
char ssid[32+1] = "*****";
char wifiPwd[64+1] = "*****";

typedef enum {
    USERDATA_MSG,
    WIFI_MSG,
    FILES_MSG,
    //UPDATE_MSG,
    UNKNOWN_MSG,
    DATETIME_MSG
} msg;

msg msgToken;

char buffer[600+1];
char readByte = 0;
int cont = 0;
char datetimeBuffer[50+1];
bool generateFiles = true;

char temp[10+1];

int state;
int hq;
int department;
char username[32+1];
char username2[32+1];
char surname[32+1];
char surname2[32+1];
char surname3[32+1];
int jobpos;
int userID;
int isAdmin;

char email[64+1];
char logs[512+1];
String payload;

const unsigned long WIFI_CHECK_PERIOD_MS = 5000;
unsigned long lastWifiCheck = 0;

const char * statusToString(wl_status_t status);
void testingPost(const char * string);
void testingWifi(const char * string);
const char * wifiConnection();
const char * wifiStatusCheck();
const char * updateWifiConnection();
void readUART();
int getTagValuesFromSTM(const char * string, const char * tag, char * out, size_t out_size);
void postWorkerAdminMessage();
void postGenerateFilesMessage();
//void postUpdateMailMessage();
msg checkTokens(const char * string);
int sendAndReceiveData(msg message);