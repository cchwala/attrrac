.include "m8515def.inc" 

.def temp = r16
.def data  = r17
.def samples = r18

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

; ONLY FAST WRITE OUTPUT
;	rjmp	write_fast_init


;======================;
; Define byte commands ;
;======================;
.equ ERROR = 0x00
.equ SET_NUM_SAMPLES = 0x01
.equ START_MSRMNT = 0x02


reset:

; TODO
; Increment a reset counter that can be read by the host PC
; and set back to zero if desired

  	ldi 	temp, LOW(RAMEND); LOW-Byte of upper RAM-Adress
        out 	SPL, temp
        ldi 	temp, HIGH(RAMEND); HIGH-Byte of upper RAM-Adress
        out 	SPH, temp 

	ldi 	temp, 0xFF	; PORTA is output fot LEDs
	out 	DDRA, temp 	
	out	PORTA, temp	; LEDs off


main:
	rcall	read_byte_usb
	rcall	check_usb_bits
	rjmp	main

;===================================;
; Read single byte command from USB ;
;-----------------------------------;
read_byte_usb:
	ldi 	temp, 0x00	
	out 	DDRB, temp	; PORTB is input
	ldi	temp, 0xFF
	out	PORTB, temp	; pull-ups active

	cbi 	DDRC, 0		; PORTC 0 input
	sbi	PORTC, 0	; pull-up active
	cbi	DDRC, 2		; PORTC 2 input
	sbi	PORTC, 2	; pull-up active

	sbi 	DDRC, 1		; PORTC 1 output
	sbi	PORTC, 1	; set it high
	sbi 	DDRC, 3		; PORTC 3 output
	sbi	PORTC, 3	; set it high

wait_for_RXF:
	sbis	PINC, 0 	; exit loop if RXF (read ready) is low
	rjmp 	read_byte
	rjmp 	wait_for_RXF	

read_byte:
	cbi 	PORTC, 1 	; pull RD low to read from usb chip
	nop			; !!! KEEP THIS FOR TIMING !!!
	in 	data, PINB	; read
	sbi 	PORTC, 1	; set RD to high again
	ret
;-----------------------------------;
;===================================;

;====================================;
; Check if the received byte matches ;
; a command and than branch to it    ;
;------------------------------------;
check_usb_bits:
	cpi	data, SET_NUM_SAMPLES
	breq	com_set_num_samples

	cpi	data, START_MSRMNT
	breq	com_start_msrmnt

	cpi 	data, 0x04
	breq 	com_04
	
	ldi	data, ERROR	; If no match was found, return error
	rcall	write_byte_usb
	ret			; Return to main

com_set_num_samples:
	rcall	write_byte_usb	; write back received command byte to PC
	rcall	read_byte_usb	; get byte from usb
	rcall 	write_byte_usb	; write back received value to PC
	mov	samples, data	; copy data byte to sample variable
	ret

com_start_msrmnt:
	rcall	write_byte_usb
	ret

com_04:
	ldi	data, 0x04
	rcall	write_byte_usb	
	rjmp	main

;============================;
; Write a single byte to USB ;
;----------------------------;
write_byte_usb:
	ldi 	temp, 0xFF	
	out 	DDRB, temp	; PORTB is output

	cbi 	DDRC, 0		; PORTC 0 input
	sbi	PORTC, 0	; pull-up active
	cbi	DDRC, 2		; PORTC 2 input
	sbi	PORTC, 2	; pull-up active

	sbi 	DDRC, 1		; PORTC 1 output
	sbi	PORTC, 1	; set it high
	sbi 	DDRC, 3		; PORTC 3 output
	sbi	PORTC, 3	; set it high
	
wait_for_TXF:
	sbis	PINC, 2
	rjmp	write_byte_usb_bits
	rjmp	wait_for_TXF

write_byte_usb_bits:
	out	PORTB, data
	cbi	PORTC, 3
	sbi	PORTC, 3
	ret
;-----------------------------;
;=============================;


;===================;
; Fast write output ;
;-------------------;
write_fast_init:
	ldi 	temp, 0xFF	
	out 	DDRB, temp	; PORTB is output

	cbi 	DDRC, 0		; PORTC 0 input
	sbi	PORTC, 0	; pull-up active
	cbi	DDRC, 2		; PORTC 2 input
	sbi	PORTC, 2	; pull-up active

	sbi 	DDRC, 1		; PORTC 1 output
	sbi	PORTC, 1	; set it high
	sbi 	DDRC, 3		; PORTC 3 output
	sbi	PORTC, 3	; set it high

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
	nop
	nop
	nop
	nop
	rjmp 	write_fast
;-------------------;
;===================;
