# BioSynth
Repository containing the project code for the "Laboratorio di Making" course for the 2025/2026 academic year (https://www.unibo.it/it/studiare/insegnamenti-competenze-trasversali-moocs/insegnamenti/insegnamento/2025/545359), along with the instructions required to replicate it. BioSynth is a bioimpedance plant translation system into music, featuring IoT integration. 

# Reproduction Instructions
To ensure the correct reproduction of the BioSynth prototype, the step-by-step assembly and configuration procedure is provided below.
- Phase 1: Hardware preparation and wiring. After acquiring the components listed above, follow the wiring diagram provided in Appendix 1 to connect the various modules.
- Phase 2: Firmware flashing, which involves installing the necessary libraries in the Arduino IDE and uploading the code—found in the repository specified in Appendix 2—onto the ESP32.
- Phase 3: IoT configuration, the final stage, which entails creating a Telegram bot and inserting its token into the code, along with the network name (SSID) and password for the network you intend to use.

# Future Updates
- To achieve a more precise calculation of the plant's natural bioimpedance values, a soil moisture sensor will be integrated in the future; the data it collects will be factored into the calculation of the BIO and BASE values.
- To allow for greater configurability, the 'ACTIVATION_THRESHOLD' value can be set directly via the Telegram bot, or potentially calculated automatically based on the frequency of event triggers.
- To better understand how the distance between the two electrodes affects the measured bioimpedance, a graph will be displayed on the OLED screen, showing how the BIO values ​​change as the distance between the electrodes varies. This will require adding a command for the Telegram bot and/or a physical button to the circuit to toggle the information shown on the screen.
