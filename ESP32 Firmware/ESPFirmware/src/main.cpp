#include "main.h"

void setup() {
  Serial1.begin(115200, SERIAL_8N1, 20, 21);

  //delay(15000);
  //Serial1.print("Hello World!");

  //wifiConnection();
  //Serial1.println(wifiConnection());
  //Serial1.println("Esperando datos...");
  //testingPost("STATE=1,HQ=1,DEPARTMENT=1,USERNAME=JUAN,USERNAME2=PEDRO,SURNAME=GARCIA,SURNAME2=LOPEZ,SURNAME3=MONTERO,JOBPOS=31,USERID=1,ISADMIN=0");
  //delay(5000);
  //testingWifi("SSID=*****,PWD=*****");
  //delay(5000);
  //testingPost("EMAIL=*****,LOGS=C001&121235;C002&131415;C003&161616");
  //delay(5000);
  //testingPost("U=1,STATE=1,HQ=1,DEPARTMENT=1,USERNAME=JUAN,USERNAME2=PEDRO,SURNAME=GARCIA,SURNAME2=LOPEZ,SURNAME3=MONTERO,JOBPOS=31,USERID=1,ISADMIN=0");
}

void loop() {
  if(Serial1.available()){
     readUART();
    if ((millis() - lastWifiCheck) >= WIFI_CHECK_PERIOD_MS)
    {
      lastWifiCheck = millis();
      wifiStatusCheck();
    }
  }
}

const char * statusToString(wl_status_t status){
  switch(status){
    case WL_NO_SHIELD:        return "WL_NO_SHIELD";
    case WL_IDLE_STATUS:      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:    return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:   return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:        return "WL_CONNECTED";
    case WL_CONNECT_FAILED:   return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:  return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:     return "WL_DISCONNECTED";
    default:                  return "WL_UNKNOWN";
  }
}

void testingPost(const char * string){
  Serial1.println("Connecting to Wifi... (SSID: "+String(ssid)+")");
  const char * status = wifiConnection();
  Serial1.println("Connection Status: "+ String(status));
  snprintf(buffer, sizeof(buffer), string);
  Serial1.println("Buffered data: " + String(buffer));
  msg message = checkTokens(buffer);    // 0 = USERDATA_MSG, ..., 3 = UPDATE_MSG
  Serial1.println("Token type: " + String(message));
  Serial1.println("Converting data to JSON...");
  if(sendAndReceiveData(message) == 0){
    Serial1.println("Data successfully sent and received from webserver. Now sending message to STM32...\n\n");
  }else{
    Serial1.println("Data could not be sent to STM32\n\n");
  }
}

void testingWifi(const char * string){
  const char * status = wifiConnection();
  Serial1.println("WIFI CONNECTION STATUS: "+String(status));
  Serial1.println("WIFI SSID: " + String(ssid));
  snprintf(buffer, sizeof(buffer), string);
  Serial1.println("Buffered data: " + String(buffer));
  msg message = checkTokens(buffer);    // 0 = USERDATA_MSG, ..., 3 = UPDATE_MSG
  Serial1.println("Token type: " + String(message));
  Serial1.println("Connecting to Wifi... (SSID: "+String(ssid)+")");
  status = wifiConnection();
  Serial1.println("WIFI CONNECTION STATUS: "+String(status));
  Serial1.println("Sending connection status to STM32...\n\n");
}

const char * wifiConnection(){
  // conectarse...
  // intentar conexión tres (3) veces
  wl_status_t status;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifiPwd);
  for(int i = 0; i<3;i++){
    delay(3500);
    status = WiFi.status();
    if(status == WL_CONNECTED){
      break;
    }
  }
  Serial1.printf("%s,\n",statusToString(status));
  return statusToString(status);
}

const char * wifiStatusCheck(){
  wl_status_t status = WiFi.status();
  if(status != WL_CONNECTED)
  {
    return wifiConnection();
  }
  Serial1.printf("%s,\n", statusToString(status));
  return statusToString(status);
}

const char * updateWifiConnection(){
  // parar conexion wifi
  WiFi.disconnect(true);
  delay(1500);
  // conectarse de nuevo
  return wifiConnection();
}

void readUART(){
  while (Serial1.available() > 0)
    {
    char c = (char)Serial1.read();

    if (c == '\r')
    {
        continue;
    }

    if (c == '\n')
    {
        if (cont > 0)
        {
            buffer[cont] = '\0';

            msgToken = checkTokens(buffer);
            sendAndReceiveData(msgToken);

            cont = 0;
            memset(buffer, 0, sizeof(buffer));
        }

        continue;
    }

    if (cont < (sizeof(buffer) - 1))
    {
        buffer[cont++] = c;
    }
    else
    {
        /* Overflow: descartar trama */
        cont = 0;
        memset(buffer, 0, sizeof(buffer));
    }
}
}

// "STATE=%s,HQ=%s,DEPARTMENT=%s,USERNAME=%s,USERNAME2=%s,SURNAME1=%s,SURNAME2=%s,SURNAME3=%s,JOBPOS=%s,USERID=%s,ISADMIN=%s"

int getTagValuesFromSTM(const char * string, const char * tag, char * out, size_t out_size){

  size_t tag_length = strlen(tag);
  const char * start = string;

  while(*start){
    if(strncmp(start, tag, tag_length) == 0 && start[tag_length] == '='){

      const char * value = start + tag_length + 1;
      char * end = strchr(value, ',');
      size_t len = end ? (size_t) (end-value) : strlen(value);

      if(len >= out_size){
        len = out_size - 1;
      }

      memcpy(out, value, len);
      out[len] = '\0';
      return 0;
    }
    start = strchr(start, ',');

    if(!start){
      break;
    }
    start++;
  }
  return -1;
}

void postWorkerAdminMessage(){
  HTTPClient http;
  String httpRequestData;

  // {state: %s, hq: %s, department: %s, username: %s, username2: %s, surname: %s, surname2: %s, surname3: %s, jobpos: %s, userid: %s, isadmin: %s, msgtype: 'USERDATA_MSG'}
  
  http.begin("http://"+hostIP.toString()+":8081/");
  http.addHeader("Content-Type", "application/json");

  httpRequestData =
  "{\"state\":\""+String(state)+
  "\",\"hq\":\""+String(hq)+
  "\",\"department\":\""+String(department)+
  "\",\"username\":\""+String(username)+
  "\",\"username2\":\""+String(username2)+
  "\",\"surname\":\""+String(surname)+
  "\",\"surname2\":\""+String(surname2)+
  "\",\"surname3\":\""+String(surname3)+
  "\",\"jobpos\":\""+String(jobpos)+
  "\",\"userid\":\""+String(userID)+
  "\",\"isadmin\":\""+String(isAdmin)+
  "\",\"msgtype\":\"USERDATA_MSG\"}";

  int code = http.POST(httpRequestData);
  payload = http.getString();
  //Serial1.println("HTTP code: " + String(code));
  //Serial1.println("Payload: " + payload); 
  http.end();
}

void postGenerateFilesMessage(){
  HTTPClient http;
  String httpRequestData;

  // {email: %s, logs: %s, msgtype: 'FILES_MSG'}
  
  http.begin("http://"+hostIP.toString()+":8081/");
  http.addHeader("Content-Type", "application/json");

  httpRequestData =
  "{\"email\":\""+String(email)+
  "\",\"logs\":\""+String(logs)+
  "\",\"msgtype\":\"FILES_MSG\"}";

  int code = http.POST(httpRequestData);
  payload = http.getString();
  //Serial1.println("HTTP code: " + String(code));
  //Serial1.println("Payload: " + payload);
  http.end();
}

// void postUpdateMailMessage(){
//   HTTPClient http;
//   String httpRequestData;

//   // {state: %s, hq: %s, department: %s, username: %s, username2: %s, surname: %s, surname2: %s, surname3: %s, jobpos: %s, userid: %s, isadmin: %s, msgtype: 'UPDATE_MSG'}
  
//   http.begin("http://"+hostIP.toString()+":8081/");
//   http.addHeader("Content-Type", "application/json");

//   httpRequestData =
//   "{\"state\":\""+String(state)+
//   "\",\"hq\":\""+String(hq)+
//   "\",\"department\":\""+String(department)+
//   "\",\"username\":\""+String(username)+
//   "\",\"username2\":\""+String(username2)+
//   "\",\"surname\":\""+String(surname)+
//   "\",\"surname2\":\""+String(surname2)+
//   "\",\"surname3\":\""+String(surname3)+
//   "\",\"jobpos\":\""+String(jobpos)+
//   "\",\"userid\":\""+String(userID)+
//   "\",\"isadmin\":\""+String(isAdmin)+
//   "\",\"msgtype\":\"UPDATE_MSG\"}";

//   int code = http.POST(httpRequestData);
//   payload = http.getString();
//   //Serial1.println("HTTP code: " + String(code));
//   //Serial1.println("Payload: " + payload);
//   http.end();
// }

msg checkTokens(const char * string){
  if(strncmp(string, "SSID=", 5) == 0){
    getTagValuesFromSTM(string, "SSID", ssid, sizeof(ssid));
    getTagValuesFromSTM(string, "PWD", wifiPwd, sizeof(wifiPwd));
    return WIFI_MSG;
  }
  if(strncmp(string, "STATE=", 6) == 0){
    getTagValuesFromSTM(string, "STATE", temp, sizeof(temp));
    state = atoi(temp);
    getTagValuesFromSTM(string, "HQ", temp, sizeof(temp));
    hq = atoi(temp);
    getTagValuesFromSTM(string, "DEPARTMENT", temp, sizeof(temp));
    department = atoi(temp);
    getTagValuesFromSTM(string, "USERNAME", username, sizeof(username));
    getTagValuesFromSTM(string, "USERNAME2", username2, sizeof(username2));
    getTagValuesFromSTM(string, "SURNAME1", surname, sizeof(surname));
    getTagValuesFromSTM(string, "SURNAME2", surname2, sizeof(surname2));
    getTagValuesFromSTM(string, "SURNAME3", surname3, sizeof(surname3));
    getTagValuesFromSTM(string, "JOBPOS", temp, sizeof(temp));
    jobpos = atoi(temp);
    getTagValuesFromSTM(string, "USERID", temp, sizeof(temp));
    userID = atoi(temp);
    getTagValuesFromSTM(string, "ISADMIN", temp, sizeof(temp));
    isAdmin = atoi(temp);
    return USERDATA_MSG;
  }
  // else if(strncmp(string, "U=", 2) == 0){
  //   getTagValuesFromSTM(string, "STATE", temp, sizeof(temp));
  //   state = atoi(temp);
  //   getTagValuesFromSTM(string, "HQ", temp, sizeof(temp));
  //   hq = atoi(temp);
  //   getTagValuesFromSTM(string, "DEPARTMENT", temp, sizeof(temp));
  //   department = atoi(temp);
  //   getTagValuesFromSTM(string, "USERNAME", username, sizeof(username));
  //   getTagValuesFromSTM(string, "USERNAME2", username2, sizeof(username2));
  //   getTagValuesFromSTM(string, "SURNAME", surname, sizeof(surname));
  //   getTagValuesFromSTM(string, "SURNAME2", surname2, sizeof(surname2));
  //   getTagValuesFromSTM(string, "SURNAME3", surname3, sizeof(surname3));
  //   getTagValuesFromSTM(string, "JOBPOS", temp, sizeof(temp));
  //   jobpos = atoi(temp);
  //   getTagValuesFromSTM(string, "USERID", temp, sizeof(temp));
  //   userID = atoi(temp);
  //   getTagValuesFromSTM(string, "ISADMIN", temp, sizeof(temp));
  //   isAdmin = atoi(temp);
  //   return UPDATE_MSG;
  // }
  else if(strncmp(string, "EMAIL=", 6) == 0){
    getTagValuesFromSTM(string, "EMAIL", email, sizeof(email));
    getTagValuesFromSTM(string, "LOGS", logs, sizeof(logs));
    return FILES_MSG;
  }
  else if(strncmp(string, "DATETIME", 8) == 0){
  
  time_t rawtime = time(NULL);
  configTime(0, 0, "pool.ntp.org"); 

  while(rawtime<100000){
    delay(500);
    rawtime = time(NULL);
  }
    struct tm* timeInfo = localtime(&rawtime);
    String wday = "";
    switch (timeInfo->tm_wday)
    {
    case 0:
      wday = "Domingo"; break;
    case 1:
      wday = "Lunes"; break;
    case 2:
      wday = "Martes"; break;
    case 3:
      wday = "Miercoles"; break;
    case 4:
      wday = "Jueves"; break;
    case 5:
      wday = "Viernes"; break;
    case 6:
      wday = "Sabado"; break;
    default:
      wday = "Domingo"; break;
    }

    Serial1.printf("SUCCESS,%04d%02d%02d-%02d%02d%02d-%s\n", timeInfo->tm_year+1900, timeInfo->tm_mon+1, timeInfo->tm_mday, timeInfo->tm_hour+2, timeInfo->tm_min, timeInfo->tm_sec, wday);
    return DATETIME_MSG;
  }
  return UNKNOWN_MSG;
}

int sendAndReceiveData(msg message){
  switch(message){
    case(0): 
      postWorkerAdminMessage(); 
      break;
    case(1): 
      updateWifiConnection();   
      return 0;
    case(2):  
      postGenerateFilesMessage();  
      break;
    // case(3): 
    //   postUpdateMailMessage();
    //   break;
    default: return 1;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if(doc["error"].is<const char *>()){
    Serial1.println("ERROR,400");
    return 1;
  }else if(doc["success"].is<const char *>()){
    if(doc["email"].is<const char *>()){
      const char * email = doc["email"].as<const char *>();
      Serial1.println("SUCCESS,200," + String(email));
      return 0;
    }
    Serial1.println("SUCCESS,200");
    return 0;
  }else{
    Serial1.println("ERROR,500");
    return 1;
  }
}