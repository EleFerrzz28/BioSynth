# BioSynth
Repository contenente il codice del progetto per il corso laboratorio di making e le istruzioni necessarie per riprodurlo. BioSynth è un traduttore di bioimpedenza delle piante in musica, con integrazione IoT. 

# Istruzioni di Riproduzione
Per consentire la corretta riproduzione del prototipo BioSynth, si riporta di seguito la procedura di assemblaggio e configurazione passo-passo. 
- Fase 1: Preparazione hardware e cablaggio. A seguito dell’acquisto dei componenti sopra elencati, seguire lo schema elettrico riportato alla sezione Appendice 1 per collegare i vari moduli. 
- Fase 2: Flash del Firmware, che prevede l’installazione delle librerie su Arduino IDE e il caricamento del codice presente nella repository indicata in Appendice 2 sull’ESP32
- Fase 3: Configurazione IoT, fase finale che prevede la creazione del bot su Telegram e l’inserimento del token nel codice, insieme al nome e alla password della stazione di rete che si prevede di utilizzare.

# Update Futuri
- Per un calcolo più preciso dei valori di bioimpedenza naturali della pianta, in futuro verrà integrato un sensore di umidità del suolo e ai valori rilevati da quest'ultimo verrà attribuito un peso nel calcolo di BIO e BASE
- Per consentire una maggiore configurabilità, il valore della soglia di attivazione, 'ACTIVATION_THRESHOLD' potrà essere impostata direttamente da bot di telegram ed eventualmente calcolata in automatico sulla base della frequenza degli event triggers
- Per comprendere l'impatto che la distanza dei due elettrodi ha sul valore di bioimpedenza rilevato, sullo schermo OLED verrà anche rappresentato un grafico che mostra cosa accade ai valori di BIO al variare della distanza tra i due elettrodi. Aggiungere dunque comando per bot telegram e/o bottone nel circuito per modificare cosa viene visualizzato su schermo

