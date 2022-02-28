# [AE] Upravljanje klimom u automobilu

## Uvod

  Ovaj projekat ima za zadatak da simulira rad klima uredjaja u automobilu. Kao okruzenje i alat za rad se koristi VisualStudio2019.
  Zadatak projekta je osim funkcionalnosti bila i implementacija MISRA standarda prilikom pisanja koda.

## Ideja i zadaci

  1.  Pratiti temperaturu u kolima. Sa kanala 0 uzimati vrednost temperature sa senzora.   
  2.  Realizovati komunikaciju sa simuliranim sistemom. Slati naredbe preko serijske komunikacije.
  3.  Podesavanje zeljene temperature koju klima treba da odrzava u automobilu.
  4.  Komande rada klime za manuelno i automatski, kojima se menja rezim rada klime
  5.  vrednost histerezisa u automatskom rezimu rada
  6.  slanje ka racunaru trenutne vrednosti temperature, rezima rada klime, kao i statusa klime

## Periferije

  Periferije koje se koriste u simulaciji su LED_bar, 7seg displej i AdvUniCom; softveri za simulaciju serijske komunikacije.
  
  Prilikom pokretanja LED_bars_plus.exe navesti kao argument RRRRRr gde se dobija led bar sa 5 izlaznih i 1 ulaznih stubaca.
  
  Prilikom pokretanja Seg7_Mux.exe navesti kao argument broj 8, cime se dobija displej sa 8 cifara.
  
  Sto se serijske komunikacije tice, potrebno je otvoriti kanale 0 i 1. Kanal 0 se automatski pokrece otvaranjem AdvUniCom.exe aplikacije, a kanal 1 otvoriti dodavanjem broja 1 kao argument uz AdvUniCom.exe u komandnoj liniji.

## Kratak pregled taskova

  Glavni fajl(.c fajl) ovog projekta je main.application.c. U njemu se nalaze sve bitne funkcionalnosti vezane za projekat.
  
  ## void led_bar_tsk(void *pvParameters)
    
    Task koji na osnovu pritisnutog tastera vrsi ukljucivanje signalne diode na istom led baru. Takodje, njegova glavna funkcija jeste slanje podataka u red kada je korisnik promenio stanje na prekidacu.
     
  ## void SerialReceive_Task0(void* pvParameters)
  
    Ovaj task vrsi prijem podataka sa kanala 0 serijske komunikacije. Podatak koji pristigne je u vidu stringa i odnosi se na vrednost temperature ocitane na senzoru. Task uzima vrednost stringa i iz njega izvlaci broj odnosno trenutnu vrednost temperature sa senzora. Tu obradjenu vrednost task salje preko reda drugim taskovima.
  
  ## void SerialReceive_Task1(void* pvParameters)
  
    Ovaj task vrsi prijem podataka sa kanala 1 serijske komunikacije. Podatak pristize kao paket koji treba dekodirati. Paket je oblika "\00XXYY\0d", gde "XX" predstavljaju slovne komande za prepoznavanje date komande, dok "YY" predstavljaju brojne komande koje oznacavaju neku vrednost. Poruka se zavrsava sa "carriage return" tj. brojem 13 decimalno. Naredbe koje su sadrzane u ovom tasku su sledece:
      1.  \00EV23\0d - ulazak u mod za podesavanje temperature koju zelimo da postignemo(eng. expected value); broj oznacava vrednost te temperature.
      2.  \00MV1\0d - ulazak u mod za podesavanje rezima rada klime(eng. mode value); broj 0 oznacava manuelni rezim, a broj 1 oznacava automatski rezim.
      3.  \00SV1\0d - ulazak u mod za podesavanje statusa trenutnog rezima rada(eng. status value); broj 0 oznacava OFF, a broj 1 oznacava ON.
      4.  \00HV5\0d - ulazak u mod za podesavanje histerezisa kada je izabrani rezim rada klime automatski(eng. histeresis value); broj pored naredbe oznacava vrednost histerezisa tj. koliko zelimo da temperatura varira od zeljene temperature.
      5.  \00IV\0d - ulazak u mod za informacije o trenutnom stanju sistema(eng. information value); tu se vrsi prikaz u komandnoj liniji i to: zeljene vrednosti, rezima rada i trenutnog statusa.
  
  ## void DisplayLCD_Task(void* pvParameters)
  
    Ovaj task omogucava prijem podataka iz reda i ispis odredjenih informacija na displeju. Podaci koje displej prikazuje su redom: rezim rada klime, trenutna vrednost temperature sa senzora i zeljena vrednost korisnika.
  
  ## void ObradaPodataka_Task(void* pvParameters)
  
    Ovaj task omogucava prijem podataka iz reda i vrsi njihovu obradu u zavisnosti od datih uslova. Kada pristignu podaci o trenutnom rezimu rada, statusu klime i slicno, task vrsi njihovu obradu i na osnovu toga prolazi kroz odredjene uslove i ispisuje podatke na komandnoj liniji.
  
  ## void main_demo(void)

    U ovoj funkciji se vrsi inicijalizacija svih periferija koje se koriste, kreiraju se taskovi, semafori i redovi, definisu se prekidi za serijsku komunikaciju i poziva se funkcija vTaskStartScheduler() koja aktivira planer za rasporedjivanje taskova.

## Predlog simulacije celog sistema

    1. Najpre otvoriti periferije na gore opisan nacin.
    2. Pokrenuti program iz okruzenja VisualStudio2019.
    3. U prozor aplikacije AdvUniCom 0 upisati vrednost sa senzora. Poruku upisati po sledecem obrascu: "11C.", gde "11" moze biti bilo koji dvocifren broj. Obavezno poruku zavrsiti tackom.
    4. U prozor aplikacije AdvUniCom 1 unositi vrednosti za zadavanje komandi. Pravilan nacin unosenja poruka dat je u delu opisa taskova i to taska void SerialReceive_Task1().
    5. Na displeju pratiti promenu vrednosti
    6. Preko ulaznog dela led bara zadati komandu za promenu stanja u manuelnom rezimu rada. Koristi nulti taster u ove svrhe.
    7. Preko komandne linije pratiti sve informacije vezane za promenu stanja u sistemu.
