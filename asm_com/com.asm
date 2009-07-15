.include "m8515def.inc" 

.def temp = r16
.def data  = r17
.def n_samples1 = r18
.def n_samples2 = r19
.def n_samples3 = r20
.def eeprom_buffer = r21

;====================================================================;
;--------------------------------------------------------------------;
;
; USB COMMUNICATION PROGRAMM
;
; Wait for commands, one byte long, from PC via FT245R USB chip and
; evaluate them.
;
; PORTA = LED output
; PORTB = I/O via USB
; PORTC = control USB	PINC0 = RXF flag INPUT
;			PINC1 = RD flag OUTPUT
;			PINC2 = TXF flag INPUT
;			PINC3 = WR flag OUTPUT
;
;--------------------------------------------------------------------;
;====================================================================;

; ASM ASM ASM

; ONLY FAST WRITE OUTPUT
;	rcall	write_fast_init


;======================;
; Define byte commands ;
;======================;
.equ ERROR 		= 0x00
.equ SET_NUM_SAMPLES 	= 0x01
.equ SET_MODE		= 0x02
.equ SET_PW		= 0x03
.equ SET_RANGE		= 0x04
.equ SET_RANGE_INCR	= 0x05
.equ START_MSRMNT 	= 0x06
.equ SET_CASE_TEMP	= 0x07


reset:

; TODO
; Increment a reset counter that can be read by the host PC
; and set back to zero if desired

  	ldi 	temp, LOW(RAMEND)	; LOW-Byte of upper RAM-Adress
        out 	SPL, temp
        ldi 	temp, HIGH(RAMEND)	; HIGH-Byte of upper RAM-Adress
        out 	SPH, temp 

	ldi 	temp, 0xFF		; PORTA is output fot LEDs
	out 	DDRA, temp 	
	out	PORTA, temp		; LEDs off


main:
	rcall	read_byte_usb
	rcall	check_usb_bits
	rjmp	main

;===================================;
; Read single byte command from USB ;
;-----------------------------------;
read_byte_usb:
	ldi 	temp, 0x00	
	out 	DDRB, temp		; PORTB is input
	ldi	temp, 0xFF
	out	PORTB, temp		; pull-ups active

	cbi 	DDRC, 0			; PORTC 0 input
	sbi	PORTC, 0		; pull-up active
	cbi	DDRC, 2			; PORTC 2 input
	sbi	PORTC, 2		; pull-up active

	sbi 	DDRC, 1			; PORTC 1 output
	sbi	PORTC, 1		; set it high
	sbi 	DDRC, 3			; PORTC 3 output
	sbi	PORTC, 3		; set it high

wait_for_RXF:
	sbis	PINC, 0 		; exit loop if RXF (read ready) is low
	rjmp 	read_byte
	rjmp 	wait_for_RXF	

read_byte:
	cbi 	PORTC, 1 		; pull RD low to read from usb chip
	nop				; !!! KEEP THIS FOR TIMING !!!
	in 	data, PINB		; read
	sbi 	PORTC, 1		; set RD to high again
	ret
;===================================;


;====================================;
;    COMMUNICATION STATE MACHINE     ;
;				     ;
; Check if the received byte matches ;
; a command. Than branch to the      ;
; matching subroutine.		     ;
;------------------------------------;
check_usb_bits:
	cpi	data, SET_NUM_SAMPLES
	breq	com_set_num_samples

	cpi	data, START_MSRMNT
	breq	com_start_msrmnt

	cpi	data, SET_CASE_TEMP
	breq	com_set_case_temp
	
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

	rcall	n_samples_from_eeprom	; Get value from EEPROM

	mov	data, n_samples1; 
	rcall 	write_byte_usb		; write back 1st byte
	mov	data, n_samples2; 
	rcall 	write_byte_usb		; write back 2st byte
	mov	data, n_samples3; 
	rcall 	write_byte_usb		; write back 3st byte
	ret

;-------------------;
; Start measurement ;
;-------------------;
com_start_msrmnt:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall 	n_samples_from_eeprom	; get n_samples1,2,3 from eeprom
	rcall	write_fast_init		; write data n_sample-times
	ret

;----------------------------------;
; Set the desired case temperature ;
;----------------------------------;
com_set_case_temp:
	nop
;====================================;


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


;============================;
; Write a single byte to USB ;
;----------------------------;
write_byte_usb:
	ldi 	temp, 0xFF	
	out 	DDRB, temp		; PORTB is output

	cbi 	DDRC, 0			; PORTC 0 input
	sbi	PORTC, 0		; pull-up active
	cbi	DDRC, 2			; PORTC 2 input
	sbi	PORTC, 2		; pull-up active

	sbi 	DDRC, 1			; PORTC 1 output
	sbi	PORTC, 1		; set it high
	sbi 	DDRC, 3			; PORTC 3 output
	sbi	PORTC, 3		; set it high
	
wait_for_TXF:
	sbis	PINC, 2
	rjmp	write_byte_usb_bits
	rjmp	wait_for_TXF

write_byte_usb_bits:
	out	PORTB, data
	cbi	PORTC, 3
	sbi	PORTC, 3
	ret
;=============================;


;===================;
; Fast write output ;
;-------------------;
write_fast_init:
	ldi 	temp, 0xFF	
	out 	DDRB, temp		; PORTB is output

	cbi 	DDRC, 0			; PORTC 0 input
	sbi	PORTC, 0		; pull-up active
	cbi	DDRC, 2			; PORTC 2 input
	sbi	PORTC, 2		; pull-up active

	sbi 	DDRC, 1			; PORTC 1 output
	sbi	PORTC, 1		; set it high
	sbi 	DDRC, 3			; PORTC 3 output
	sbi	PORTC, 3		; set it high

	ldi	temp, 0

write_fast:
	out 	PORTB, temp
	cbi	PORTC, 3
	sbi	PORTC, 3
	inc	temp
	out 	PORTB, temp
	cbi	PORTC, 3
	sbi	PORTC, 3
	inc	temp
	nop
	nop

	subi 	n_samples1, 1		; decrement 24bit loop counter 'n_samples'
	sbci 	n_samples2, 0		;
	sbci 	n_samples3, 0		;
	breq	exit_write_fast		; exit loop if counter reached zero

	rjmp 	write_fast

exit_write_fast:
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
