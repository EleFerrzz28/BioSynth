// ================================================================================
// PROJECT: BioSynth - Plant Bio-Impedance Sonification System
// COURSE: LABORATORY OF MAKING
// AUTHOR: Eleonora Ferrari
// DATE: July 2026
// DESCRIPTION: The system converts plant bio-electric signals into ambient/dynamic 
//              music scales with IoT Telegram integration, together with visual 
//              feedback
// ================================================================================

// 1. LIBRARIES
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// 2. HARDWARE CONFIGURATIONS (Pinout and OLED)
#define OLED_SDA 21 
#define OLED_SCL 22 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

const int BIO_SENSOR_PIN = 34; // ADC1 channel for bio-impedence reading
const int AUDIO_PIN = 26; // PWM audio output (connected to PAM8403)

// 3. GLOBAL VARIABLES AND CONSTANTS

// ----- USER CONFIGURATION -----

// Wifi network station credentials
#define WIFI_SSID "Kika"
#define WIFI_PASSWORD "EldenLord"
// Telegram Bot Token
#define BOT_TOKEN "7934798297:AAGVGZ7w-X_8yt4RygL-EkDNF7KqOP4nAo8"


// ----- Bio-impedence Reading -----

// Filter variables
float filterRead = 0;
const float ALPHA_FILTER = 0.15; // EMA filter coefficient for BIO values

// Event Trigger variables
const float ALPHA_SLOW = 0.002; // EMA filter coefficient for BASELINE values   
const int ACTIVATION_THRESHOLD = 60; // Increase (e.g., 100) if environment has high electrical noise
float slowAverage = 0;  
bool eventTrigger = false;


// ----- Plant profiles -----

// Plant profile struct
struct PlantProfile {
  const char* command; // Telegram bot command
  const char* description; // description of the category 
};

// Plant profiles 
const int NUM_PROFILES = 4;
const PlantProfile plantProfiles[NUM_PROFILES] = {
  {
    "/low_drone", 
    "BIO Range ~0-1100. Description: These plants are characterized by values flattened towards zero, flat signal and unresponsive to touch. Their melody is identified by low and constant notes, drones and static soundscapes-let us say, the low_drone plants are ideal for meditation sessions. Examples: Lingua di suocera(dracaena trifasciata), stella di vetro (haworthia retusa), pianta fantasma (graptopetalum)."
  }, 
  {
    "/low_rhythm", 
    "BIO Range ~1200-1900. Description: These plants are characterized by consistently low yet dynamic values, with intrinsic micro-fluctuations due to the segmentation of the fleshy leaves. The melody created remains in the low register, but with a more rhythmic and lively phrasing or arpeggio. Examples: Erba Teresina (Sedum sieboldii), cuore di giada (Mesembryanthemum cordifolium), surfinia (petunia integrifolia)."
  }, 
  {
    "/medium", 
    "BIO Range ~2000-2700. Description: These plants are characterized by average values that are stable at rest but tend to be responsive to human touch. Their symphony is lively, composed of middle octaves (piano-like range), perfect for soloist interaction. Examples: Spatifillo (spathiphyllum), pomodoro (solanum lycopersicum), falangio (chlorophytum comosum), peonia."
  }, 
  {
    "/high", 
    "BIO Range ~2800-4095. Description: These plants are characterized by values close to the ADC ceiling—a nervous, vibrant signal with continuous triggering. Producing high-pitched, crystalline notes, their melodies tendo to sound hyperactive and frenetic. Examples: Tarassaco comune (taraxacum officinale), rosmarino (salvia rosmarinus), lavanda (lavandula)."
  }
};


// ----- OLED variables -----

// OLED display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ECG OLED variables 
#define BUFFER_SIZE 56 // ECG area
int history[BUFFER_SIZE]; 
int historyIndex = 0;


// ---- Music and Sound variables -----

// Audio quality 
const int audioQuality = 8;

// Last played frequency
int lastPlayedFrequency = -1;

// Scales variables
// activeScale is declared "volatile", since it's modified by Core 0 (telegram bot)
// and read by core 1 (sound production)
volatile int activeScale = 0;

// Musical Scales, all extended to 8 notes - with the correspective frequencies: 
int scales[7][8] = {
  {262, 294, 330, 392, 440, 523, 587, 659}, // 0: C major pentatonic scale
  {220, 247, 262, 294, 330, 349, 392, 440}, // 1: A minor natural scale
  {262, 277, 311, 349, 392, 415, 466, 523},  // 2: Arabic scale
  {262, 311, 349, 370, 392, 466, 523, 622}, // 3: Blues scale
  {294, 311, 392, 440, 523, 587, 622, 784}, // 4: Japanese (Miyako-bushi / In Sen)
  {256, 288, 324, 342, 384, 432, 486, 512}, // 5: Accordatura Aurea
  {247, 262, 294, 330, 349, 392, 440, 494}, // 6: Scala Locria
};


// ----- Timer variables -----

unsigned long lastCheck = 0; // Deviation computation
unsigned long lastUpdate_OLED = 0; // OLED update
unsigned long timerEventTrigger = 0; // Event Trigger Visualization
unsigned long lastMusicUpdate = 0; // Sound update
const unsigned long BOT_MTBS = 1000; // Time between scan messages


// ----- Bot object instantiation -----
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Telegram task handler 
TaskHandle_t TelegramTaskHandle;

// 4. MODULAR FUNCTIONS
/* 
Function that takes in input the computed frequency and changes the played frequency
only if it is different from the last played one, so as to avoid distorsion and
unnecessary sound production. 
Paremeters: computed frequency 
*/
void playTone(int frequency){
  if (frequency != lastPlayedFrequency){
    ledcWriteTone(AUDIO_PIN, frequency);
    lastPlayedFrequency = frequency;
  }
}

/* 
Function that, given the acquired bio filtered and base values, computes the correct note. 
Parameters: ADC bio value, baseline value and mode (mode == 0 -> ambience note mode, 
mode == 1 -> fast note mode)
*/
void mapNotes(int bioValue, int baseValue, bool mode){
  int note = 0; 
  int deviation = 0; 

  if(mode){
    // ----------- FAST NOTE MODE ----------
    /* This mode activates whenever an event trigger (a sudden peak caused by human touch
    or deep stress) occures. The plants reacts to an instant stimulus. */

    /* the function abs() is used because what truly matters is the 
    extent of the fluctuation - not if it is positive or negative */
    deviation = abs(bioValue - baseValue);

    /* in order to enhance the sysem's sensitiveness, a narrow range [0, 15]
    is used in the map() function. A slight touch will move the melody across 
    the whole musical scale */
    note = map(deviation, 0, 15, 0, 7);

    /* Software safety guard: prevents out-of-bounds array access 
    in the 'scales' matrix if calculations exceed scale dimensions (0-7) */
    note = constrain(note, 0, 7); 

    /* finally, the computed note is mapped accross the active scale into
    the corresponding frequency */
    int frequency = scales[activeScale][note];
    playTone(frequency); // function that plays the frequency
  } else {
    // ----------- AMBIENCE NOTE MODE ----------
    /* This mode activates when the plant is in a "resting" state, that is, when it does
    not receive any external stimulus. It represents its normal vegetative state, its own
    "melody". 
    NOTE: for musical reasons, the small physiological variations will cause it to move up 
    or down by a maximum of two notes from the baseline tone. */

    deviation = bioValue - baseValue; // without abs()

    /* baseNote represents the plant's base identity, and the whole ADC range [0, 4095] is
    mapped into the eight notes of a scale. Each plant has its own fixed, defining note */
    int baseNote = map(baseValue, 0, 4095, 0, 7);

    /* varNote takes into account micro-fluctuations (deviation variable) with positive
    or negative signs, causing the melody to oscillate above and below the base note.
    A small deviation is mapped to a limited note offset [-2, +2]  */
    int varNote = map(deviation, -10, 10, -2, 2);
    varNote = constrain(varNote, -2, 2); // safety guard

    note = baseNote + varNote; // final note
    note = constrain(note, 0, 7); // safety guard

    /* finally, the computed note is mapped accross the active scale into
    the corresponding frequency */
    int frequencyA = scales[activeScale][note];
    playTone(frequencyA); // function that plays the frequency
  }
}

/*
Function that handles bot received messages (during the BOT_MTBS time range) and checks whether 
the command in input corresponds to a scale, a plant profile or an identification request. 
Here lies a list of all possible commands: 
  > /cmajor -> sets activeScale to 0, C Major Pentatonic scale
  > /aminor -> sets activeScale to 1, A Minor Natural scale
  > /arabic -> sets activeScale to 2, Arabic scale
  > /blues -> sets activeScale to 3, Blues-like scale
  > /japanese -> sets activeScale to 4, Japanese scale (Miyako-bushi / In Sen)
  > /aurea -> sets activeScale to 5, Accordatura Aurea scale
  > /locria -> sets activeScale to 6, Locria scale
  > /low_drone -> visualize low drone plant category
  > /low_rhythm -> visualize low rhythm plant category
  > /medium -> visualize medium plant category
  > /high -> visualize high plant category
  > /identify -> automatic plant identification (based upon baseline ADC value)
Paremeters: number of messages received
*/
void handleMessages (int numNewMsgs){
  for(int i = 0; i < numNewMsgs; i++){ 
    String chat_id = bot.messages[i].chat_id; // unique Telegram chat identifier for routing responses
    String text = bot.messages[i].text; // incoming user command or message string

    // check text is a scale command...
    if(text == "/cmajor") { activeScale = 0; bot.sendMessage(chat_id, "Scale set: C Major Pentatonic", ""); }
    else if (text == "/aminor") { activeScale = 1; bot.sendMessage(chat_id, "Scale set: A Minor Natural", ""); }
    else if (text == "/arabic") { activeScale = 2; bot.sendMessage(chat_id, "Scale set: Arabic Scale", ""); }
    else if (text == "/blues") { activeScale = 3; bot.sendMessage(chat_id, "Scale set: Blues Scale", ""); }
    else if (text == "/japanese") { activeScale = 4; bot.sendMessage(chat_id, "Scale set: Japanese Scale", ""); }
    else if (text == "/aurea") { activeScale = 5; bot.sendMessage(chat_id, "Scale set: Accordatura Aurea", ""); }
    else if (text == "/locria") { activeScale = 6; bot.sendMessage(chat_id, "Scale set: Locria Scale", ""); }
    else {
      bool found = false;

      //...a plant profile command...
      for (int j = 0; j < NUM_PROFILES; j++){
        if (text == plantProfiles[j].command) {
          bot.sendMessage(chat_id, plantProfiles[j].description, "");
          found = true;
          break;
        }
      }

      // ...or an identification command
      if(text == "/identify"){
        String msg;
        if(slowAverage <= 1100){
          msg = "Bio-analysis completed!\nThe plant likely belongs to the low drone category.\n\n" + String(plantProfiles[0].description);
          bot.sendMessage(chat_id, msg, "");
        } else if (slowAverage > 1100 && slowAverage <= 1900){
          msg = "Bio-analysis completed!\nThe plant likely belongs to the low rhythm category.\n\n" + String(plantProfiles[1].description);
          bot.sendMessage(chat_id, msg, "");
        } else if (slowAverage > 1900 && slowAverage <= 2700){
          msg = "Bio-analysis completed!\nThe plant likely belongs to the medium category.\n\n" + String(plantProfiles[2].description);
          bot.sendMessage(chat_id, msg, "");
        } else if (slowAverage > 2700 && slowAverage <= 4095 ){
          msg = "Bio-analysis completed!\nThe plant likely belongs to the high category.\n\n" + String(plantProfiles[3].description);
          bot.sendMessage(chat_id, msg, "");
        }
        found = true;
      }

      /* if the incoming user message is not a predefined command, 
      the bot sends a list of all possible commands*/
      if(!found){
        String helpMsg = "Available music commands: /cmajor, /aminor, /arabic, /blues, /japanese, /aurea, /locria\n\n";
        helpMsg += "Available plant profiles: ";
        
        for (int j = 0; j < NUM_PROFILES; j++) {
          helpMsg += String(plantProfiles[j].command) + " ";
        }

        helpMsg += "\nAutomatic detection: /identify";
        bot.sendMessage(chat_id, helpMsg, "");
      }
    }
  }
}

/* FreeTOS Task dedicated to handling Telegram Bot communication. Runs continuously on the
assigned core (core 0). 
Parameters: pointer to task input parameters (not used here, NULL)*/
void TelegramTask(void * pvParameters){
  // Print verification to the serial monitor to ensure correct core deployment
  Serial.println("Telegram Task launched on core: ");
  Serial.println(xPortGetCoreID()); 

  // store the last time the bot polled for updates
  unsigned long bot_lasttime = 0;

  for(;;){
    unsigned long currentTime = millis();

    // check is the ESP32 is connected to the WiFi network
    if(WiFi.status() == WL_CONNECTED){

      // enforce non-blocking polling interval (BOT_MTBS)
      if(currentTime - bot_lasttime > BOT_MTBS){

        // fetch pending messages from telegram server using correct message offset
        int numNewMsgs = bot.getUpdates(bot.last_message_received + 1);

        // if new messages are detected, call handleMessages() and process them
        if (numNewMsgs > 0){
          handleMessages(numNewMsgs);
          bot.last_message_received = bot.messages[numNewMsgs - 1].update_id;
        }
        bot_lasttime = currentTime; // timestamp update
      } 
    } else { // WiFi disconnection handling without crash
      static unsigned long lastWifiCheck = 0;

      // attempt a non-blocking WiFi reconnection every 10 seconds
      if (currentTime - lastWifiCheck >= 10000){
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        lastWifiCheck = currentTime;
      }
    }

    /* yield CPU control to the RTOS background activities (Wi-Fi/IP stack) for 50ms.
    This prevents the Task Watchdog Timer (TWDT) from resetting the ESP32.*/
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// 5. CORE FUNCTIONS (setup & loop)

/*
setup() function: the OLED screen is initialized, operation followed by system booting;
the slowAverage value of the connected plant is computed through arithmetic mean, 
WiFi and LED PWM controller are set, and the Telegram task is launched on core 0. 
*/
void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL); 
  
  /* initialization of SSD1306 OLED screen, configuration of the latter internal charge pump to 
  generate display voltage, and check for successful communication */
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;);
  }
  
  // OLED initial settings 
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("System booting...");
  display.display();

  /* Before the loop() function, the resting state of the plant is computed and stored
  in the slowAverage variable. This is accomplished by performing 100 analog readings
  every 5ms and then computing the arithmetic mean. Distinct plants will have distinct 
  baseline initial values */
  // Plant's baseline
  long sum = 0;
  for(int i=0; i<100; i++) {
    sum += analogRead(BIO_SENSOR_PIN);
    delay(5);
  }
  filterRead = sum / 100;
  slowAverage = filterRead;

  // ledcAttach() is used to setup the LEDC (LED PWM Controller) pin frequency and resolution
  ledcAttach(AUDIO_PIN, 2000, audioQuality);

  // WiFi setup
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // attempt to establish WiFi connection (maximum 20 attempts)...
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    delay(500);
    Serial.print(".");
    attempt++;
  }
   
  // ...if establishment of the connection fails, the system proceeds offline
  if(WiFi.status() == WL_CONNECTED) { 
     Serial.println("WiFi connected");
  } else {
     Serial.println("WiFi not connected, booting offline.");
  }

  // WiFi secure client setup (bypasses SSL certificate validation for speed and RAM saving)
  secured_client.setInsecure();
  delay(500);

  /* clean the telegram update queue of old, unhandled messages sent 
  while the bot was offline */
  int numOldMsg = bot.getUpdates(bot.last_message_received + 1);
  if (numOldMsg > 0){
    bot.last_message_received = bot.messages[numOldMsg - 1].update_id;
  }

  // Core 0 Telegram task creation 
  xTaskCreatePinnedToCore(
    TelegramTask,         // task function to execute
    "TelegramTask",       // name string for the task (debugging purposes)
    12288,                // stack size in bytes
    NULL,                 // task input parameters (none passed)
    1,                    // task priority (1 = low/normal priority)
    &TelegramTaskHandle,  // task handle reference
    0                     // core ID
  );
}

/*
loop() function: executed on Core 1, the loop() function performes the following operations:
1. bio and baseline values computation through EMA, after the acquisition of the raw analog signal
2. check whether an "event trigger" occured (a stimulus that affected the plant's bio-impedence values),
   by analyzing the difference between bio and baseline values. 
3. update music (through the function mapNotes()) 
4. update OLED, by displaying bio and base values, whether an event trigger occured or and
   draw an ECG-like graph.
*/
void loop() {
  unsigned long currentTime = millis();

  // acquisition of raw analog ADC signal
  int rawRead = analogRead(BIO_SENSOR_PIN);

  // 1. BIO and BASELINE computation
  
  /* BIO computation: this first operation cleans the raw signal coming from the plant pin. It
  employs a filter and an Exponential Moving Average (EMA). Instead of storing an array of 
  readings - which would result in an excessive memory usage - it calculates the current value by
  assigning a 15% weight (ALPHA_FILTER) to the latest raw reading (rawRead variable) and an 
  85% weight (1.0 - 0.15) to the accumulated signal history (filterRead). 
  Its goal is to smooth out high-frequency electrical noise (such as 50Hz mains interference or ADC jitter), 
  yielding a BIO variable that is fluid yet responsive enough to detect a human touch.
  */
  filterRead = (ALPHA_FILTER * rawRead) + ((1.0 - ALPHA_FILTER) * filterRead);
  int BIO = (int)filterRead;

  /* BASELINE computation: an other EMA filter is employed, this time using a very small
  coefficient: ALPHA_SLOW = 0.002, thus meaning that the latest reading accounts only 0.2%,
  while 99.8% of the baseline value comes from past data. Its purpose is to create a 
  baseline (variable slowAverage) that moves extremely slowly. If one touches the plant, 
  the BIO value spikes up or down, but the slowAverage remains virtually motionless, acting
  as an "anchor" or a reference point for the resting state
  */
  slowAverage = (ALPHA_SLOW * BIO) + ((1.0 - ALPHA_SLOW) * slowAverage);

  // 2. EVENT TRIGGER CHECK

  // every 20 ms is performed an event trigger check
  if (currentTime - lastCheck >= 20) {
    // deviation computation
    int deviation = abs(BIO - (int)slowAverage);
    
    /* if the difference between bio and baseline values is enough - that is, greater than the
    ACTIVATION_THRESHOLD constant (whose value was determined experimentally), it means that 
    an event trigger has been detected. */
    if (deviation > ACTIVATION_THRESHOLD) {

      if (!eventTrigger) {
        eventTrigger = true;

        /* the time in which the event trigger occured is stored, so as to mantain the
        eventTrigger variable set to true for 2 seconds, allowing the user to see it on screen */
        timerEventTrigger = currentTime; 
      }
    }
    /* in order to correctly map and represent BIO values in the ECG displayed on the right side
    of the OLED screen, the computed BIO value is stored in the history buffer, and the index is
    later increased */
    history[historyIndex] = BIO;
    historyIndex = (historyIndex + 1) % BUFFER_SIZE;
  
    lastCheck = currentTime;
  }

  // Sustainment
  if (eventTrigger && (currentTime - timerEventTrigger >= 2000)) {
    eventTrigger = false;
  }

  // 3. Music Update

  /* Every 150 ms, the function mapNotes() is called, which will perform note and
  subsequent frequency mapping, according to the mode (true for fast notes related to event
  triggers and false for ambience notes related to the resting state of the plant) */
  if (currentTime - lastMusicUpdate >= 150){
    if(eventTrigger){
      // true -> interaction - rapid notes
      mapNotes(BIO, (int)slowAverage, true);
    } else {
      // false -> ambience music - slow notes
      mapNotes(BIO, (int)slowAverage, false);
    }

    lastMusicUpdate = currentTime;
  }

  // 4. OLED update

  // through a non-blocking timer, the OLED display is refreshed every 100 ms
  if (currentTime - lastUpdate_OLED >= 100) {
    display.clearDisplay();

    // --------- TEXT SECTION (left side of the screen) ----------
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // display current Bio-Impedence raw value
    display.setCursor(0, 2);
    display.print("BIO: ");
    display.println(BIO);
    
    // display the rolling baseline/slow average value (cast to int)
    display.setCursor(0, 14);
    display.print("BASE: ");
    display.println((int)slowAverage);
    
    // draw a horizontal divider line 
    display.drawFastHLine(0, 26, 68, SSD1306_WHITE);

    // display the event trigger status
    display.setCursor(0, 32);
    display.println("EVENT");
    
    display.setCursor(0, 44);
    display.println("TRIGGER:");

    display.setCursor(0, 56);
    if (eventTrigger) {
      display.println("[YES]");
    } else {
      display.println("[NO]");
    }
    
    // draw a vertical line divider separating the text data from the ECG
    display.drawFastVLine(70, 0, 64, SSD1306_WHITE);

    // --------- OSCILLOSCOPE SECTION (right side of the screen) ----------

    // iteration through the circular buffer to render the waveform lines
    for (int i = 0; i < BUFFER_SIZE - 1; i++) {

      // calculate wrapped indices to correctly read from the circular history buffer
      int index1 = (historyIndex + i) % BUFFER_SIZE;
      int index2 = (historyIndex + i + 1) % BUFFER_SIZE;
      
      // map the plant's dynamic range (0-4000) into screen Y coordinates (63=bottom, 0=top)
      int y1 = map(history[index1], 0, 4000, 63, 0); 
      int y2 = map(history[index2], 0, 4000, 63, 0);
      
      // constrain mapped values to prevent accidental rendering out-of-bounds
      y1 = constrain(y1, 0, 63);
      y2 = constrain(y2, 0, 63);
      
      // draw the waveform segments shifted to the right half of the display (starting from X: 71)
      display.writeLine(71 + i, y1, 71 + i + 1, y2, SSD1306_WHITE);
    }
    
    display.display();
    lastUpdate_OLED = currentTime;
  }
  delay(5); 
}
