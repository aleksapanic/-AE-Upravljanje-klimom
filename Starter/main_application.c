
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

/* Hardware simulator utility functions */
#include "HW_access.h"

/* SERIAL SIMULATOR CHANNEL TO USE */
#define COM_CH_0 (0)
#define COM_CH_1 (1)

/* MACROS */


	/* TASK PRIORITIES */
#define	TASK_SERIAL_SEND_PRI		( tskIDLE_PRIORITY + 2 )
#define TASK_SERIAl_REC_PRI			( tskIDLE_PRIORITY + 3 )
#define	SERVICE_TASK_PRI			( tskIDLE_PRIORITY + 1 )

/* TASKS: FORWARD DECLARATIONS */
void led_bar_tsk( void *pvParameters );
void SerialSend_Task0(void* pvParameters);
void SerialReceive_Task0(void* pvParameters);
//void SerialSend_Task1(void* pvParameters);
void SerialReceive_Task1(void* pvParameters);
void ManualMode_Task(void* pvParameters);
void DisplayLCD_Task(void* pvParameters);

/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
const char trigger[] = "XYZ";
unsigned volatile t_point;
uint16_t cv = 0; //ova promenljiva cuva vrednost temperature sa senzora
uint16_t temperatura;
static uint16_t EV;
static uint16_t MV;
static uint16_t SV = 0;
static uint16_t IV;
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

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/* GLOBAL OS-HANDLES */
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore0;
SemaphoreHandle_t RXC_BinarySemaphore1;

TimerHandle_t per_TimerHandle;

QueueHandle_t temperatura_q;
/* OPC - ON INPUT CHANGE - INTERRUPT HANDLER */
static uint32_t OnLED_ChangeInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
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

/* PERIODIC TIMER CALLBACK */
static void TimerCallback(TimerHandle_t xTimer)
{ 
	static uint8_t bdt = 0;
	/*set_LED_BAR(2, 0x00);//sve LEDovke iskljucene
	set_LED_BAR(3, 0xF0);// gornje 4 LEDovke ukljucene
	
    set_LED_BAR(0, bdt); // ukljucena LED-ovka se pomera od dole ka gore
	bdt <<= 1;
	if (bdt == 0)
		bdt = 1;*/
}

void led_bar_tsk(void* pvParameters)
{
	unsigned i;
	uint8_t d;
			while (1)
	{  
		/*xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
				get_LED_BAR(1, &d);
		i = 3;
		do
		{
			i--;
			select_7seg_digit(i);
			set_7seg_digit(hexnum[d % 10]);
			d /= 10;
		} while (i > 0);*/
	}
}

/*Sa ovim taskom simuliramo slanje vrednosti sa senzora i to na svakih 200ms. Saljemo karakter 'a'
i u AdvUniCom simulatoru omogucimo opciju AUTO ukljucen*/
void SerialSend_Task0(void* pvParameters)
{
	uint8_t c = (uint8_t)'a';

	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(200));
		if (send_serial_character(COM_CH_0, c) != 0)
		{
			printf("Greska prilikom slanja");
		}
	}
}

/*Ovaj task vrsi prijem podataka sa kanala 0. Ti podaci dolaze u vidu temperature sa senzora*/
void SerialReceive_Task0(void* pvParameters)
{
	uint8_t cc;

	for (;;)
	{
		
		if (xSemaphoreTake(RXC_BinarySemaphore0, portMAX_DELAY) != pdTRUE)
		{
			printf("Greska");
		}

		if (get_serial_character(COM_CH_0, &cc) != 0)
		{
			printf("Greska");
		}

		//kada stignu podaci, salju se u red
		if (cc == (uint8_t)'C')
		{
			r_point = 0;
		}
		else if (cc == (uint8_t)'.')
		{
			char *ostatak;
			printf(" Temperatura je %sC\n", r_buffer);
			temperatura = (uint16_t)strtol(r_buffer, &ostatak, 10);
			if (xQueueSend(temperatura_q, &temperatura, 0) != pdTRUE)
			{
				printf("Neuspesno slanje u red");
			}

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

void SerialSend_Task1(void* pvParameters)
{
	t_point = 0;
	while (1)
	{

		if (t_point > (sizeof(trigger) - 1))
			t_point = 0;
		//send_serial_character(COM_CH_0, trigger[t_point++]);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		//vTaskDelay(pdMS_TO_TICKS(100));// kada se koristi vremenski delay
	}
}

//salje na racunar informacije o trenutnoj i ocekivanoj vrednosti, rezim rada klime i mod klime(ON/OFF)
/*Format poruke: \00C\0d
				\00E45\0d
				\00S1\0d
				\00M\0d*/
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
			if (r_buffer1[0] == 'E' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					//Pristigla naredba je EV(expected value) iz koje je potrebno izvuci brojeve
					//Npr. EV31
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						expected_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				EV = (uint16_t)strtol(expected_value, &ostatak, 10);
				printf("Ocekivana vrednost korisnika: %d\n", EV);
			}
			else if (r_buffer1[0] == 'M' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					//Pristigla naredba je MV(mode value) iz koje je potrebno izvuci broj
					//Npr. MV1
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						mode_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				MV = (uint16_t)strtol(mode_value, &ostatak, 10);
				
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
			else if (r_buffer1[0] == 'S' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					//Pristigla naredba je SV(status value) iz koje je potrebno izvuci broj
					//Npr. SV0
					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						status_value[k] = r_buffer1[i];
						k++;
					}
				}
				char* ostatak;
				SV = (uint16_t)strtol(status_value, &ostatak, 10);
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
			else if (r_buffer1[0] == 'I' && r_buffer1[1] == 'V')
			{
				size_t i;
				for (i = (size_t)0; i < strlen(r_buffer1); i++)
				{
					//Pristigla naredba je IV(info value) iz koje je potrebno izvuci broj
					//Npr. SV0
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

/* Ovaj task prima vrednost za promenu manuelnog rezima i vrsi ON/OFF uz pomoc LED_bar*/
void ManualMode_Task(void* pvParameters)
{
	uint8_t button, d;

	for(;;)
	{ 
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);

		if (MANUELNI == 1)
		{
			get_LED_BAR(1, &d);
			
			
			if (d == (uint8_t)1)
			{
				printf("MAN ON\n");
				set_LED_BAR(0, d);
			}
			else
			{
				printf("MAN OFF\n");
				set_LED_BAR(0, d);
			}
		}
	}
}

/* Ovaj task ispisuje koji je trenutni mod izabran i ispisuje trenutnu temperaturu na displeju*/
void DisplayLCD_Task(void* pvParameters)
{
	for (;;)
	{
		if (MANUELNI == 1)
		{
			vTaskDelay(pdMS_TO_TICKS(200));
			select_7seg_digit(0);
			set_7seg_digit(hexnum[0]);
		}
		else if (AUTOMATSKI == 1)
		{
			vTaskDelay(pdMS_TO_TICKS(200));
			select_7seg_digit(0);
			set_7seg_digit(hexnum[1]);
		}
	}
}

/* MAIN - SYSTEM STARTUP POINT */
void main_demo(void)
{
	init_7seg_comm();
	init_LED_comm();
	init_serial_uplink(COM_CH_0);  // inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(COM_CH_0);// inicijalizacija serijske RX na kanalu 0
	init_serial_uplink(COM_CH_1);  // inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(COM_CH_1);// inicijalizacija serijske RX na kanalu 0

	/* ON INPUT CHANGE INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);

	/* Create LED interrapt semaphore */
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();

	/* create a timer task */
	per_TimerHandle = xTimerCreate("Timer", pdMS_TO_TICKS(500), pdTRUE, NULL, TimerCallback);
	xTimerStart(per_TimerHandle, 0);



	/* SERIAL TRANSMITTER TASK */
	xTaskCreate(SerialSend_Task0, "ST0", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);
	xTaskCreate(SerialSend_Task1, "ST1", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);

	/* SERIAL RECEIVER TASK */
	xTaskCreate(SerialReceive_Task0, "SR0", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);
	xTaskCreate(SerialReceive_Task1, "SR1", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);

	

	/* Create TBE semaphore - serial transmit comm */
	TBE_BinarySemaphore = xSemaphoreCreateBinary();

	/* Create RXC semaphore - serial receive comm */
	//RXC_BinarySemaphore = xSemaphoreCreateBinary();
	RXC_BinarySemaphore0 = xSemaphoreCreateBinary();
	if (RXC_BinarySemaphore0 == NULL)
	{
		printf("Greska prilikom kreiranja\n");
	}

	RXC_BinarySemaphore1 = xSemaphoreCreateBinary();
	if (RXC_BinarySemaphore1 == NULL)
	{
		printf("Greska prilikom kreiranja\n");
	}

	/*Kreiranje reda*/
	temperatura_q = xQueueCreate(10, sizeof(uint16_t));
	if (temperatura_q == NULL)
	{
		printf("Greska prilikom kreiranja\n");
	}

	/* SERIAL TRANSMISSION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);

	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	/* create a led bar TASK */
	xTaskCreate(led_bar_tsk, "ST", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

	/* Kreiranje taska za manuelni mod*/
	xTaskCreate(ManualMode_Task, "MAN_MODE", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

	/* Kreiranje taska za LCD*/
	xTaskCreate(DisplayLCD_Task, "DSP_LCD", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

	vTaskStartScheduler();

	r_point = 0;
	r_point1 = 0;

	//while (1);
}