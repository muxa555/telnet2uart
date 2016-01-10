#include "lpc17xx.h"
#include "type.h"
#include "uart.h"

volatile uint32_t UART0Status;
uint8_t UART0SendBuffer[TXBUFSIZE], UART0RecvBuffer[RXBUFSIZE];
const uint8_t txBufEnd = UART0SendBuffer + TXBUFSIZE, rxBufEnd = UART0RecvBuffer + RXBUFSIZE; // rxBufThreshold = RXBUFSIZE * 3 / 4;
volatile uint8_t *UART0RBHead = UART0RecvBuffer, *UART0SBHead = UART0SendBuffer, *UART0SBTail = UART0SendBuffer;

uint32_t UARTInit( uint32_t baudrate )
{
	uint32_t Fdiv;
	uint32_t pclkdiv, pclk;

	LPC_PINCON->PINSEL0 &= ~0x000000F0;
	LPC_PINCON->PINSEL0 |= 0x00000050;	/* RxD0 is P0.3 and TxD0 is P0.2 */
	/* By default, the PCLKSELx value is zero, thus, the PCLK for
	* 		all the peripherals is 1/4 of the SystemFrequency. */
	/* Bit 6~7 is for UART0 */
	pclkdiv = (LPC_SC->PCLKSEL0 >> 6) & 0x03;
	switch ( pclkdiv )
	{
		case 0x00:
		default:
			pclk = SystemFrequency/4;
			break;
		case 0x01:
			pclk = SystemFrequency;
			break; 
		case 0x02:
			pclk = SystemFrequency/2;
			break; 
		case 0x03:
			pclk = SystemFrequency/8;
			break;
	}

	LPC_UART0->LCR = 0x03;			/* 8 bits, no parity, 1 stop bit */
	Fdiv = ( pclk / 16 ) / baudrate ;		/* baud rate */
	LPC_UART0->DLM = Fdiv / 256;								
	LPC_UART0->DLL = Fdiv % 256;
	LPC_UART0->LCR = 0x03;			/* DLAB = 0 */
	LPC_UART0->FCR = 0x07;			/* Enable and reset TX and RX FIFO. */

	NVIC_EnableIRQ(UART0_IRQn);
	LPC_UART0->IER = IER_RBR | IER_THRE | IER_RLS;		/* Enable UART0 interrupt */

	return (TRUE);
}

void UART0_IRQHandler() 
{
	uint8_t IIRValue;
	
	IIRValue = LPC_UART0->IIR;
		
	IIRValue >>= 1;			/* skip pending bit in IIR */
	IIRValue &= 0x07;			/* check bit 1~3, interrupt identification */
	if ( IIRValue == IIR_RLS )		/* Receive Line Status */
	{
		uint8_t LSRValue;
		LSRValue = LPC_UART0->LSR;
		/* Receive Line Status */
		if ( LSRValue & ( LSR_OE | LSR_PE | LSR_FE | LSR_RXFE | LSR_BI ) )
		{
			/* There are errors or break interrupt */
			/* Read LSR will clear the interrupt */
			UART0Status = LSRValue;
			uint8_t dummy = LPC_UART0->RBR;		/* Dummy read on RX to clear 
												interrupt, then bail out */
			return;
		}
		if ( LSRValue & LSR_RDR )	/* Receive Data Ready */			
		{
			/* If no error on RLS, normal ready, save into the data buffer. */
			/* Note: read RBR will clear the interrupt */
			if ( UART0RBHead != rxBufEnd ) // buffer not full
			{
				*UART0RBHead = LPC_UART0->RBR;
				UART0RBHead++;
			}
			else 
			{
				// send
			}
		}
	}
	else if ( IIRValue == IIR_RDA )	/* Receive Data Available */
	{
		/* Receive Data Available */
		if ( UART0RBHead != rxBufEnd ) // buffer not full
		{
			*UART0RBHead = LPC_UART0->RBR;
			UART0RBHead++;
		}
		else 
		{
			// send
		}
	}
	else if ( IIRValue == IIR_CTI )	/* Character timeout indicator */
	{
		/* Character Time-out indicator */
		UART0Status |= 0x100;		/* Bit 9 as the CTI error */
	}
	else if ( IIRValue == IIR_THRE )	/* THRE, transmit holding register empty */
	{
		/* THRE interrupt */
		uint8_t LSRValue = LPC_UART0->LSR;		/* Check status in the LSR to see if
												valid data in U0THR or not */
		if ( LSRValue & LSR_THRE )
		{
			UART0TxEmpty = 1;
			if ( UART0SBTail != UART0SBHead ) // Transmit FIFO not empty
			{
				LPC_UART1->THR = *UART0SBTail;
				++UART0SBTail;
				if( UART0SBTail == txBufEnd ) 
				{
					UART0SBTail = UART0SendBuffer;
				}
			}
		}
	}
}