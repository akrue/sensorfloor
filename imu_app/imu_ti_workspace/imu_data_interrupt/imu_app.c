/* Includes */
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Event.h>

#include <ti/drivers/UART.h>

#include "sensors/SensorI2C.h"
#include "sensors/SensorMpu9250.h"

#include "Board.h"


#define START_PRINT_EVT         Event_Id_00

#define BUFFER_SIZE 28
#define FREQ 7

/* Variable Declaration */
static PIN_Handle buttonPinHandle;
static PIN_State buttonPinState;

PIN_Config buttonPinTable[] = {
    IOID_16  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    Board_PIN_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};

static Event_Handle event;
static Event_Params eventParams;
static Event_Struct structEvent;

/*  ======== buttonCallbackFxn ======== */
void buttonCallbackFxn(PIN_Handle handle, PIN_Id pinId) {
    /* Debounce logic, only toggle if the button is still pushed */
    //CPUdelay(1000); //TODO maybe it after testing
    if (!PIN_getInputValue(pinId)) {
        switch (pinId) {
            case IOID_16:
                Event_post(event, START_PRINT_EVT);
                break;
            case Board_PIN_BUTTON1:
                Event_post(event, START_PRINT_EVT);
                break;
            default:
                break;
        }
    }
}
/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    //const char  progStart[] = "Prog Start\r\n";
    //const char  ISRInfo[] = "ISR called\r\n";
    //const char  printInfo[] = "Print finished\r\n";
    UART_Handle uart;
    UART_Params uartParams;
    //char msg[5] = 0;

    uint16_t buffer[BUFFER_SIZE][9]; // buffer 100 readings (3 accel, 3 gyro, 3 mag)

    //uint16_t accel_x, accel_y, accel_z;
    //float accelx, accely, accelz;
    //uint16_t gyro_x, gyro_y, gyro_z;
    //float gyrox, gyroy, gyroz;
    //uint16_t mag_x, mag_y, mag_z;
    uint8_t data[6];

    uint8_t index = 255; //overflow when increment to skip increment for first time
    uint32_t loop_time, loop_delay_count, delay_count;
    bool event_status = false;
    bool buffer_overflowed = false;
    uint8_t start_index, flushed_count = 0, last_index;
    uint16_t no_of_readings;
    int i;

    Event_Params_init(&eventParams);
    Event_construct(&structEvent, &eventParams);
    event = Event_handle(&structEvent);

    /* Call driver init functions */
    UART_init();

    /* Create a UART with data processing off. */
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_BINARY;
    uartParams.readDataMode = UART_DATA_BINARY;
    uartParams.readReturnMode = UART_RETURN_FULL;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.baudRate = 921600;

    uart = UART_open(Board_UART0, &uartParams);

    if (uart == NULL) {
        /* UART_open() failed */
        while (1);
    }

    buttonPinHandle = PIN_open(&buttonPinState, buttonPinTable);
    if(!buttonPinHandle) {
       /* Error initializing button pins */
       while(1);
    }

    /* Setup callback for button pins */
    if (PIN_registerIntCb(buttonPinHandle, &buttonCallbackFxn) != 0) {
       /* Error registering button callback function */
       while(1);
    }

    //UART_write(uart, progStart, sizeof(progStart));

    if (SensorI2C_open())
    {
        SensorMpu9250_init();           // Gyroscope and accelerometer
        SensorMpu9250_powerOn();
        SensorMpu9250_enable(0x003F); //enable all Axes for acc and gyro
        SensorMpu9250_accSetRange(ACC_RANGE_2G);
    }

    /* buffer data and check for event*/
    // timeout input in Event_pend function controls the buffer time
    loop_time = (uint32_t) 16000000/FREQ; /* in cycles */ /* CPU_delay takes 3 cycles - CPU clock 48MHz - 48MHz/3=16MHz */
    loop_delay_count = (uint32_t)loop_time/100000;

    while(1) {
        //UART_write(uart, "loop: ", 6); ltoa(loop_delay_count, msg); UART_write(uart, msg, strlen(msg)); UART_write(uart, "\r\n", 2);

        while(1) {
            if (event_status == true) {break;}

            if(index == BUFFER_SIZE-1) {index = 0; buffer_overflowed=true;}
            else                       {index++;}

            /* Accel */
            SensorMpu9250_accRead((uint16_t*) &data);
            buffer[index][0] = (((int16_t)data[1]) << 8) | data[0];
            buffer[index][1] = (((int16_t)data[3]) << 8) | data[2];
            buffer[index][2] = (((int16_t)data[5]) << 8) | data[4];
            /* Mag */
            SensorMpu9250_gyroRead((uint16_t*) &data);
            buffer[index][3] = (((int16_t)data[1]) << 8) | data[0];
            buffer[index][4] = (((int16_t)data[3]) << 8) | data[2];
            buffer[index][5] = (((int16_t)data[5]) << 8) | data[4];
            /* Gyro */
            SensorMpu9250_magRead((uint16_t*) &data);
            buffer[index][6] = (((int16_t)data[1]) << 8) | data[0];
            buffer[index][7] = (((int16_t)data[3]) << 8) | data[2];
            buffer[index][8] = (((int16_t)data[5]) << 8) | data[4];

            for(delay_count = 0; delay_count < 100000; delay_count++)
            {
                if (Event_pend(event, Event_Id_NONE, START_PRINT_EVT, 0))
                {
                    event_status = true;
                    break;
                } else  {
                    CPUdelay(loop_delay_count);
                }
            }
        }

        /* flush the buffer */
        // handling buffer_overflow
        // no overflow {0:last}
        // overflowed  {last+1:BUFFER_SIZE-1 and 0:last}
        if      (!buffer_overflowed) {start_index=0; last_index=index; no_of_readings = index+1; }
        else if (buffer_overflowed)  {start_index=index+1; no_of_readings = BUFFER_SIZE; }
        i = start_index;

        UART_write(uart, "[[", 2);

        UART_write(uart, &no_of_readings, 2);

        while(1)
        {
            /* Accel */
            UART_write(uart, &buffer[i][0], 2);
            UART_write(uart, &buffer[i][1], 2);
            UART_write(uart, &buffer[i][2], 2);
            /* Mag */
            UART_write(uart, &buffer[i][3], 2);
            UART_write(uart, &buffer[i][4], 2);
            UART_write(uart, &buffer[i][5], 2);
            /* Gyro */
            UART_write(uart, &buffer[i][6], 2);
            UART_write(uart, &buffer[i][7], 2);
            UART_write(uart, &buffer[i][8], 2);

            i++;

            // no overflow
            if (!buffer_overflowed && i == last_index+1) {break;}
            // overflow
            if (buffer_overflowed && i==BUFFER_SIZE) {i = 0;}
            flushed_count++;
            if (flushed_count == BUFFER_SIZE) {break;}
        }
        UART_write(uart, "]]", 2);
        UART_write(uart, "\n", 2);

        //UART_write(uart, "No: ", 4); ltoa(no_of_prints, msg); UART_write(uart, msg, strlen(msg)); UART_write(uart, "\r\n", 2);

        //reset
        event_status = false;
        index = 255;
        start_index = 0;
        flushed_count = 0;
        buffer_overflowed = false;
    }
}
