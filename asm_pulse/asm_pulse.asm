;====================================================================;
;--------------------------------------------------------------------;
;
; -A-T-T-R-R-A-C---p-u-l-s-e-_-u-C-  
;
; Pulse generation programm. Gets configuration via TWI from communication uC. 
;
; -TWI BUS-
; The pulse generation uC and a DS1621 are controlled via
; hardware TWI
;			PINC0 = SCL
;			PINC1 = SDA
;
;
;--------------------------------------------------------------------;
;====================================================================;

;======================;
; Runs on a ATMEGA324P ;
;======================;
.include "m324pdef.inc" 

; TODO
; Increment a reset counter that can be read by the host PC
; and set back to zero if desired

; ALIAS FOR ATMEGA324P
.set   EEMWE = EEMPE
.set   EEWE  = EEPE 

.def temp 	   = r16
.def data  	   = r17 
.def n_samples1    = r18
.def n_samples2    = r19
.def n_samples3    = r20
.def eeprom_buffer = r21

.def twi_stat	   = r24

;=======================================;
; TWI Bitrate = 100 kHz for 8 MHz clock ;
;=======================================;
.equ TWI_BIT_RATE	= 32	; Fastest setting
.equ TWI_PRESCALER	= 1
;.equ TWI_BIT_RATE	= 255	; SLOWEST SETTING
;.equ TWI_PRESCALER	= 64



;====================;
; TWI STATUS ALIASES ;
;====================;
.equ START	= 0x08
.equ REP_START  = 0x10
.equ SLA_W_ACK	= 0x18
.equ DAT_W_ACK	= 0x28
.equ SLA_R_ACK	= 0x40
.equ DAT_R_ACK	= 0x50
.equ DAT_R_NACK	= 0x58



;============================;
; TWI communication commands ;
;============================;
.equ ERROR 		= 0x00
.equ SET_NUM_SAMPLES 	= 0x01
.equ SET_MODE		= 0x02
.equ SET_PW		= 0x03
.equ SET_RANGE		= 0x04
.equ SET_RANGE_INCR	= 0x05
.equ START_MSRMNT 	= 0x06



reset:
  	ldi 	temp, LOW(RAMEND)	; LOW-Byte of upper RAM-Adress
        out 	SPL, temp
        ldi 	temp, HIGH(RAMEND)	; HIGH-Byte of upper RAM-Adress
        out 	SPH, temp 


main:
	rcall   twi_read_byte
	rcall	twi_evaluate
	rjmp	main


;====================================;
;    COMMUNICATION STATE MACHINE     ;
;				     ;
; Check if the received byte matches ;
; a command. Than branch to the      ;
; matching subroutine.		     ;
;------------------------------------;
twi_evaluate:
	cpi	data, SET_NUM_SAMPLES
	breq	com_set_num_samples

	cpi	data, START_MSRMNT
	breq	com_start_msrmnt
	
	ldi	data, ERROR		; If no match was found, send error
	rcall	write_byte_usb
	ret				; Return to main


;------------------------------------;
; Sets the 3byte value for n_samples ;
;------------------------------------;
com_set_num_samples:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; get 1st byte from usb
	mov	n_samples1, data; 
	rcall	read_byte_usb		; get 2nd byte from usb
	mov	n_samples2, data; 
	rcall	read_byte_usb		; get 3rd byte from usb
	mov	n_samples3, data; 

	rcall 	n_samples_to_eeprom	; Save value in EEPROM

	ret

;-------------------;
; Start measurement ;
;-------------------;
com_start_msrmnt:
	rcall 	n_samples_from_eeprom	; get n_samples1,2,3 from eeprom
	rcall	pulse_gen		; start pulse generation

	ret

;------------------------------------;
; E N D  COMMUNICATION STATE MACHINE ;
;====================================;


;====================;
; Initialize the TWI ;
;--------------------;
twi_init:
	ldi 	temp, TWI_BIT_RATE	; Set bitrate
	sts 	TWBR, temp
	ldi 	temp, TWI_PRESCALER	; Set prescaler
	sts 	TWSR, temp
	ldi 	temp, (0<<TWINT|1<<TWEA|1<<TWEN) 	
	sts 	TWCR,temp		; Set control register
	
	ret
;====================;


;======================;
; Start a TWI transfer ;
;----------------------;
twi_start:
	ldi 	temp, (1<<TWINT|1<<TWSTA|1<<TWEN)
	sts 	TWCR, temp		; Set Start in control register

wait1:
	lds 	temp,TWCR		; Wait for TWINT Flag set.  
	sbrs 	temp,TWINT		; This indicates that the START
	rjmp 	wait1			; condition has been transmitted.

	lds 	temp,TWSR		; Check value of TWI Status
	andi 	temp, 0xF8		; Register. Mask prescaler bits. If
	cpi 	temp, START		; if status = START return to caller
	breq	exit_twi_start
	cpi	temp, REP_START		; if status = REP_START return to caller
	breq	exit_twi_start
; MAYBE use rcall instead of rjmp
	rjmp 	twi_error		; if not --> ERROR
exit_twi_start:
	ret
;=======================;


;===================;
; Stop TWI transfer ;
;-------------------;
twi_stop:
	ldi	temp, (1<<TWINT|1<<TWEN|1<<TWSTO)
	sts	TWCR, temp

	lds 	temp,TWSR		; Check value of TWI Status
	andi 	temp, 0xF8		; Register. Mask prescaler bits. If

	ret
;===================;


;===================;
; Write byte to TWI ;
;-------------------;
twi_write:
	mov	temp, data		; Set data to data register
	sts	TWDR, temp
	ldi	temp, (1<<TWINT|1<<TWEN)
	sts	TWCR, temp		; Start transmission
wait3:
	lds	temp,TWCR		; Wait for TWINT to be set
	sbrs	temp,TWINT
	rjmp	wait3

	ret
;===================;


;===============================;
; Check the TWI status register ;
;-------------------------------;
twi_check:
	lds	temp,TWSR	
	andi	temp, 0xF8
	cp	temp, twi_stat		; twi_stat = desired value
	brne 	twi_error

	ret
;===============================;


;=====================================;
; Read one byte and continue with ACK ;
;-------------------------------------;
twi_read_ack:
	ldi	temp, (1<<TWINT|1<<TWEA|1<<TWEN)
	sts	TWCR, temp

wait4:
	lds	temp,TWCR		; Wait for TWINT to be set
	sbrs	temp,TWINT
	rjmp	wait4

	lds	data, TWDR		; read data in
	
	ret
;=====================================;


;=======================================;
; Read one byte and terminate with NACK ;
;---------------------------------------;
twi_read:
	ldi	temp, (1<<TWINT|0<<TWEA|1<<TWEN)
	sts	TWCR, temp

wait5:
	lds	temp,TWCR		; Wait for TWINT to be set
	sbrs	temp,TWINT
	rjmp	wait5

	lds	data, TWDR		; read data in
	
	ret
;=======================================;


;===========;
; TWI ERROR ;
;-----------;
twi_error:
ldi data, 0xFF
rcall write_byte_usb
	rcall	twi_stop		; STOP
		
	ldi	temp, (0<<TWEN)		; TWI OFF
	sts	TWCR, temp
	
	rcall 	twi_init		; Init TWI

	ret
;===========;


;================================;
; Write n_samples1 2 3 to EEPROM ;
;--------------------------------;
n_samples_to_eeprom:
	ldi     ZL,low(n1)       	; Set pointer to eeprom address
    	ldi     ZH,high(n1)     	; 
	mov	eeprom_buffer,n_samples1; Copy data to eeprom buffer
	rcall	write_eeprom		; Write buffer to eeprom

	ldi     ZL,low(n2)       	; Same for n2
    	ldi     ZH,high(n2)     	; 
	mov	eeprom_buffer,n_samples2; 
	rcall	write_eeprom		; 

	ldi     ZL,low(n3)       	; Same for n3
    	ldi     ZH,high(n3)     	; 
	mov	eeprom_buffer,n_samples3; 
	rcall	write_eeprom		; 
    
    	ret
;================================;


;======================;
; Write byte to EEPROM ;
;----------------------;
write_eeprom:
	sbic    EECR, EEWE            	; check if last write is already done
    	rjmp    write_eeprom 		; if not, check again

	out     EEARH, ZH               ; Write address
    	out     EEARL, ZL               ; 
    	out     EEDR,eeprom_buffer	; Write data
    	sbi     EECR,EEMWE              ; Prepare write
   	sbi     EECR,EEWE               ; And go!

	ret
;======================;


;=================================;
; Read n_samples1 2 3 from EEPROM ;
;---------------------------------;
n_samples_from_eeprom:
	ldi     ZL,low(n1)       	; Set pointer to eeprom address
    	ldi     ZH,high(n1)     	; 
	rcall	read_eeprom		; Write buffer to eeprom
	mov	n_samples1,eeprom_buffer; Copy data from eeprom buffer

	ldi     ZL,low(n2)       	; Same for n2
    	ldi     ZH,high(n2)     	; 
	rcall	read_eeprom		; 
	mov	n_samples2,eeprom_buffer;

	ldi     ZL,low(n3)       	; Same for n3
    	ldi     ZH,high(n3)     	; 
	rcall	read_eeprom		;
	mov	n_samples3,eeprom_buffer; 
    
    	ret
;================================;


;=======================;
; Read byte from EEPROM ;
;-----------------------;
read_eeprom:
    	sbic    EECR,EEWE		; check if last write is already done		
    	rjmp    read_eeprom   		; if not, check again
 
	out     EEARH, ZH           	; Write address
	out     EEARL, ZL  
    	sbi     EECR, EERE          	; Read
    	in      eeprom_buffer, EEDR	; Copy data to buffer

	ret
;=================================;



;===================;
;  Pulse generator  ;
;-------------------;
pulse_gen:
	
	cbi	PORTC, 7
	sbi	PORTC, 7
	
	nop
	nop

	subi 	n_samples1, 1		; decrement 24bit loop counter 'n_samples'
	sbci 	n_samples2, 0		;
	sbci 	n_samples3, 0		;

	breq	exit_pulse_gen		; exit loop if counter reached zero

	rjmp 	pulse_gen

exit_pulse_gen:
	ret
;===================;


;=============;
; EEPROM data ;
;=============;
.ESEG

; The 3 bytes of n_samples, default = 100
n1:
	.db	100
n2:
	.db	0
n3:
	.db	0
