/* Standard includes. */
#include <stdio.h>l.g 
#include <conio.h>

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


	/* TASK PRIORITIES */
#define	TASK_SERIAL_SEND_PRI		( tskIDLE_PRIORITY + 2 )
#define TASK_SERIAl_REC_PRI			( tskIDLE_PRIORITY + 3 )
#define	SERVICE_TASK_PRI		( tskIDLE_PRIORITY + 1 )

/* TASKS: FORWARD DECLARATIONS */
void led_bar_tsk( void *pvParameters );
void SerialSend_Task0(void* pvParameters);
void SerialReceive_Task0(void* pvParameters);
void SerialSend_Task1(void* pvParameters);
void SerialReceive_Task1(void* pvParameters);

/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
const char trigger[] = "XYZ";
unsigned volatile t_point;
uint8_t cv = 0; //ova promenljiva cuva vrednost temperature sa senzora

/* RECEPTION DATA BUFFER */
#define R_BUF_SIZE (32)
uint16_t r_buffer[R_BUF_SIZE];
uint16_t r_buffer1[R_BUF_SIZE];
uint16_t r_buffer2[R_BUF_SIZE];
static uint8_t volatile r_point, r_point1;

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/* GLOBAL OS-HANDLES */
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore0;
SemaphoreHandle_t RXC_BinarySemaphore1;

TimerHandle_t per_TimerHandle;

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
	set_LED_BAR(2, 0x00);//sve LEDovke iskljucene
	set_LED_BAR(3, 0xF0);// gornje 4 LEDovke ukljucene
	
    set_LED_BAR(0, bdt); // ukljucena LED-ovka se pomera od dole ka gore
	bdt <<= 1;
	if (bdt == 0)
		bdt = 1;
}

/* MAIN - SYSTEM STARTUP POINT */
void main_demo( void )
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

	r_point = 0;

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


	/* SERIAL TRANSMISSION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);

	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	/* create a led bar TASK */
	xTaskCreate(led_bar_tsk, "ST",	configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

	vTaskStartScheduler();

	while (1);
}

void led_bar_tsk(void* pvParameters)
{
	unsigned i;
	uint8_t d;
			while (1)
	{  
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
				get_LED_BAR(1, &d);
		i = 3;
		do
		{
			i--;
			select_7seg_digit(i);
			set_7seg_digit(hexnum[d % 10]);
			d /= 10;
		} while (i > 0);
	}
}

//Slanje podataka preko serijske na kanalu 0
void SerialSend_Task0(void* pvParameters)
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


//salje na racunar podatak o vrednosti sa senzora na kanalu 0
void SerialReceive_Task0(void* pvParameters)
{
	
	uint8_t cc;
	static uint8_t loca = 0;
	char *ostatak;
	while (1)
	{
		xSemaphoreTake(RXC_BinarySemaphore0, portMAX_DELAY);// ceka na serijski prijemni interapt
		get_serial_character(COM_CH_0, &cc);//ucitava primljeni karakter u promenjivu cc
		//printf("primio karakter: %c\n", (char)cc);// prikazuje primljeni karakter u cmd prompt
		r_buffer[r_point++] = cc;
		
		//printf("primio karakter: %c\n", r_buffer[r_point++]);
		//na serijskoj ce slovo C oznacavati da se radi o stepenima Celzijusovim,
		//a ujedno ce oznaciti kraj upisa vrednosti temperature sa senzora
		if (cc == 'C')
		{
			cv = strtol(r_buffer, &ostatak, 10);
			//printf("primio vrednost: %d\n", (uint8_t)cv);
			r_point = 0;
			//r_buffer[0] = '\0';
			//r_buffer[1] = '\0';
			
		}

		/*
		if (cc == 0x00) // ako je primljen karakter 0, inkrementira se vrednost u GEX formatu na ciframa 5 i 6
		{		
			r_point = 0;
			//select_7seg_digit(2);
			//set_7seg_digit(hexnum[loca >> 4]);
			//select_7seg_digit(3);
			//set_7seg_digit(hexnum[loca & 0x0F]);
			loca++;
		}
		else if (cc == 0xff)// za svaki KRAJ poruke, prikazati primljenje bajtove direktno na displeju 3-4
		{
			
			//select_7seg_digit(0);
			//set_7seg_digit(hexnum[r_buffer[0]]);
			//select_7seg_digit(1);
			//set_7seg_digit(hexnum[r_buffer[1]]);
		}
		else if (r_point < R_BUF_SIZE)// pamti karaktere izmedju 0 i FF
		{
			
			for (uint8_t i = 0; i < sizeof(r_buffer); i++)
			{
				//printf("primio karakter: %u\n", (unsigned)r_buffer[i]);
			}

		}*/
		
	}
}

//salje na racunar informacije o trenutnoj i ocekivanoj vrednosti, rezim rada klime i mod klime(ON/OFF)
/*Format poruke: \00C\0d
				\00E45\0d
				\00S1\0d
				\00M\0d*/
void SerialReceive_Task1(void* pvParameters)
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
			if (r_buffer1[0] == 'C')
			{
				printf("Trenutna temperatura je %d stepeni Celzijusovih. \n", (uint8_t)cv);
				//r_buffer2[0] = r_buffer[0];
				//r_buffer2[1] = r_buffer[1];
				printf("%c%c", r_buffer[0],r_buffer[1]);
				//ovaj deo mora opet da se pogleda posto nece da ispisuje kako treba
				select_7seg_digit(0);
				set_7seg_digit(hexnum[r_buffer[0]]);
				select_7seg_digit(1);
				set_7seg_digit(hexnum[r_buffer[1]]);
			}
		
			else
			{
				printf("Nepoznata naredba");
			}

			r_buffer1[0] = '\0';
			r_buffer1[1] = '\0';
			r_buffer1[2] = '\0';
		}
		else
		{
			r_buffer1[r_point1++] = (char)cc;
		}
	}
}