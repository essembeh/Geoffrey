#include <EEPROM.h>
#include "GPRS_Shield_Arduino.h"

/*******************************************************************************
   Configuration
*/
#define PIN_NUMBER ""
#define ADMIN_NUMBER "+33XXXXXXXXX"
#define SMS_DISABLE false
#define SMS_NOTIFY_ADMIN true
#define SMS_NOTIFY_CALLER true
#define SERIAL_COMMANDS_ENABLE true
#define GATE_OPEN_DURATION 60000

/*******************************************************************************
   Constants
*/
#define EEPROM_HEADER "Geoffrey v1.0"
#define EEPROM_OFFSET 20
#define ALIAS_LEN 10
#define NUMBER_LEN 16
#define SMS_LEN 160
#define RELAY_PIN 6

/*******************************************************************************
   Messages for translations
*/
#define MSG_1 "Le portail est connecte"
#define MSG_2 "Je viens d'ouvrir le portail"
#define MSG_3 "Je viens d'ouvrir le portail pour %s"
#define MSG_4 "Je viens de fermer le portail"
#define MSG_5 "J'ai ajoute %s, son index est le %d"
#define MSG_6 "J'ai supprime l'utilisateur a l'index %d"
#define MSG_7 "[%d] %s: %s"
#define MSG_8 "%d utilisateur(s) trouve(s)"
#define ACTION_1 "lister"
#define ACTION_2 "ajouter "
#define ACTION_3 "supprimer "

/*******************************************************************************
   Struct
*/
struct User {
  boolean enabled;
  char alias[ALIAS_LEN];
  char number[NUMBER_LEN];
};


/*******************************************************************************
   Global variables
*/
GPRS phone(7, 8, 9600);
User user;
char sms[SMS_LEN];
char caller[NUMBER_LEN];
char date[24];

/*******************************************************************************
   Macros
*/
#define INDEX_TO_ADDRESS(i) EEPROM_OFFSET + i * sizeof(User)
#define USER_MAX (EEPROM.length() - EEPROM_OFFSET ) / sizeof(User)

/*******************************************************************************
   Check str starts with prefix ignoring the case
*/
bool str_starts_with_ignore_case(const char* str, const char* prefix) {
  if (strlen(prefix) > strlen(str)) {
    return false;
  }
  for (size_t i = 0; i < strlen(prefix); i++) {
    if (tolower(str[i]) != tolower(prefix[i])) {
      return false;
    }
  }
  return true;
}

/*******************************************************************************
   Function to blink the embedded LED
*/
void blink(const char* sequence) {
  for (size_t i = 0; i < strlen(sequence); i++) {
    if (sequence[i] == '.') {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
    } else if (sequence[i] == '_' || sequence[i] == '-') {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(500);
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(500);
      digitalWrite(LED_BUILTIN, LOW);
    }
    delay(100);
  }
}

/*******************************************************************************
   Checks if the number is the admin
*/
boolean is_admin(const char* number) {
  return strcmp(number, ADMIN_NUMBER) == 0;
}

/*******************************************************************************
   Return the user
*/
boolean find_user(const char* number) {
  clear_user();
  for (int i = 0; i < USER_MAX; i++) {
    EEPROM.get(INDEX_TO_ADDRESS(i), user);
    if (user.enabled && strcmp(user.number, number) == 0) {
      return true;
    }
  }
  clear_user();
  return false;
}

/*******************************************************************************
   Checks if EEPROM is initialized
*/
boolean is_eeprom_initialized() {
  for (char i = 0; i < strlen(EEPROM_HEADER); i++) {
    if (EEPROM.read(i) != EEPROM_HEADER[i]) {
      return false;
    }
  }
  return true;
}

/*******************************************************************************
   Empty a user
*/
void clear_user() {
  user.enabled = false;
  memset(user.alias, '\0', ALIAS_LEN);
  memset(user.number, '\0', NUMBER_LEN);
}

/*******************************************************************************
   Initialize the EEPROM
*/
void eeprom_initialize() {
  Serial.println("[EEPROM]  Initialize");
  for (char i = 0; i < strlen(EEPROM_HEADER); i++) {
    EEPROM.write(i, EEPROM_HEADER[i]);
  }
  for (char i = strlen(EEPROM_HEADER); i < EEPROM_OFFSET; i++) {
    EEPROM.write(i, 0);
  }
  clear_user();
  for (int i = 0; i < USER_MAX; i++) {
    EEPROM.put(INDEX_TO_ADDRESS(i), user);
  }
}

/*******************************************************************************
   Open the relay dor given ms.
*/
bool config_remove_user(int index) {
  if (index >= 0 && index < USER_MAX) {
    EEPROM.get(INDEX_TO_ADDRESS(index), user);
    if (user.enabled) {
      clear_user();
      EEPROM.put(INDEX_TO_ADDRESS(index), user);
      return true;
    }
  }
  return false;
}

/*******************************************************************************
   Open the relay dor given ms.
*/
int config_add_user(const char* alias, const char* number) {
  if (strlen(alias) < 1 || strlen(number) < 10) {
    return -1;
  }
  for (size_t i = 0; i < strlen(alias); i++) {
    if (!isAlphaNumeric(alias[i])) {
      return -1;
    }
  }
  if (number[0] != '+') {
    return -1;
  }
  for (size_t i = 1; i < strlen(number); i++) {
    if (!isDigit(number[i])) {
      return -1;
    }
  }
  for (int i = 0; i < USER_MAX; i++) {
    EEPROM.get(INDEX_TO_ADDRESS(i), user);
    if (!user.enabled) {
      user.enabled = true;
      strcpy(user.alias, alias);
      strcpy(user.number, number);
      EEPROM.put(INDEX_TO_ADDRESS(i), user);
      clear_user();
      return i;
    }
  }
  return -2;
}

/*******************************************************************************
   Open the relay dor given ms.
*/
void open_relay(int duration) {
  digitalWrite(RELAY_PIN, LOW);
  delay(duration);
  digitalWrite(RELAY_PIN, HIGH);
}

/*******************************************************************************
   Return the role of the given number
*/
void send_sms(const char* number, const char* message) {
  Serial.print("[SMS]  Send to ");
  Serial.print(number);
  Serial.print(": ");
  Serial.print(message);
  if (SMS_DISABLE) {
    Serial.println(" -> SKIP");
  } else if (phone.sendSMS(number, message)) {
    Serial.println(" -> OK");
  } else {
    Serial.println(" -> ERROR");
  }
}

/*******************************************************************************
   Open the gate, close it after, send notifications
*/
void open_gate(const char* caller_number, User * puser) {
  open_relay(500);
  if (SMS_NOTIFY_CALLER) {
    send_sms(caller_number, MSG_2);
  }
  if (SMS_NOTIFY_ADMIN && puser != NULL) {
    sprintf(sms, MSG_3, puser->alias);
    send_sms(ADMIN_NUMBER, sms);
  }
  if (GATE_OPEN_DURATION > 0) {
    delay(GATE_OPEN_DURATION);
    open_relay(500);
    if (SMS_NOTIFY_ADMIN) {
      send_sms(ADMIN_NUMBER, MSG_4);
    }
  }
}

/*******************************************************************************
   Configuration
*/
void handle_config_command(const char* command) {
  if (str_starts_with_ignore_case(command, ACTION_1)) {
    int count = 0;
    for (int i = 0; i < USER_MAX; i++) {
      EEPROM.get(INDEX_TO_ADDRESS(i), user);
      if (user.enabled) {
        sprintf(sms, MSG_7, i, user.alias, user.number);
        send_sms(ADMIN_NUMBER, sms);
        count ++;
      }
    }
    sprintf(sms, MSG_8, count);
    send_sms(caller, sms);
  } else if (str_starts_with_ignore_case(command, ACTION_2)) {
    char alias[ALIAS_LEN];
    char number[NUMBER_LEN];
    memset(alias, '\0', ALIAS_LEN);
    memset(number, '\0', NUMBER_LEN);
    char step = 0;
    for (size_t i = strlen(ACTION_2); i < strlen(command); i++) {
      if (command[i] == ' ') {
        step++;
        continue;
      }
      switch (step) {
        case 0:
          if (strlen(alias) == sizeof(user.alias) - 1) {
            step = -1;
          } else {
            alias[strlen(alias)] = command[i];
          }
          break;
        case 1:
          if (strlen(number) == sizeof(user.number) - 1) {
            step = -1;
          } else {
            number[strlen(number)] = command[i];
          }
          break;
      }
      if (step < 0) {
        break;
      }
    }
    if (step > 0) {
      int index = config_add_user(alias, number);
      if (index >= 0) {
        sprintf(sms, MSG_5, alias, index);
        send_sms(ADMIN_NUMBER, sms);
      }
    }
  } else if (str_starts_with_ignore_case(command, ACTION_3)) {
    int index = -1;
    sscanf(&command[strlen(ACTION_3)], "%d", &index);
    if (config_remove_user(index)) {
      sprintf(sms, MSG_6, index);
      send_sms(ADMIN_NUMBER, sms);
    }
  }
}

/*******************************************************************************
   Setup the arduino embedded serial, LED and relay
*/
bool setup_io() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }
  delay(1000);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
}

/*******************************************************************************
   Initialize the EEPROM and read configuration
*/
void setup_eeprom() {
  if (!is_eeprom_initialized()) {
    eeprom_initialize();
  }
}

/*******************************************************************************
   Setup the GSM module
*/
bool setup_gsm() {
  phone.init();
  phone.checkPowerUp();
  while (!phone.isNetworkRegistered()) {
    Serial.println("[GSM]  Waiting for network");
    blink("._._._");
    delay(1000);
  }
  Serial.println("[GSM]  Ready");
}

/*******************************************************************************
   Setup
*/
void setup() {
  setup_io();
  setup_eeprom();
  setup_gsm();
  if (SMS_NOTIFY_ADMIN) {
    send_sms(ADMIN_NUMBER, MSG_1);
  }
  blink("..............");
}

/*******************************************************************************
   Loop
*/
void loop() {
  blink("_");

  if (phone.isCallActive(caller)) {
    phone.hangup();
    Serial.print("[CALL]  from ");
    Serial.println(caller);
    if (is_admin(caller)) {
      open_gate(caller, NULL);
    } else if (find_user(caller)) {
      open_gate(caller, &user);
    }
  }

  char smsId;
  while ((smsId = phone.isSMSunread()) > 0) {
    phone.readSMS(smsId, sms, sizeof(sms), caller, date);
    phone.deleteSMS(smsId);
    Serial.print("[SMS]  from ");
    Serial.println(caller);
    if (is_admin(caller)) {
      handle_config_command(sms);
    }
  }

  if (SERIAL_COMMANDS_ENABLE && Serial.available()) {
    String line = Serial.readString();
    line.trim();
    if (line.length() > 0) {
      handle_config_command(line.c_str());
    }
  }

  delay(1000);
}
