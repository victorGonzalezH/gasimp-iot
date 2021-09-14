#include <EEPROM.h>
#define EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS 13
#define EEPROM_LOGGING_ADDRESS_OFFSET 80
void setup() {
  // put your setup code here, to run once:
  EEPROM.begin(80);
  EEPROM.put(0, false);
  EEPROM.put(EEPROM_ADDRESS_LOGGING_EEPROM_ADDRESS, EEPROM_LOGGING_ADDRESS_OFFSET);
  EEPROM.commit();
  EEPROM.end();

}

void loop() {
  // put your main code here, to run repeatedly:

}
