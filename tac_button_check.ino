const int buttonPin = 13;  // GPIO pin connected to the button

void setup() {
  Serial.begin(115200);  // Start serial communication
  pinMode(buttonPin, INPUT_PULLUP);  // Set the pin as input with internal pull-up resistor
}

void loop() {
  int buttonState = digitalRead(buttonPin);  // Read the button state

  if (buttonState == LOW) {  // Button is pressed
    Serial.println("on");
  } else {  // Button is not pressed
    Serial.println("off");
  }

  delay(100);  // Small delay to stabilize the reading
}