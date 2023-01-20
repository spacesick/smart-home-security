#include <ArduinoJson.h>                // For JSON serialization
#include <ArduinoJson.hpp>              // 

#include <Arduino_FreeRTOS.h>           // For RTOS
#include <timers.h>                     //

#include <Keypad.h>                     // For 4x4 matrix keypad

#include <Servo.h>                      // For servo

// Output pin mappings for sensors and actuators
const int pingPin = 10;
const int servoPin = 11;
const int buzzerPin = 12;

// Door locking mechanism
bool locked = true;
char key;                               // Store the pressed key
static String enteredPassword = "";     // Store the entered password
const String keypad_password = "1234";  // Actual password

// Servo object
Servo myservo;    
const int unlockedPos = 90;             // Angle of 90 degrees
const int lockedPos = 0;                // Angle of 0 degrees

// Pins for the 4x4 matrix keypad
const byte ROWS = 4; 
const byte COLS = 4; 
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9,8,7,6};         // Connect to the row pinouts of the keypad
byte colPins[COLS] = {5,4,3,2};         // Connect to the column pinouts of the keypad

// Keypad object
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Store measurements from the ultrasonic sensor
long prev_cm = 0;
long duration, inches, cm;

// RTOS timer and tasks
TimerHandle_t pingTimer;
TaskHandle_t taskSendHandler;
TaskHandle_t taskPingHandler;
TaskHandle_t taskKeypadHandler;
TaskHandle_t taskBeepHandler;

// JSON document to send to ESP8266
StaticJsonDocument<200> doc;
char out[128];

/**
  Converts time measurement in microseconds to distance in centimeters.

  @param microSeconds time in microseconds
  @return distance in centimeters
*/
long microsecondsToCentimeters(long microseconds) {
  // The speed of sound is 340 m/s or 29 microseconds per centimeter.
  // The ping travels out and back, so to find the distance of the object we
  // take half of the distance travelled.
  return microseconds / 29 / 2;
}

/**
  Make the PING))) ultrasonic sensor emit and receive sound signals to detect
  movement of nearby objects. Sound the buzzer if the sensor detects
  something close.

  This task controls the ultrasonic sensor and the buzzer. It is controlled by a
  timer and triggered at 20ms intervals.
*/
void taskPing(void *pvParameters) {
    // Turn the buzzer off
    digitalWrite(buzzerPin, LOW);

    // The PING))) is triggered by a HIGH pulse of 2 or more microseconds.
    // Give a short LOW pulse beforehand to ensure a clean HIGH pulse:
    pinMode(pingPin, OUTPUT);
    digitalWrite(pingPin, LOW);
    delayMicroseconds(2);
    digitalWrite(pingPin, HIGH);
    delayMicroseconds(5);
    digitalWrite(pingPin, LOW);

    // The same pin is used to read the signal from the PING))), a HIGH pulse
    // whose duration is the time (in microseconds) from the sending of the ping
    // to the reception of its echo off of an object.
    pinMode(pingPin, INPUT);
    duration = pulseIn(pingPin, HIGH);

    // Convert the time into distance in centimeters
    cm = microsecondsToCentimeters(duration);

    // Check the measured distance
    if (locked) {
      if (cm > 20) {
        doc["status"] = "safe";
      } 
      else if (cm > 5) {
        doc["status"] = "warn";
      } 
      else {
        doc["status"] = "danger";

        // Run task to sound the buzzer
        vTaskResume(taskBeepHandler);

        // Stop the timer to let taskBeep finish its task
        xTimerStop(pingTimer, 0);
      }
    } 
    else {
      doc["status"] = "unlocked";
    }

    doc["dist"] = cm;

    // Output the serialized JSON document to out
    serializeJson(doc, out);

    // Unblock taskSend
    xTaskNotifyGive(taskSendHandler);
}

/**
  Sends the serialized JSON document to ESP8266 via USART.

  This task runs as soon taskPing is done doing its task.
*/
void taskSend(void *pvParameters) {
  while (true) {
    // Wait for taskPing to unblock
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Send through the TX pin
    Serial.println(out);
  }
}

/**
  Read entered password from the keypad and verifies it. Unlock/lock the door
  based on input to the keypad. Sound the buzzer if the user enters a wrong
  password.

  This task controls the keypad, the servo, and the buzzer.
*/
void taskKeypad(void *pvParameters) {
  // This task will run indefinitely, performing some task every 2000 milliseconds
  while (true) {
    key = keypad.getKey();

    if (!locked) {
      if (key == 'D') {
        // Lock the door when the user inputs 'D'
        locked = true;
        myservo.attach(servoPin);
        myservo.write(lockedPos);
      }
    } 
    else {  
      // Debug function to automatically unlock the door
      // if (key == 'A') {
      //   locked = false;
      //   myservo.attach(servoPin);
      //   myservo.write(unlockedPos);
      // }
      if (key != NO_KEY) {
        // A key was pressed, add the key to the entered password as char
        enteredPassword += key;

        // Check the entered password length
        if (enteredPassword.length() == keypad_password.length()) {
          // Password is complete, verify the password
          if (enteredPassword == keypad_password) {
            // Correct password entered, unlock the door
            locked = false;
            myservo.attach(servoPin);
            myservo.write(unlockedPos);
          } 
          else {
            // Incorrect password entered, sound the buzzer to notify the user
            digitalWrite(buzzerPin, HIGH);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            digitalWrite(buzzerPin, LOW);
          }

          // Clear the entered password
          enteredPassword = "";
        }
      }
    }
  }
}

/**
  Sound a buzzer chime
*/
void taskBeep(void *pvParameters) {
  while (true) {
    // Suspend this task automatically and wait taskPing to resume this task
    vTaskSuspend(NULL);

    int c;
    for (c = 0; c < 4; c += 1) {
      digitalWrite(buzzerPin, HIGH);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(buzzerPin, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Restart the timer that controls taskPing
    xTimerStart(pingTimer, 0);
  }
}

void setup() {
  Serial.begin(9600);   // 9600 baudrate
  Serial.println("setup");
  pinMode(buzzerPin, OUTPUT);

  // Create timer to control taskPing
  pingTimer = xTimerCreate("pingTimer", 20, pdTRUE, NULL, taskPing);

  // Create tasks, each allocates 64 words as stack memory
  xTaskCreate(taskKeypad, "taskKeypad", 64, NULL, 1, &taskKeypadHandler);   // Priority 1
  xTaskCreate(taskSend, "taskSend", 64, NULL, 2, &taskSendHandler);         // Priority 2
  xTaskCreate(taskBeep, "taskBeep", 64, NULL, 3, &taskBeepHandler);         // Priority 3

  // Start the timer
  xTimerStart(pingTimer, 0);

  // Start the RTOS scheduler
  vTaskStartScheduler();  
}

void loop() {
  // This function should never be called, as the RTOS scheduler will take over
}
