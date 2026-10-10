// Makes GPIO15 switch between 3.3V and 0V every second.
// Use a multimeter on the pin to check you are on the real GPIO15.
void setup() { pinMode(15, OUTPUT); }
void loop()  { digitalWrite(15, HIGH); delay(1000); digitalWrite(15, LOW); delay(1000); }
