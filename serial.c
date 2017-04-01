//
// serial.c - (UART) port library for Tiva
//
// v1.0 / 2017-04-01 / Io Engineering / Terje
//

/*

Copyright (c) 2017, Terje Io
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

� Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

� Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

� Neither the name of the copyright holder nor the names of its contributors may
be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "tiva.h"
#include "serial.h"

#if defined(RX_BUFFER_SIZE) || defined(TX_BUFFER_SIZE)

#define BUFCOUNT(head, tail, size) ((head >= tail) ? (head - tail) : (size - tail + head))

static void uart_interrupt_handler (void);

 #ifdef RX_BUFFER_SIZE
  static char rxbuf[RX_BUFFER_SIZE];
  static volatile uint16_t rx_head = 0, rx_tail = 0, rx_overflow = 0, rts_state = 0;
  static bool (*serialReceiveCallback)(int32_t) = 0;
 #endif

 #ifdef TX_BUFFER_SIZE
  static char txbuf[TX_BUFFER_SIZE];
  static volatile uint16_t tx_head = 0, tx_tail = 0;
 #endif

#endif

void serialInit (void) {

#ifdef BACKCHANNEL

    #define UARTCH UART0_BASE
    #define INT_UART INT_UART0

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlDelay(3);

    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

#else

    #define UARTCH UART1_BASE
    #define INT_UART INT_UART1

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    SysCtlDelay(3);

    GPIOPinConfigure(GPIO_PB0_U1RX);
    GPIOPinConfigure(GPIO_PB1_U1TX);
    GPIOPinTypeGPIOOutput(RTS_PORT, RTS_PIN);
    GPIOPinWrite(RTS_PORT, RTS_PIN, 0);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
    SysCtlDelay(3);

    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);

#endif

    UARTClockSourceSet(UARTCH, UART_CLOCK_PIOSC);
    UARTConfigSetExpClk(UARTCH, 16000000, 115200, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));
    UARTFIFOEnable(UARTCH);
    UARTFIFOLevelSet(UARTCH, UART_FIFO_TX1_8, UART_FIFO_RX1_8);

#if defined(RX_BUFFER_SIZE) || defined(TX_BUFFER_SIZE)
    IntPrioritySet(INT_UART, 0x40);
    UARTIntRegister(UARTCH, uart_interrupt_handler);
  #ifdef RX_BUFFER_SIZE
    UARTIntEnable(UARTCH, UART_INT_RX|UART_INT_RT);
  #endif
  #ifdef TX_BUFFER_SIZE
    UARTTxIntModeSet(UARTCH, UART_TXINT_MODE_EOT);
  #endif
#endif

    UARTEnable(UARTCH);
}

#ifdef RX_BUFFER_SIZE

//
// serialReceiveCallback - called before data is inserted into the buffer, return false to drop
//

void setSerialReceiveCallback (bool (*fn)(int32_t)) {
    serialReceiveCallback = fn;
}

//
// serialGetC - returns -1 if no data available
//

int32_t serialGetC (void) {

    int32_t data;
    uint16_t bptr = rx_tail;

    if(bptr == rx_head)
        return -1; // no data available else EOF

//    UARTIntDisable(UARTCH, UART_INT_RX|UART_INT_RT);
    data = rxbuf[bptr++];                   // Get next character, increment tmp pointer
    rx_tail = bptr & (RX_BUFFER_SIZE - 1);  // and update pointer

//    UARTIntEnable(UARTCH, UART_INT_RX|UART_INT_RT);

    if (rts_state && BUFCOUNT(rx_head, rx_tail, RX_BUFFER_SIZE) < RX_BUFFER_LWM)    // Clear RTS if
        GPIOPinWrite(RTS_PORT, RTS_PIN, rts_state = 0);                             // buffer count is below low water mark

    return data;
}

inline uint16_t serialRxCount(void) {

    uint16_t head = rx_head, tail = rx_tail;

    return BUFCOUNT(head, tail, RX_BUFFER_SIZE);
}

uint16_t serialRxFree(void) {
    return RX_BUFFER_SIZE - serialRxCount();
}

void serialRxFlush(void) {
    rx_tail = rx_head;
    GPIOPinWrite(RTS_PORT, RTS_PIN, rts_state = 0);
}

void serialRxCancel(void) {

    rxbuf[rx_head] = CAN;
    rx_tail = rx_head;
    rx_head = (rx_tail + 1) & (RX_BUFFER_SIZE - 1);

    GPIOPinWrite(RTS_PORT, RTS_PIN, rts_state = 0);
}

#else

int32_t serialGetC () {

    int32_t c = -1,  waitCount = 3000;

    while(waitCount && c == -1) {

        if(!UARTCharsAvail(UARTCH)) {
            SysCtlDelay(500);
            waitCount--;
        } else
            c = UARTCharGetNonBlocking(UARTCH);
    }

    return c;
}

#endif

void serialWriteS(const char *data) {

    char c, *ptr = (char *)data;

    while((c = *ptr++) != '\0')
        serialPutC(c);

}

void serialWriteLn(const char *data) {
    serialWriteS(data);
    serialWriteS(EOL);
}

void serialWrite(const char *data, unsigned int length) {

    char *ptr = (char *)data;

    while(length--)
        serialPutC(*ptr++);

}

#ifdef LINE_BUFFER_SIZE

char *serialReadLn (void) {

    static char cmdbuf[LINE_BUFFER_SIZE];

    int32_t c = 0;
    uint32_t count = 0;

    while(c != CR) {
        if((c = serialGetC()) != -1) {

            if(c == CR)
                cmdbuf[count] = '\0';
            else if(c > 31 && count < sizeof(cmdbuf))
                cmdbuf[count++] = (char)c;
            else if(c == EOF)
                c = CR;
            else if(c == DEL && count > 0)
                count--;
        }
    }

    return count ? cmdbuf : 0;
}
#endif

#ifdef TX_BUFFER_SIZE

void serialPutC (const char c) {

    uint32_t next_head;

    if(tx_head != tx_tail || !UARTCharPutNonBlocking(UARTCH, c)) {  // Send character without buffering if possible

        next_head = (tx_head + 1) & (TX_BUFFER_SIZE - 1);           // Get and update head pointer

        while(tx_tail == next_head);                                // Buffer full, block until space is available...

        txbuf[tx_head] = c;                                         // Add data to buffer
        tx_head = next_head;                                        // and update head pointer

        UARTIntEnable(UARTCH, UART_INT_TX); // Enable interrupts
    }
}

uint16_t serialTxCount(void) {

    uint16_t head = tx_head, tail = tx_tail;

    return BUFCOUNT(head, tail, TX_BUFFER_SIZE);
}

#else

void serialPutC (const char c) {
    UARTCharPut(UARTCH, c);
}

#endif

#if defined(RX_BUFFER_SIZE) || defined(TX_BUFFER_SIZE)

static void uart_interrupt_handler (void) {

    uint16_t bptr;
    int32_t data;
    uint32_t iflags = UARTIntStatus(UARTCH, true);

#ifdef TX_BUFFER_SIZE

    if(iflags & UART_INT_TX) {

        bptr = tx_tail;

        if(tx_head != bptr) {

            UARTCharPut(UARTCH, txbuf[bptr++]);                 // Put character in TXT FIFO
            bptr &= (TX_BUFFER_SIZE - 1);                       // and update tmp tail pointer

            while(tx_head != bptr && UARTSpaceAvail(UARTCH)) {  // While data in TX buffer and free space in TX FIFO
                UARTCharPut(UARTCH, txbuf[bptr++]);             // put next character
                bptr &= (TX_BUFFER_SIZE - 1);                   // and update tmp tail pointer
            }

            tx_tail = bptr;                                     //  Update tail pinter

            if(bptr == tx_head)                                 // Disable TX  interrups
                UARTIntDisable(UARTCH, UART_INT_TX);            // when TX buffer empty

/*        } else {
            UARTIntDisable(UARTCH, UART_INT_TX);
            UARTIntClear(UARTCH, UART_INT_TX); */
        }

    }

#endif

#ifdef RX_BUFFER_SIZE

    if(iflags & (UART_INT_RX|UART_INT_RT)) {

        bptr = (rx_head + 1) & (RX_BUFFER_SIZE - 1);    // Get next head pointer

        if(bptr == rx_tail) {                           // If buffer full
            rx_overflow = 1;                            // flag overlow
            UARTCharGet(UARTCH);                        // and do dummy read to clear interrupt;
        } else {
            data = UARTCharGet(UARTCH);
            if(!serialReceiveCallback || serialReceiveCallback(data)) {
                rxbuf[rx_head] = data;                  // Add data to buffer
                rx_head = bptr;                         // and update pointer
            }
        }

        if (!rts_state && BUFCOUNT(rx_head, rx_tail, RX_BUFFER_SIZE) >= RX_BUFFER_HWM)
            GPIOPinWrite(RTS_PORT, RTS_PIN, rts_state = RTS_PIN);

    }

#endif

}

#endif
