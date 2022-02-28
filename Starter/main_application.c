
/* Standard includes. */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

/* Hardware simulator utility functions. */
#include "HW_access.h"

/* SERIAL SIMULATOR CHANNEL TO USE. */
#define COM_CH_0 (0)
#define COM_CH_1 (1)
//#define COM_CH_1 (2) // ne koristimo drugi senzor

/* MACROS */

/* TASK PRIORITIES */
#define TASK_SERIAl_REC_PRI0			( tskIDLE_PRIORITY + (UBaseType_t)4 ) // prijem sa serijske komunikacije ( ima veci prioritet u odnosu na slanje da ne bi doslo do gubitka podataka prilikom prijema )
#define TASK_SERIAl_REC_PRI1			( tskIDLE_PRIORITY + (UBaseType_t)4 ) // prijem sa serijske komunikacije ( ima veci prioritet u odnosu na slanje da ne bi doslo do gubitka podataka prilikom prijema )
#define TASK_LED_BAR					( tskIDLE_PRIORITY + (UBaseType_t)3 ) // pritisak tastera na led baru
#define	TASK_7SEG						( tskIDLE_PRIORITY + (UBaseType_t)3 ) // ocitavanje 7seg
#define	TASK_SERIAL_SEND_PRI			( tskIDLE_PRIORITY + (UBaseType_t)2 ) // slanje na serijsku komunikaciju
#define TASK_OBRADA						( tskIDLE_PRIORITY + (UBaseType_t)1 ) // vezano za taskove automatic i manual

/* TASKS: FORWARD DECLARATIONS */
void led_bar_tsk(void *pvParameters);			// ocitavanje sa LED bar
void SerialSend_Task0(void* pvParameters);		// slanje na serijski port
void SerialReceive_Task0(void* pvParameters);	// prijem sa senzora temperature
void SerialReceive_Task1(void* pvParameters);	// prijem komandi sa serijske komunikacije, ujedno i vrsi obradu pristigle poruke
void DisplayLCD_Task(void* pvParameters);		// ispis vrednosti na 7seg
void ObradaPodataka_Task(void* pvParameters);	// obrada pristliglih podataka iz reda i ispis u komandnoj liniji

/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
const char trigger[] = "XYZ";
unsigned volatile t_point;
uint16_t cv = 0; //ova promenljiva cuva vrednost temperature sa senzora
uint16_t temperatura = 0u;
static uint16_t EV;
static uint16_t MV;
static uint16_t SV = 0;
static uint16_t IV;
static uint16_t HV = 10;
static uint16_t MANUELNI = 0;
static uint16_t AUTOMATSKI = 0;

/* RECEPTION DATA BUFFER */
#define R_BUF_SIZE (32)
static char r_buffer[R_BUF_SIZE];
static char r_buffer1[R_BUF_SIZE];
static char r_buffer2[R_BUF_SIZE];
static uint8_t volatile r_point, r_point1;
static char expected_value[R_BUF_SIZE];
static char mode_value[R_BUF_SIZE];
static char status_value[R_BUF_SIZE];
static char info_value[R_BUF_SIZE];
static char histeresis_value[R_BUF_SIZE];
static uint8_t receiver_counter = 0u;
static uint8_t sendToLcd = 0u;

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

static const char line[] = { 0x21 };

/* GLOBAL OS-HANDLES */
static SemaphoreHandle_t LED_INT_BinarySemaphore;
static SemaphoreHandle_t TBE_BinarySemaphore;
static SemaphoreHandle_t RXC_BinarySemaphore0;
static SemaphoreHandle_t RXC_BinarySemaphore1;
static SemaphoreHandle_t SEVEN_SEG_BinarySemaphore;
//static SemaphoreHandle_t MUTEX_Semaphore;

/* TIMER HANDLE*/
TimerHandle_t per_TimerHandle;

/* QUEUE HANDLE*/
static QueueHandle_t temperatura_q = NULL;
static QueueHandle_t EV_q = NULL;
static QueueHandle_t MV_q = NULL;
static QueueHandle_t SV_q = NULL;
static QueueHandle_t HV_q = NULL;
static QueueHandle_t switch_q = NULL;

/* PERIODIC TIMER CALLBACK */
static void TimerCallback(TimerHandle_t xTimer)
{
	xSemaphoreGive(SEVEN_SEG_BinarySemaphore); // preko timer funkcije trazimo semafor na svakih 200ms za ispis podataka na displej
}

/* Ovaj task omogucava kontrolu ukljucivanja manuelnog rezima klime; pritiskom na ulazni segment leb_bar programa, tasku za obradu
	podataka se salje preko reda podatak o tome da li je taster pritisnut */
void led_bar_tsk(void* pvParameters)
{
	uint16_t switch_send;
	uint8_t d;
	for(;;)
	{
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		
		get_LED_BAR(5, &d);
		printf("Vrednost d: %d\n", d);
		set_LED_BAR(4, d);
		switch_send = (uint16_t)d;
		xQueueSend(switch_q, &switch_send, 0);	//primeniti MISRA; obrisati komentar nakon primene
	}
}

/* Ovaj task prima podatke preko redova, obradjuje ih i omogucava ispis u komandnoj liniji; podaci sadrze informacije
	da li je manuelna klima ukljucena ili nije i da li trenutna temperatura upada u opseg ocekivane vrednosti i histerezisa */
void ObradaPodataka_Task(void* pvParameters) {

	uint16_t mv_buf = 2;
	uint16_t switch_buf = 2;
	uint16_t ev_buf = 0;
	uint16_t temp_buf = 0;
	uint16_t hv_buf = 0;
	uint16_t sv_buf = 0;

	for (;;) {

		xQueueReceive(MV_q, &mv_buf, pdMS_TO_TICKS(100));	//primeniti MISRA; obrisati komentar nakon primene

		xQueueReceive(switch_q, &switch_buf, pdMS_TO_TICKS(100));	//primeniti MISRA; obrisati komentar nakon primene

		xQueueReceive(EV_q, &ev_buf, pdMS_TO_TICKS(100));	//primeniti MISRA; obrisati komentar nakon primene
			
		xQueueReceive(temperatura_q, &temp_buf, pdMS_TO_TICKS(100));	//primeniti MISRA; obrisati komentar nakon primene

		xQueueReceive(HV_q, &hv_buf, pdMS_TO_TICKS(100));	//primeniti MISRA; obrisati komentar nakon primene

		xQueueReceive(SV_q, &sv_buf, pdMS_TO_TICKS(100));	//primeniti MISRA; obrisati komentar nakon primene

		/*MANUAL MODE*/
		if (mv_buf == 0 && switch_buf == 0)
		{
			printf("**************** \n MANUAL Primio \n mode %d \n switch %d \n **************** \n", mv_buf, switch_buf);
			printf("Klima iskljucena. \n");
			switch_buf = 2;
		}
		else if (mv_buf == 0 && switch_buf == 1)
		{
			printf("**************** \n MANUAL Primio \n mode %d \n switch %d \n **************** \n", mv_buf, switch_buf);
			printf("Klima ukljucena. \n");
			switch_buf = 2;
		}
		
		/* AUTOMATIC MODE */
		else if (mv_buf == 1 && sv_buf == 1)
		{
			printf(" **************** \n AUTOMATIC Primio \n EV %d \n sens %d \n his %d \n switch %d \n mode %d \n **************** \n", ev_buf, temp_buf, hv_buf, sv_buf, mv_buf);
			/* Proveravamo da li je trenutna vrednost manja od ocekivane*/
			if (temp_buf < (ev_buf - hv_buf))
			{
				printf("Temperaturu treba povecati za: %d\n", ((ev_buf - hv_buf) - temp_buf));
			}
			/* Proveravamo da li je trenutna vrednost veca od ocekivane*/
			else if (temp_buf > (ev_buf + hv_buf))
			{
				printf("Tempetaturu treba smanjiti za: %d\n", temp_buf - (ev_buf + hv_buf));
			}
			else
			{
				printf("Dobro podesena temperatura!\n");
			}
		}
	}
}

/* Ovaj task ispisuje koji je trenutni mod izabran i ispisuje trenutnu i ocekivanu temperaturu na displeju */
void DisplayLCD_Task(void* pvParameters)
{
	uint16_t temp_buf = 0;
	uint16_t ev_buf = 0;
	uint16_t jedinica_temp = 0;
	uint16_t desetica_temp = 0;
	uint16_t jedinica_ev = 0;
	uint16_t desetica_ev = 0;
	uint16_t mv_buf = line[0];

	for (;;)
	{
		
			xSemaphoreTake(SEVEN_SEG_BinarySemaphore, portMAX_DELAY); //primeniti MISRA; obrisati komentar nakon primene

			xQueuePeek(MV_q, &mv_buf, pdMS_TO_TICKS(50));//primeniti MISRA; obrisati komentar nakon primene

			xQueuePeek(temperatura_q, &temp_buf, pdMS_TO_TICKS(50));//primeniti MISRA; obrisati komentar nakon primene

			xQueuePeek(EV_q, &ev_buf, pdMS_TO_TICKS(50));//primeniti MISRA; obrisati komentar nakon primene

			/* Ispis trenutnog moda na 7seg */
			select_7seg_digit(0);
			set_7seg_digit(hexnum[mv_buf]);

			/* Ispis trenutne temperature na 7seg */
			jedinica_temp = temp_buf % (uint16_t)10;
			desetica_temp = (temp_buf / (uint16_t)10) % (uint16_t)10;
			select_7seg_digit(2);
			set_7seg_digit(hexnum[desetica_temp]);
			select_7seg_digit(3);
			set_7seg_digit(hexnum[jedinica_temp]);

			/* Ispis ocekivane vrednosti na 7seg */
			jedinica_ev = ev_buf % (uint16_t)10;
			desetica_ev = (ev_buf / (uint16_t)10) % (uint16_t)10;
			select_7seg_digit(5);
			set_7seg_digit(hexnum[desetica_ev]);
			select_7seg_digit(6);
			set_7seg_digit(hexnum[jedinica_ev]);
	}
}

/* Ovaj task vrsi prijem podataka sa kanala 0. Ti podaci dolaze u vidu temperature sa senzora */
void SerialReceive_Task0(void* pvParameters)
{
	uint8_t cc;

	for (;;)
	{

		if (xSemaphoreTake(RXC_BinarySemaphore0, portMAX_DELAY) != pdTRUE)
		{
			printf("Greska/n");
		}

		if (get_serial_character(COM_CH_0, &cc) != 0)
		{
			printf("Greska/n");
		}

		//kada stignu podaci, salju se u red; kada poruka stigne do karaktera "C" tada se prebrise bafer u koji treba smestiti vrednost temperature sa senzora
		if (cc == (uint8_t)'C')
		{
			r_point = 0;
		}

		// poslednji karakter pri slanju poruke je tacka("."); karakter koji se ocekuje kako bi se pravilno poslala poruka
		else if (cc == (uint8_t)'.')
		{
			char* ostatak;
			printf(" Temperatura je %sC\n", r_buffer);
			temperatura = (uint16_t)strtol(r_buffer, &ostatak, 10);
			xQueueSend(temperatura_q, &temperatura, 0);    //primeniti MISRA; obrisati komentar nakon primene                                         
	
			r_buffer[0] = '\0';
			r_buffer[1] = '\0';
			r_buffer[2] = '\0';
			r_buffer[3] = '\0';
		}
		else
		{
			r_buffer[r_point++] = (char)cc;
		}

	}
}

/*salje na racunar informacije o trenutnoj i ocekivanoj vrednosti, mod klime(manuelni ili automatski) i status klime(ukljucen ili iskljucen)
Format poruke:		\00EV23\0d	-- ocekivana vrednost temperature
					\00MV0\0d	-- izabrani mod klime(manuelni ili automatski)
					\00SV1\0d	-- status izabranog moda klime(ukljucen ili iskljucen)
					\00HV5\0d	-- vrednost histerezisa
					\00IV\0d	-- informacija o unetim vrednostima
*/
static void SerialReceive_Task1(void* pvParameters)
{

	uint8_t cc;
	uint8_t j = 0, k = 0, l = 0;

	for (;;)
	{
		if (xSemaphoreTake(RXC_BinarySemaphore1, portMAX_DELAY) != pdTRUE)
		{
			printf("Neuspesno");
		}

		if (get_serial_character(COM_CH_1, &cc) != 0)
		{
			printf("Neuspesno");
		}

		if (cc == (uint8_t)0x00)
		{
			j = 0;
			k = 0;
			l = 0;
			r_point1 = 0;
		}
		else if (cc == (uint8_t)13) // 13 decimalno je CR(carriage return)
		{

			//Pristigla naredba je EV(expected value) iz koje je potrebno izvuci brojeve
			//Npr. EV31
			if (r_buffer1[0] == 'E' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						expected_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				EV = (uint16_t)strtol(expected_value, &ostatak, 10);
				printf("Ocekivana vrednost korisnika: %dC\n", EV);
				if (xQueueSend(EV_q, &EV, 0) != pdTRUE)
				{
					printf("Neuspesno slanje u red\n");
				}
			}

			//Pristigla naredba je MV(mode value) iz koje je potrebno izvuci broj
			//Npr. MV1
			else if (r_buffer1[0] == 'M' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						mode_value[k] = r_buffer1[i];
						k++;
					}
				}

				char* ostatak;
				MV = (uint16_t)strtol(mode_value, &ostatak, 10);
				if (xQueueSend(MV_q, &MV, 0) != pdTRUE)
				{
					printf("Neuspesno slanje u red\n");
				}
			
				if (MV == 0)
				{
					printf("Trenutni mod: MANUELNI\n");
					MANUELNI = 1;
					AUTOMATSKI = 0;
				}
				else if (MV == 1)
				{
					printf("Trenutni mod: AUTOMATSKI\n");
					MANUELNI = 0;
					AUTOMATSKI = 1;
				}
				else
				{
					printf("Pogresan unos naredbe\n");
				}
			}

			//Pristigla naredba je SV(status value) iz koje je potrebno izvuci broj
			//Npr. SV0
			else if (r_buffer1[0] == 'S' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						status_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				SV = (uint16_t)strtol(status_value, &ostatak, 10);
				if (xQueueSend(SV_q, &SV, 0) != pdTRUE)
				{
					printf("Neuspesno slanje u red\n");
				}

				if (SV == 0)
				{
					printf("Status: OFF\n");
				}
				else if (SV == 1)
				{
					printf("Status: ON\n");
				}
				else
				{
					printf("Pogresan unos naredbe\n");
				}
			}

			//Pristigla naredba je IV(info value)
			else if (r_buffer1[0] == 'I' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						info_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				IV = (uint16_t)strtol(info_value, &ostatak, 10);
				printf("**************************************\n");
				printf("Ocekivana vrednost korisnika: %d\n", EV);
				if (MV == 0)
				{
					printf("Trenutni mod: MANUELNI\n");

				}
				else if (MV == 1)
				{
					printf("Trenutni mod: AUTOMATSKI\n");
				}
				if (SV == 0)
				{
					printf("Status: OFF\n");
				}
				else if (SV == 1)
				{
					printf("Status: ON\n");
				}
				printf("**************************************\n");
			}

			//Pristigla naredba je HV(histeresis value) iz koje je potrebno izvuci broj
			//Npr. HV2
			else if (r_buffer1[0] == 'H' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						histeresis_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				HV = (uint16_t)strtol(histeresis_value, &ostatak, 10);
				printf("Vrednost odstupanja temperature: %d\n", HV);
				if (xQueueSend(HV_q, &HV, 0) != pdTRUE)
				{
					printf("Neuspesno slanje u red\n");
				}
				
				
			}
			
			else
			{
				printf("Nepoznata naredba\n");
			}

			r_buffer1[0] = '\0';
			r_buffer1[1] = '\0';
			r_buffer1[2] = '\0';
			r_buffer1[3] = '\0';
			r_buffer1[4] = '\0';
			r_buffer1[5] = '\0';
			r_buffer1[6] = '\0';
			r_buffer1[7] = '\0';
			r_buffer1[8] = '\0';
			r_buffer1[9] = '\0';
			r_buffer1[10] = '\0';
			r_buffer1[11] = '\0';

		}
		else
		{
			r_buffer1[r_point1++] = (char)cc;
		}
	}
}

/* Saljemo karakter 'a' i u AdvUniCom0 simulatoru omogucimo opciju AUTO ukljucen */
void SerialSend_Task0(void* pvParameters)
{
	uint8_t c = (uint8_t)'a';

	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(700));

		if (send_serial_character(COM_CH_0, c) != 0)
		{
			printf("Greska prilikom slanja");
		}
	}
}

/* OPC - ON INPUT CHANGE - INTERRUPT HANDLER */
static uint32_t OnLED_ChangeInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW); // javlja se ostatku sistema da se ovaj prekid desio

	portYIELD_FROM_ISR(xHigherPTW); //nakon interapta ne ceka naredni tick, nego odma zapocinje izvrsavanje taska iz semafora
}

/* TBE - TRANSMISSION BUFFER EMPTY - INTERRUPT HANDLER */
static uint32_t prvProcessTBEInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(TBE_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* RXC - RECEPTION COMPLETE - INTERRUPT HANDLER */
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t higher_priority_task_woken = pdFALSE;

	if (get_RXC_status(0) != 0)
	{
		if (xSemaphoreGiveFromISR(RXC_BinarySemaphore0, &higher_priority_task_woken) != pdTRUE)
		{
			printf("Greska pri slanju podatka\n");
		}
	}
	if (get_RXC_status(1) != 0)
	{
		if (xSemaphoreGiveFromISR(RXC_BinarySemaphore1, &higher_priority_task_woken) != pdTRUE)
		{
			printf("Greska pri slanju podatka\n");
		}
	}
	portYIELD_FROM_ISR((uint32_t)higher_priority_task_woken);

	/*BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(RXC_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);*/
}

/* MAIN - SYSTEM STARTUP POINT */
void main_demo(void)
{
	init_7seg_comm();
	init_LED_comm();
	init_serial_uplink(COM_CH_0);  // inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(COM_CH_0);// inicijalizacija serijske RX na kanalu 0
	init_serial_uplink(COM_CH_1);  // inicijalizacija serijske TX na kanalu 1
	init_serial_downlink(COM_CH_1);// inicijalizacija serijske RX na kanalu 1

	/* ON INPUT CHANGE INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt); // klikom misa generise se interapt broja 5 ( prvi parametar ) i poziva se funkcija ( drugi parametar )

	/* Create LED interrapt semaphore */
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary(); // binarni semafor radi prosledjivanja informacije iz interapt funkcije

	/* create a timer task */
	per_TimerHandle = xTimerCreate("Timer", pdMS_TO_TICKS(200), pdTRUE, NULL, TimerCallback);
	xTimerStart(per_TimerHandle, 0);

	/* SERIAL TRANSMITTER TASK */
	xTaskCreate(SerialSend_Task0, "ST0", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);

	/* SERIAL RECEIVER TASK */
	xTaskCreate(SerialReceive_Task0, "SR0", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI0, NULL);
	xTaskCreate(SerialReceive_Task1, "SR1", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI1, NULL);

	/* create a led bar TASK */
	xTaskCreate(led_bar_tsk, "ST", configMINIMAL_STACK_SIZE, NULL, TASK_LED_BAR, NULL);

	/* Kreiranje taska za LCD*/
	xTaskCreate(DisplayLCD_Task, "DSP_LCD", configMINIMAL_STACK_SIZE, NULL, TASK_7SEG, NULL);

	/* Kreiranje taska za obradu podataka */
	xTaskCreate(ObradaPodataka_Task, "OBRADA_MODE", configMINIMAL_STACK_SIZE, NULL, TASK_OBRADA, NULL);

	/* Create TBE semaphore - serial transmit comm */
	TBE_BinarySemaphore = xSemaphoreCreateBinary();

	/* Create RXC semaphore - serial receive comm */
	RXC_BinarySemaphore0 = xSemaphoreCreateBinary();
	RXC_BinarySemaphore1 = xSemaphoreCreateBinary();

	/* Create 7seg semaphore */
	SEVEN_SEG_BinarySemaphore = xSemaphoreCreateBinary();
	//MUTEX_Semaphore = xSemaphoreCreateMutex();
	
	/* Kreiranje reda */
	temperatura_q = xQueueCreate(10, sizeof(uint16_t));

	EV_q = xQueueCreate(10, sizeof(uint16_t));

	MV_q = xQueueCreate(10, sizeof(uint16_t));

	SV_q = xQueueCreate(10, sizeof(uint16_t));
	
	HV_q = xQueueCreate(10, sizeof(uint16_t));

	switch_q = xQueueCreate(10, sizeof(uint16_t));
	
	/* SERIAL TRANSMISSION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);

	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	vTaskStartScheduler();

	r_point = 0;
	r_point1 = 0;

	//while (1);
}