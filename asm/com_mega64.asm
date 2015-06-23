;====================================================================;
;--------------------------------------------------------------------;
;
; -A-T-T-R-R-A-C---c-o-m-_-u-C-  
;
; Master control and USB communication programm for the pulse 
; generation PCB board. The main loop waits for commands from a PC
; via the FT2232H USB iC and controlls the pulse generation CPLD and
; a DS1621 thermostat
; 
; -TWI BUS-
; The pulse generation CPLD and a DS1621 are controlled via
; hardware TWI
;			PIND0 = SCL
;			PIND1 = SDA
;
; -USB COMMUNICATION-
; Waits for commands, one byte long, from PC via FT245R USB chip and
; evaluate them.
;
; PORTB PORTC = control USB	PINB6 PINC4 = RXF flag INPUT
;				PINB4 PINC5 = RD flag OUTPUT
;				PINB7 PINC6 = TXF flag INPUT
;				PINB5 PINC7 = WR flag OUTPUT
; PORTE PORTD = I/O via USB
;
; 
;
;--------------------------------------------------------------------;
;====================================================================;

;====================;
; Runs on a ATMEGA64 ;
;====================;
.include "m64def.inc" 

; ALIAS FOR ATMEGA324P
;.set   EEMWE = EEMPE
;.set   EEWE  = EEPE 

.def temp 	   = r16
.def data  	   = r17 	; holds the byte sent via USB or TWI
.def buffer1       = r18
.def buffer2       = r19
.def buffer3 	   = r20
.def counter	   = r21
.def t_msb 	   = r22	; Case temperature MSB
.def t_lsb	   = r23	; Case temperature LSB
.def twi_stat	   = r24
.def flags    	   = r25

.equ FLAG_T1_COMPARE = 1	; timer_1 compare flag


.equ MAX_USB_READ_TRIES = 100	; Max number of tries to read from usb

;=======================================;
; TWI Bitrate = 100 kHz for 16 MHz clock ;
;=======================================;
.equ TWI_BIT_RATE	= 18
.equ TWI_PRESCALER	= 1


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


;====================;
; DS1621 TWI ALIASES ;
;====================;
.equ THERMO_ADDRS_W	= 0b10010000	; Address for case thermostate writting
.equ THERMO_ADDRS_R	= 0b10010001	; Address for case thermostate reading

.equ THERMO_ADDRS_BRD_W = 0b10011110	; Same for thermostat on PCB
.equ THERMO_ADDRS_BRD_R = 0b10011111	; 

.equ READ_TEMP		= 0xAA		; Read temperature
.equ SET_TH		= 0xA1		; Set high temp limit
.equ SET_TL		= 0xA2		; Set low temp limit
.equ SET_CFG		= 0xAC		; Set config register
.equ START_CONV		= 0xEE		; Start conversion
.equ STOP_CONV		= 0x22		; Stop conversion

;==================;
; CPLD TWI ALIASES ;
;==================;

.equ CPLD_ADDRS_W	= 0b00001010	; Address for writing
.equ CPLD_ADDRS_R	= 0b00001011	; Address for reading

;==============================================;
; Communication commands. Same for CPLD and PC ;
;==============================================;
.equ ERROR 		= 0xEE		; Status aliases
.equ CPLD_BUSY		= 0xBB
.equ OK			= 0xF0
.equ DONE		= 0xF1

.equ SET_PW		= 0x02		; Puslse generator setting (CPLD)
.equ SET_DELAY		= 0x04
.equ SET_NUM_SAMPLES 	= 0x06
.equ SET_MODE		= 0x08
.equ SET_ADC		= 0x0A
.equ GET_STATUS		= 0x0C
.equ SET_POL_PRECEDE	= 0x12
.equ SET_ATTEN22	= 0x14
.equ SET_ATTEN35	= 0x18

.equ GET_LOCK		= 0xD0		; Get lock indicators from HF oscillators

.equ START_MSRMNT 	= 0x1E		; Measurement functions
.equ START_SLOW_LOOP	= 0x2E
.equ STOP_SLOW_LOOP	= 0x3E

.equ SET_CASE_TEMP	= 0x07		; Thermostat function
.equ GET_CASE_TEMP	= 0x17
.equ SET_BOARD_TEMP	= 0x03
.equ GET_BOARD_TEMP	= 0x13

.equ SET_RESET_COUNT	= 0x09		; Reset functions
.equ GET_RESET_COUNT	= 0x19

.equ GET_ADC4		= 0xA4		; Get voltage from internal ADCs
.equ GET_ADC5		= 0xA5	
.equ GET_ADC6		= 0xA6
.equ GET_ADC7		= 0xA7

.equ FOO_CMD		= 0x7B 		; 0x7b = 123

;#############################################################################
;                          P R O G R A M    S T A R T
;#############################################################################

.org 0x0000
	rjmp	reset

.org OC1Aaddr
	rjmp	timer1_compare		; timer1_compare is used for slowed down measurement loop

reset:
  	ldi 	temp, LOW(RAMEND)	; Init stack pointer
        out 	SPL, temp
        ldi 	temp, HIGH(RAMEND)	
        out 	SPH, temp

	; Timer1 configuration for 10 Hz (overflow at 6250)
	; Timer1 configuration for 20 Hz (overflow at 3125)
	ldi     temp, high( 3125 - 1 ) ; compare value
        out     OCR1AH, temp
        ldi     temp, low( 3125 - 1 )
        out     OCR1AL, temp
        ldi     temp, ( 1 << WGM12 ) | ( 1 << CS12 ); CTC mode enable, prescaler to 8
        out     TCCR1B, temp
        ldi     temp, 1 << OCIE1A  	; OCIE1A: Interrupt at timer compare
        out     TIMSK, temp

	; external ADC control pins
	cbi 	DDRC, 6			; PORTC 6 = EOLC = input
	sbi	PORTC, 6		; pull-up active
	sbi 	DDRC, 5			; PORTC 5 = RD = output
	sbi	PORTC, 5		; set it high
	sbi 	DDRC, 4			; PORTC 4 = WR = output
	sbi	PORTC, 4		; set it high
	sbi 	DDRC, 3			; PORTC 3 = CS = output
	sbi	PORTC, 3		; set it high
	
	; USB status flags
	cbi 	DDRB, 6			; PORTB 6 = RXF = input
	sbi	PORTB, 6		; pull-up active
	cbi	DDRB, 7			; PORTB 7 = TXE = input
	sbi	PORTB, 7		; pull-up active

	; USB control pins
	sbi 	DDRB, 4			; PORTB 4 = RD = output
	sbi	PORTB, 4		; set it high
	sbi 	DDRB, 5			; PORTB 5 = WR = output
	cbi	PORTB, 5		; set it low

	; CPLD BUSY pin
	cbi 	DDRB, 0 		; CPLD_BUSY = PORTB0 input 
	sbi	PORTB, 0		; pull-up active

	; Lock indicators input pins
	cbi 	DDRD, 7 		; 35 GHz DRO (high = locked)
	cbi 	DDRD, 6 		; 22 GHZ DRO (high = locked)
	cbi 	DDRD, 5 		; 22 GHZ PLO (high = locked)

	; enable internal ADC
	ldi     temp, (1<<ADEN) | (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0)
    	out     ADCSRA, temp		; prescaler 128 bit

	; Switch the watchdog on
	rcall 	wdt_on			

	; Set watchdog timer prescaler to maximum = 2 seconds
	wdr
	in 	r16, WDTCR
	ldi 	r16, (1<<WDCE)|(1<<WDE)
	ori 	r16, (1<<WDCE)|(1<<WDE)	
	out 	WDTCR, r16		; Write logical one to WDCE and WDE
	ldi 	temp, (0<<WDCE)|(1<<WDP0)|(1<<WDP1)|(1<<WDP2) 
	out 	WDTCR, temp		; Set prescaler to 0b111

	rcall	incr_reset_counter	; Increment the reset counter

	; !!! HANGS IF TWI SLAVE NOT CONNECTED OR TWI BUS BROKEN
	rcall	init_thermostat		; Initialize the DS1621 over TWI (not yet installed)
	rcall	init_board_thermostat	; Initialize the DS1621 soldered to the board

main:
	wdr				; feed the watchdog

	rcall	read_byte_usb_max_tries	; try to read byte from usb for
					; MAX_USB_READ_TRIES times
	brtc    main			; jmp to main if T_FLAG is cleared, 
					; thas is, no byte was received
	rjmp	check_usb_bits		; otherwise, check which command was received



;====================================;
;    COMMUNICATION STATE MACHINE     ;
;				     ;
; Check if the received byte matches ;
; a command. Than branch to the      ;
; matching subroutine.		     ;
;------------------------------------;
check_usb_bits:
	cpi	data, SET_NUM_SAMPLES
	breq	jmp_set_num_samples

	cpi	data, SET_PW
	breq	jmp_set_pw

	cpi	data, SET_DELAY
	breq	jmp_set_delay

	cpi	data, SET_MODE
	breq	jmp_set_mode

	cpi	data, START_MSRMNT
	breq	jmp_start_msrmnt

	cpi	data, START_SLOW_LOOP
	breq	jmp_start_slow_loop

	cpi	data, SET_CASE_TEMP
	breq	jmp_set_case_temp

	cpi	data, GET_CASE_TEMP
	breq	jmp_get_case_temp

	cpi	data, SET_BOARD_TEMP
	breq	jmp_set_board_temp

	cpi	data, GET_BOARD_TEMP
	breq	jmp_get_board_temp

	cpi	data, SET_RESET_COUNT
	breq	jmp_set_reset_count

	cpi	data, GET_RESET_COUNT
	breq	jmp_get_reset_count

	cpi	data, SET_ADC
	breq	jmp_set_adc

	cpi	data, GET_STATUS
	breq	jmp_get_status

	cpi	data, SET_POL_PRECEDE
	breq	jmp_set_pol_precede

	cpi	data, SET_ATTEN22
	breq	jmp_set_atten22

	cpi	data, SET_ATTEN35
	breq	jmp_set_atten35

	cpi	data, GET_LOCK
	breq	jmp_get_lock

	cpi	data, GET_ADC4
	breq	jmp_get_adc4

	cpi	data, GET_ADC5
	breq	jmp_get_adc5

	cpi	data, GET_ADC6
	breq	jmp_get_adc6

	cpi	data, GET_ADC7
	breq	jmp_get_adc7

	cpi	data, FOO_CMD
	breq	jmp_foo_cmd
		
	ldi	data, ERROR		; If no match was found, send error
	rcall	write_byte_usb
	rjmp	main			; Return to main

; Jump table
jmp_set_num_samples:
	rjmp com_set_num_samples

jmp_set_pw:
	rjmp	com_set_pw

jmp_set_delay:
	rjmp 	com_set_delay

jmp_set_mode:
	rjmp	com_set_mode

jmp_start_msrmnt:
	rjmp	com_start_msrmnt

jmp_start_slow_loop:
	rjmp	com_start_slow_loop

jmp_set_case_temp:
	rjmp	com_set_case_temp

jmp_get_case_temp:
	rjmp 	com_get_case_temp

jmp_set_board_temp:
	rjmp	com_set_board_temp

jmp_get_board_temp:
	rjmp	com_get_board_temp

jmp_set_reset_count:
	rjmp	com_set_reset_count

jmp_get_reset_count:
	rjmp	com_get_reset_count

jmp_set_adc:
	rjmp	com_set_adc

jmp_get_status:
	rjmp	com_get_status

jmp_set_pol_precede:
	rjmp	com_set_pol_precede

jmp_set_atten22:
	rjmp	com_set_atten22

jmp_set_atten35:
	rjmp	com_set_atten35

jmp_get_lock:
	rjmp	com_get_lock

jmp_get_adc4:
	rjmp	com_get_adc4

jmp_get_adc5:
	rjmp	com_get_adc5

jmp_get_adc6:
	rjmp	com_get_adc6

jmp_get_adc7:
	rjmp	com_get_adc7

jmp_foo_cmd:
	rjmp	com_foo_cmd

;-------------------------------------;
; Sets the 3 byte value for n_samples ;
;-------------------------------------;
com_set_num_samples:
					; --- Get data from USB ---
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; get 1st byte from usb
	mov	buffer1, data; 
	rcall	read_byte_usb		; get 2nd byte from usb
	mov	buffer2, data;
	rcall	read_byte_usb		; get 3nd byte from usb
	mov	buffer3, data;
	
	mov	data, buffer1 
	rcall 	write_byte_usb		; write back 1st byte
	mov	data, buffer2
	rcall 	write_byte_usb		; write back 2nd byte
	mov	data, buffer3
	rcall 	write_byte_usb		; write back 3rd byte

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_NUM_SAMPLES	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer1		; Write 1st byte to CPLD
	rcall	twi_write		; 
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer2		; Write 2nd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check	

	mov	data, buffer3		; Write 3rd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check	

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main


;-------------------------;
; Set pulse width in CPLD ;
;-------------------------;
com_set_pw:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read PW byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; PW on stack	

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_PW		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	pop	data			; Pop PW from stack
	rcall	twi_write		; Write it to CPLD
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;-------------------;
; Set delay in CPLD ;
;-------------------;
com_set_delay:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; get 1st byte from usb
	mov	buffer1, data; 
	rcall	read_byte_usb		; get 2nd byte from usb
	mov	buffer2, data; 

	mov	data, buffer1 
	rcall 	write_byte_usb		; write back 1st byte
	mov	data, buffer2 
	rcall 	write_byte_usb		; write back 2st byte

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_DELAY		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer1		; Write 1st byte to CPLD
	rcall	twi_write		; 
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer2		; Write 2nd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop
	
	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;------------------;
; Set mode in CPLD ;
;------------------;
com_set_mode:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read PW byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; PW on stack
	
	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_MODE		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	pop	data			; Pop PW from stack
	rcall	twi_write		; Write it to CPLD
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;-------------------;
; Start measurement ;
;-------------------;
com_start_msrmnt:
	rcall	write_byte_usb		; write back received command byte to PC

init_ADC_channels:
	ldi 	temp, 0xFF
	sts	DDRF, temp		; PORTF is output
	sts	PORTF, temp		; All pins high

					; Write 1111 to activate all four ADC channels
	cbi	PORTC, 3		; pull CS low
	cbi	PORTC, 4		; pull WR low

	sbi	PORTC, 4		; release WR
	sbi	PORTC, 3		; release CS
			
init_read_ADC:				
	ldi 	temp, 0x00		; NOTE: STS is used instead of OUT because PORTF is in extended I/O range
	sts 	DDRF, temp		; PORTF is input (actually only the first 4 bytes are used)
	ldi	temp, 0xFF
	sts	PORTF, temp		; pull-ups active
	
	ldi 	temp, 0x00	
	out 	DDRA, temp		; PORTA is input
	ldi	temp, 0xFF
	out	PORTA, temp		; pull-ups active

	ldi	counter, 0

start_CPLD_pulser:
;	sbis	pinb, 0			; --- Check CPLD_BUSY ---
;	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
;	ldi	data, OK		; if not busy send OK
;	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, START_MSRMNT	; Send start command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop


wait_for_EOLC_high:			; wait while ADC is converting
	sbic	pinb, 0			; exit if measurement is done = CPLD not busy = pinb0 = high
	rjmp	main
	sbis	PINC, 6			; wait for EOLC to go low (ADC converting)
	rjmp	wait_for_EOLC_high
wait_for_EOLC_low:
	sbic	PINC, 6 		; wait for EOLC to go low (conversion done
	rjmp	wait_for_EOLC_low

					; Read and write the four ADC channels. With the counter byte
					; that makes 9 transmitted bytes per cylce
read_4_channels:
	mov	data, counter		; wirte counter byte
	rcall	write_byte_usb_no_wait

	cbi	PORTC, 3		; pull CS low

	cbi	PORTC, 5		; pull RD low 
	nop				; wait on cycle!!!
	in 	data, PINF		; read in D0,D1,D2,D3 from ADC (lds is used instead of in)
	rcall	write_byte_usb_no_wait	; write it to PC over USB
	in	data, PINA		; read in D4,D5,...,D11
	rcall	write_byte_usb_no_wait	; wirte it
	sbi	PORTC, 5		; release RD

	cbi	PORTC, 5		; three more times
	nop				
	in 	data, PINF	
	rcall	write_byte_usb_no_wait
	in	data, PINA
	rcall	write_byte_usb_no_wait
	sbi	PORTC, 5

	cbi	PORTC, 5		
	nop				
	in 	data, PINF	
	rcall	write_byte_usb_no_wait
	in	data, PINA
	rcall	write_byte_usb_no_wait
	sbi	PORTC, 5
	 	
	cbi	PORTC, 5		
	nop				
	in 	data, PINF	
	rcall	write_byte_usb_no_wait
	in	data, PINA
	rcall	write_byte_usb_no_wait
	sbi	PORTC, 5

	sbi	PORTC, 3		; release CS
	
	inc	counter			; incr counter +3
	inc 	counter
	inc	counter

	wdr				; feed watchdog

	rjmp	wait_for_EOLC_high


;----------------------------------------------------------;
; Loop which reads a burst of samples and repeats after    ;
; a timer interupt so that low sampling rates are possible ;
;----------------------------------------------------------;
;
; interrupt routine for timer1 compare
timer1_compare:
	sbr 	flags,(1<<FLAG_T1_compare); set compare flag
	reti

com_start_slow_loop:
	rcall	write_byte_usb		; write back received command byte to PC
	
					; init ADC channels
	ldi 	temp, 0xFF
	sts	DDRF, temp		; PORTF is output
	sts	PORTF, temp		; All pins high

					; Write 1111 to activate all four ADC channels
	cbi	PORTC, 3		; pull CS low
	cbi	PORTC, 4		; pull WR low

	sbi	PORTC, 4		; release WR
	sbi	PORTC, 3		; release CS
			
					; init_read_ADC				
	ldi 	temp, 0x00		; NOTE: STS is used instead of OUT because PORTF is in extended I/O range
	sts 	DDRF, temp		; PORTF is input (actually only the first 4 bytes are used)
	ldi	temp, 0xFF
	sts	PORTF, temp		; pull-ups active
	
	ldi 	temp, 0x00	
	out 	DDRA, temp		; PORTA is input
	ldi	temp, 0xFF
	out	PORTA, temp		; pull-ups active

	ldi	counter, 0
	rcall	twi_init		; Init TWI

	cbr 	flags,(1<<FLAG_T1_compare); clear timer1 compare flag
	sei				; set global interrupt enable
	rjmp	wait_for_timer1_compare	; wait for timer1_compare match

wait_for_timer1_compare:
	sbrs	flags,FLAG_T1_compare	; check if compare flag is set
	rjmp	wait_for_timer1_compare	; if not, continue wait loop
	cbr	flags,(1<<FLAG_T1_compare); if yes, clear flag and proceed towards measure loop

	sbic	PINB, 6			; check if a byte was received over USB (RXF is low) to signal stop
	rjmp	burst_read_loop_init	; if not, proceed to measure loop
	cli				; if yes, disable interupts and exit
	rjmp	exit_slow_loop

burst_read_loop_init:	
	; FIRST get case temperature
	rcall	twi_init		; Init TWI

	rcall	twi_start

	ldi	data, THERMO_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check
			
	ldi	data, READ_TEMP		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_start		; Send start again to begin read process

	ldi	data, THERMO_ADDRS_R	; Send address for thermostat + read
	rcall	twi_write
	ldi	twi_stat, SLA_R_ACK	; Check status (read ACK)
	rcall	twi_check

	rcall	twi_read_ack		; Read first byte answer with ACK
	mov 	t_msb, data		; copy to MSB
	ldi	twi_stat, DAT_R_ACK	; Check status (data received, ACK sent)
	rcall	twi_check

	rcall	twi_read		; Read second byte and terminate with NACK
	mov	t_lsb, data		; copy to LSB
	ldi	twi_stat, DAT_R_NACK	; Check status (data received, NACK sent)
	rcall	twi_check

	rcall	twi_stop

	mov 	data, t_lsb		; Transmit to PC
	rcall	write_byte_usb
	mov	data, t_msb
	rcall	write_byte_usb

	; SECOND get board temperature
	rcall	twi_init		; Init TWI

	rcall	twi_start

	ldi	data, THERMO_ADDRS_BRD_W; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check
			
	ldi	data, READ_TEMP		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_start		; Send start again to begin read process

	ldi	data, THERMO_ADDRS_BRD_R; Send address for thermostat + read
	rcall	twi_write
	ldi	twi_stat, SLA_R_ACK	; Check status (read ACK)
	rcall	twi_check

	rcall	twi_read_ack		; Read first byte answer with ACK
	mov 	t_msb, data		; copy to MSB
	ldi	twi_stat, DAT_R_ACK	; Check status (data received, ACK sent)
	rcall	twi_check

	rcall	twi_read		; Read second byte and terminate with NACK
	mov	t_lsb, data		; copy to LSB
	ldi	twi_stat, DAT_R_NACK	; Check status (data received, NACK sent)
	rcall	twi_check

	rcall	twi_stop

	mov 	data, t_lsb		; Transmit to PC
	rcall	write_byte_usb
	mov	data, t_msb
	rcall	write_byte_usb

	; THIRD get accelerometer1 value from adc4
	ldi     temp, (1<<REFS0) | (1<<MUX2); ADC4 with AVCC reference, no gain
    	out     ADMUX, temp

	sbi    	ADCSRA, ADSC  		; start ADC conversion 
burst_loop_wait_adc4:
    	sbic    ADCSRA, ADSC       	; wait for end of conversion (ADSC = 0)
    	rjmp    burst_loop_wait_adc4
 
    	in      data, ADCL          	; transfer low byte
	rcall	write_byte_usb
    	in      data, ADCH        	; transfer high byte
	rcall	write_byte_usb

	; FOURTH get acceleroemter2 value from adc5
	ldi     temp, (1<<REFS0) | (1<<MUX2) | (1<<MUX0); ADC5 with AVCC reference, no gain
    	out     ADMUX, temp

	sbi    	ADCSRA, ADSC  		; start ADC conversion
burst_loop_wait_adc5:
    	sbic    ADCSRA, ADSC       	; wait for end of conversion (ADSC = 0)
    	rjmp    burst_loop_wait_adc5
 
    	in      data, ADCL          	; transfer low byte
	rcall	write_byte_usb
    	in      data, ADCH        	; transfer high byte
	rcall	write_byte_usb

	; FIFTH get reset count
	ldi     ZL,low(reset_counter)  	; Set pointer to eeprom address
    	ldi     ZH,high(reset_counter) 	; 
	rcall	read_eeprom		; Read buffer from eeprom
	mov	data, temp		; Copy data from eeprom buffer

	rcall	write_byte_usb		; And transmit it to PC

	; THEN start burst measunring loop
	rcall	twi_start		; --- START TWI ---
		
	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, START_MSRMNT	; Send start command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

burst_read_loop:

burst_wait_for_EOLC_high:		; wait while ADC is converting
	sbic	pinb, 0			; if measurement is done = CPLD not busy = pinb0 = high
	rjmp	wait_for_timer1_compare
;	rjmp	exit_burst_read_loop	; exit the burst read loop
	sbis	PINC, 6			; wait for EOLC to go high (ADC converting)
	rjmp	burst_wait_for_EOLC_high
burst_wait_for_EOLC_low:
	sbic	PINC, 6 		; wait for EOLC to go low (conversion done)
	rjmp	burst_wait_for_EOLC_low

					; Read and write the four ADC channels. With the counter byte
					; that makes 9 transmitted bytes per cylce
burst_read_4_channels:
	mov	data, counter		; write counter byte
	rcall	write_byte_usb_no_wait

	cbi	PORTC, 3		; pull CS low

	cbi	PORTC, 5		; pull RD low 
	nop				; wait on cycle!!!
	in 	data, PINF		; read in D0,D1,D2,D3 from ADC (lds is used instead of in)
	rcall	write_byte_usb_no_wait	; write it to PC over USB
	in	data, PINA		; read in D4,D5,...,D11
	rcall	write_byte_usb_no_wait	; wirte it
	sbi	PORTC, 5		; release RD

	cbi	PORTC, 5		; three more times
	nop				
	in 	data, PINF	
	rcall	write_byte_usb_no_wait
	in	data, PINA
	rcall	write_byte_usb_no_wait
	sbi	PORTC, 5

	cbi	PORTC, 5		
	nop				
	in 	data, PINF	
	rcall	write_byte_usb_no_wait
	in	data, PINA
	rcall	write_byte_usb_no_wait
	sbi	PORTC, 5
	 	
	cbi	PORTC, 5		
	nop				
	in 	data, PINF	
	rcall	write_byte_usb_no_wait
	in	data, PINA
	rcall	write_byte_usb_no_wait
	sbi	PORTC, 5

	sbi	PORTC, 3		; release CS
	
	inc	counter			; incr counter +3
	inc 	counter
	inc	counter

	wdr				; feed watchdog

	rjmp	burst_wait_for_EOLC_high; 

exit_burst_read_loop:
;	ldi	data, DONE		; when done with one burst, write done message
;	rcall	write_byte_usb
	rjmp	wait_for_timer1_compare	; and wait for timer1_compare to trigger next burst

exit_slow_loop:
;	rcall	twi_stop
	rcall	read_byte_usb		; get the command from USB (should be STOP_LOOP)
	ldi	data, STOP_SLOW_LOOP		
	rcall	write_byte_usb		; write back STOP_LOOP
	rjmp	main


;----------------------------------;
; Set the desired case temperature ;
;----------------------------------;
com_set_case_temp:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read T_HIGH byte from USB
	rcall	write_byte_usb		; Write it back for error checking	
	push 	data			; T_HIGH on stack

	rcall	twi_init		; Init TWI


	rcall	twi_start		; --- SET T_HIGH ----

	ldi	data, THERMO_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_TH		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

;	rcall	read_byte_usb		; Read T_HIGH byte from USB
;	rcall	write_byte_usb		; Write it back for error checking
	pop	data			; T_HIGH from stack
	rcall	twi_write		; Write it to thermostat
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check
	push 	data			; T_HIGH on stack

	ldi	data, 0			; SET TH LSB to 0
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	rcall	twi_start		; --- SET T_LOW ----

	ldi	data, THERMO_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_TL		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	pop	data			; Pop T_HIGH from stack
	rcall	twi_write		; And write as T_LOW to thermostat
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	ldi	data, 0			; SET TL LSB to 0
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	rjmp	main

;----------------------;
; Get case temperature ;
;----------------------;
com_get_case_temp:
	rcall	write_byte_usb		; write back received command byte to PC
	
	rcall	twi_init		; Init TWI

	rcall	twi_start

	ldi	data, THERMO_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check
			
	ldi	data, READ_TEMP		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_start		; Send start again to begin read process

	ldi	data, THERMO_ADDRS_R	; Send address for thermostat + read
	rcall	twi_write
	ldi	twi_stat, SLA_R_ACK	; Check status (read ACK)
	rcall	twi_check

	rcall	twi_read_ack		; Read first byte answer with ACK
	mov 	t_msb, data		; copy to MSB
	ldi	twi_stat, DAT_R_ACK	; Check status (data received, ACK sent)
	rcall	twi_check

	rcall	twi_read		; Read second byte and terminate with NACK
	mov	t_lsb, data		; copy to LSB
	ldi	twi_stat, DAT_R_NACK	; Check status (data received, NACK sent)
	rcall	twi_check

	rcall	twi_stop

	mov 	data, t_lsb		; Transmit to PC
	rcall	write_byte_usb
	mov	data, t_msb
	rcall	write_byte_usb

	rjmp	main

;-----------------------------------;
; Set the desired board temperature ; no heating connected, so not really important
;-----------------------------------;
com_set_board_temp:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read T_HIGH byte from USB
	rcall	write_byte_usb		; Write it back for error checking	
	push 	data			; T_HIGH on stack

	rcall	twi_init		; Init TWI


	rcall	twi_start		; --- SET T_HIGH ----

	ldi	data, THERMO_ADDRS_BRD_W; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_TH		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

;	rcall	read_byte_usb		; Read T_HIGH byte from USB
;	rcall	write_byte_usb		; Write it back for error checking
	pop	data			; T_HIGH on stack
	rcall	twi_write		; Write it to thermostat
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check
	push 	data			; T_HIGH on stack

	ldi	data, 0			; SET TH LSB to 0
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	rcall	twi_start		; --- SET T_LOW ----

	ldi	data, THERMO_ADDRS_BRD_W; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_TL		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	pop	data			; Pop T_HIGH from stack
	rcall	twi_write		; And write as T_LOW to thermostat
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	ldi	data, 0			; SET TL LSB to 0
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	rjmp	main

;-----------------------;
; Get board temperature ;
;-----------------------;
com_get_board_temp:
	rcall	write_byte_usb		; write back received command byte to PC
	
	rcall	twi_init		; Init TWI

	rcall	twi_start

	ldi	data, THERMO_ADDRS_BRD_W; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check
			
	ldi	data, READ_TEMP		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_start		; Send start again to begin read process

	ldi	data, THERMO_ADDRS_BRD_R; Send address for thermostat + read
	rcall	twi_write
	ldi	twi_stat, SLA_R_ACK	; Check status (read ACK)
	rcall	twi_check

	rcall	twi_read_ack		; Read first byte answer with ACK
	mov 	t_msb, data		; copy to MSB
	ldi	twi_stat, DAT_R_ACK	; Check status (data received, ACK sent)
	rcall	twi_check

	rcall	twi_read		; Read second byte and terminate with NACK
	mov	t_lsb, data		; copy to LSB
	ldi	twi_stat, DAT_R_NACK	; Check status (data received, NACK sent)
	rcall	twi_check

	rcall	twi_stop

	mov 	data, t_lsb		; Transmit to PC
	rcall	write_byte_usb
	mov	data, t_msb
	rcall	write_byte_usb

	rjmp	main

;-------------------;
; Set reset counter ;
;-------------------;
com_set_reset_count:
	rcall	write_byte_usb		; write back received command byte to PC

	ldi     ZL,low(reset_counter)  	; Set pointer to eeprom address
    	ldi     ZH,high(reset_counter) 	; 
	ldi	temp, 0			; Copy '0' to buffer
	rcall	write_eeprom		; Write buffer to eeprom

	rjmp	main

;-------------------;
; Get reset counter ;
;-------------------;
com_get_reset_count:
	rcall	write_byte_usb		; write back received command byte to PC

	ldi     ZL,low(reset_counter)  	; Set pointer to eeprom address
    	ldi     ZH,high(reset_counter) 	; 
	rcall	read_eeprom		; Read buffer from eeprom
	mov	data, temp		; Copy data from eeprom buffer

	rcall	write_byte_usb		; And transmit it to PC

	rjmp	main

;---------------;
; Set ADC delay ;
;---------------;
com_set_adc:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read ADC delay byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; ADC delay on stack	

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_ADC		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	pop	data			; Pop delay from stack
	rcall	twi_write		; Write it to CPLD
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;----------------------;
; Get status from CPLD ;
;----------------------;
com_get_status:
	rcall	write_byte_usb		; write back received command byte to PC
	
	rcall	twi_init		; Init TWI

	rcall	twi_start

	ldi	data, CPLD_ADDRS_R	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check
			
	ldi	data, GET_STATUS	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_start		; Send start again to begin read process

	ldi	data, CPLD_ADDRS_R	; Send address for thermostat + read
	rcall	twi_write
	ldi	twi_stat, SLA_R_ACK	; Check status (read ACK)
	rcall	twi_check

	rcall	twi_read_ack		; Read byte answer with ACK
	ldi	twi_stat, DAT_R_ACK	; Check status (data received, ACK sent)
	rcall	twi_check


; !!!!!!!!!!!!!!!!!OR THE SAME BUT WITH NACK !!!!!!!!!!
;
;	rcall	twi_read		; Read second byte and terminate with NACK
;	ldi	twi_stat, DAT_R_NACK	; Check status (data received, NACK sent)
;	rcall	twi_check

	rcall	twi_stop

	rcall	write_byte_usb

	rjmp	main

;----------------------------------------------;
; Set time the polarizer precedes the TX pulse ;
;----------------------------------------------;
com_set_pol_precede:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read pol_preced byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; pol_preced on stack	

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_POL_PRECEDE	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	pop	data			; Pop pol_preced from stack
	rcall	twi_write		; Write it to CPLD
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main



;-----------------------;
; Set 22 GHz attenuator ;
;-----------------------;
com_set_atten22:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; get 1st byte from usb
	mov	buffer1, data; 
	rcall	read_byte_usb		; get 2nd byte from usb
	mov	buffer2, data; 

	mov	data, buffer1 
	rcall 	write_byte_usb		; write back 1st byte
	mov	data, buffer2 
	rcall 	write_byte_usb		; write back 2st byte

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_ATTEN22	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer1		; Write 1st byte to CPLD
	rcall	twi_write		; 
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer2		; Write 2nd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;-----------------------;
; Set 35 GHz attenuator ;
;-----------------------;
com_set_atten35:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; get 1st byte from usb
	mov	buffer1, data; 
	rcall	read_byte_usb		; get 2nd byte from usb
	mov	buffer2, data; 

	mov	data, buffer1 
	rcall 	write_byte_usb		; write back 1st byte
	mov	data, buffer2 
	rcall 	write_byte_usb		; write back 2st byte

	sbis	pinb, 0			; --- Check CPLD_BUSY ---
	rjmp	cpld_is_busy		; send error message if busy (pinb0 = low)
	ldi	data, OK		; if not busy send OK
	rcall	write_byte_usb

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_ATTEN35	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer1		; Write 1st byte to CPLD
	rcall	twi_write		; 
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer2		; Write 2nd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;-----------------------------------------;
; Get lock indicators from HF oscillators ;
;-----------------------------------------;
com_get_lock:
	rcall	write_byte_usb
	
	in	data, PIND		; read in PORTD (three lock indicator bits are 5,6,7)
	andi	data, 0b11100000	; mask the 5 lowest bits

	rcall	write_byte_usb		; transfer byte

	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp	main

;-----------------------------------------------------;
; Funcitons to read and transfer internal ADC voltage ;
;-----------------------------------------------------;
;- ADC4 -;
com_get_adc4:
	rcall	write_byte_usb		; write back received command byte to PC
	
	ldi     temp, (1<<REFS0) | (1<<MUX2); ADC4 with AVCC reference, no gain
    	out     ADMUX, temp

	sbi    	ADCSRA, ADSC  		; start ADC conversion
 
wait_adc4:
    	sbic    ADCSRA, ADSC       	; wait for end of conversion (ADSC = 0)
    	rjmp    wait_adc4
 
    	in      data, ADCL          	; transfer low byte
	rcall	write_byte_usb
    	in      data, ADCH        	; transfer high byte
	rcall	write_byte_usb
 
	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp 	main

;- ADC5 -;
com_get_adc5:
	rcall	write_byte_usb		; write back received command byte to PC
	
	ldi     temp, (1<<REFS0) | (1<<MUX2) | (1<<MUX0); ADC5 with AVCC reference, no gain
    	out     ADMUX, temp

	sbi    	ADCSRA, ADSC  		; start ADC conversion
 
wait_adc5:
    	sbic    ADCSRA, ADSC       	; wait for end of conversion (ADSC = 0)
    	rjmp    wait_adc5
 
    	in      data, ADCL          	; transfer low byte
	rcall	write_byte_usb
    	in      data, ADCH        	; transfer high byte
	rcall	write_byte_usb
 
	ldi	data, DONE		; Send done message
	rcall	write_byte_usb
	
	rjmp 	main

;- ADC6 -;
com_get_adc6:
	rcall	write_byte_usb		; write back received command byte to PC
	
	ldi     temp, (1<<REFS0) | (1<<MUX2) | (1<<MUX1); ADC6 with AVCC reference, no gain
    	out     ADMUX, temp

	sbi    	ADCSRA, ADSC  		; start ADC conversion
 
wait_adc6:
    	sbic    ADCSRA, ADSC       	; wait for end of conversion (ADSC = 0)
    	rjmp    wait_adc6
 
    	in      data, ADCL          	; transfer low byte
	rcall	write_byte_usb
    	in      data, ADCH        	; transfer high byte
	rcall	write_byte_usb
 
	ldi	data, DONE		; Send done message
	rcall	write_byte_usb


	rjmp 	main
;- ADC7 -;
com_get_adc7:
	rcall	write_byte_usb		; write back received command byte to PC
	
	ldi     temp, (1<<REFS0) | (1<<MUX2) | (1<<MUX1) | (1<<MUX0); ADC4 with AVCC reference, no gain
    	out     ADMUX, temp

	sbi    	ADCSRA, ADSC  		; start ADC conversion
 
wait_adc7:
    	sbic    ADCSRA, ADSC       	; wait for end of conversion (ADSC = 0)
    	rjmp    wait_adc7
 
    	in      data, ADCL          	; transfer low byte
	rcall	write_byte_usb
    	in      data, ADCH        	; transfer high byte
	rcall	write_byte_usb
 
	ldi	data, DONE		; Send done message
	rcall	write_byte_usb

	rjmp 	main

;------------------------------------;
; use this to try out some foo stuff ;
;------------------------------------;
com_foo_cmd:
	rcall	write_byte_usb
	sbi 	DDRB, 0			; PORTB0 = CDPL_BUSY = output
	sbi	PORTB, 0		; 
	cbi	PORTB, 0		; 
	sbi	PORTB, 0		; 
	rjmp 	main


;------------------------------------;
; E N D  COMMUNICATION STATE MACHINE ;
;====================================;

;------------------------------------;
; Send error message if cpld is busy ;
;------------------------------------;
cpld_is_busy:
	ldi	data, CPLD_BUSY
	rcall	write_byte_usb
	rjmp	main



;#############################################################################
;		           T W I    M E T H O D S
;#############################################################################

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
	ldi 	data, 0xFF
	rcall 	write_byte_usb
	rcall	twi_stop		; STOP
		
	ldi	temp, (0<<TWEN)		; TWI OFF
	sts	TWCR, temp
	
	rcall 	twi_init		; Init TWI

	ret
;===========;


;#############################################################################
; 			 E E P R O M    M E T H O D S
;#############################################################################

;================================;
; Write buffer1 2 3 to EEPROM ;
;--------------------------------;
n_samples_to_eeprom:
	ldi     ZL, low(n1)       	; Set pointer to eeprom address
    	ldi     ZH, high(n1)     	; 
	mov	temp, buffer1	; Copy data to eeprom buffer
	rcall	write_eeprom		; Write buffer to eeprom

	ldi     ZL, low(n2)       	; Same for n2
    	ldi     ZH, high(n2)     	; 
	mov	temp, buffer2; 
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
    	out     EEDR, temp		; Write data
    	sbi     EECR, EEMWE              ; Prepare write
   	sbi     EECR, EEWE               ; And go!

	ret
;======================;


;=================================;
; Read buffer1 2 3 from EEPROM ;
;---------------------------------;
n_samples_from_eeprom:
	ldi     ZL, low(n1)       	; Set pointer to eeprom address
    	ldi     ZH, high(n1)     	; 
	rcall	read_eeprom		; Write buffer to eeprom
	mov	buffer1,temp		; Copy data from eeprom buffer

	ldi     ZL, low(n2)       	; Same for n2
    	ldi     ZH, high(n2)     	; 
	rcall	read_eeprom		; 
	mov	buffer2, temp;
  
    	ret
;================================;


;=======================;
; Read byte from EEPROM ;
;-----------------------;
read_eeprom:
    	sbic    EECR, EEWE		; check if last write is already done		
    	rjmp    read_eeprom   		; if not, check again
 
	out     EEARH, ZH           	; Write address
	out     EEARL, ZL  
    	sbi     EECR, EERE          	; Read
    	in      temp, EEDR	; Copy data to buffer

	ret
;=================================;

;#############################################################################
;			  U S B    M E T H O D S
;#############################################################################


;====================================================;
; Read single byte command from USB (unlimited tries);
;----------------------------------------------------;
read_byte_usb:
	ldi 	temp, 0x00	
	out 	DDRE, temp		; PORTE is input
	ldi	temp, 0xFF
	out	PORTE, temp		; pull-ups active

wait_for_RXF:
	sbis	PINB, 6 		; exit wait loop if RXF (read ready) is low
	rjmp 	read_byte
	rjmp 	wait_for_RXF

read_byte:
	cbi 	PORTB, 4 		; pull RD low to read from usb chip
	nop				; !!! KEEP THIS FOR TIMING !!!
	in 	data, PINE		; read
	sbi 	PORTB, 4		; set RD to high again
	ret

;====================================================;


;====================================================;
; Read single byte command from USB (limited tries)  ;
;----------------------------------------------------;
read_byte_usb_max_tries:
	ldi 	temp, 0x00	
	out 	DDRE, temp		; PORTD is input
	ldi	temp, 0xFF
	out	PORTE, temp		; pull-ups active

	clr	counter			; clear the counter for the wait loop

wait_for_RXF_max_tries:
	sbis	PINB, 6 		; exit wait loop if RXF (read ready) is low
	rjmp 	read_byte_max_tries
	inc	counter			; if RXF != low, increment counter
	cpi	counter, MAX_USB_READ_TRIES; if counter reaches MAX_TRIES
	breq	no_byte_read		; exit without having read a byte
	rjmp 	wait_for_RXF_max_tries

read_byte_max_tries:
	cbi 	PORTB, 4 		; pull RD low to read from usb chip
	nop				; !!! KEEP THIS FOR TIMING !!!
	in 	data, PINE		; read
	sbi 	PORTB, 4		; set RD to high again
	set				; set the T_FLAG which is checked in main
	ret

no_byte_read:
	clt				; clear the T_FLAG which is checked in main
	ret
	
;===================================;

;============================;
; Write a single byte to USB ;
;----------------------------;
write_byte_usb:
	ldi 	temp, 0xFF	
	out 	DDRE, temp		; PORTE is output
	
wait_for_TXF:
	sbis	PINB, 7
	rjmp	write_byte_usb_bits
	rjmp	wait_for_TXF

write_byte_usb_bits:
	out	PORTE, data
	sbi	PORTB, 5
	cbi	PORTB, 5

	ret
;=============================;

;====================================================;
; Write a single byte to USB and do not wait for TXF ;
;----------------------------------------------------;
write_byte_usb_no_wait:
	ldi 	temp, 0xFF	
	out 	DDRE, temp		; PORTE is output

write_byte_usb_bits_no_wait:
	out	PORTE, data
	sbi	PORTB, 5
	cbi	PORTB, 5

	ret
;=============================;


;===================;
; Fast write output ;
;-------------------;
write_fast_init:
	ldi 	temp, 0xFF	
	out 	DDRE, temp		; PORTE is output

;
	ldi	temp, 0

	ldi	buffer1, 0x80
	ldi	buffer2, 0x1a
	ldi	buffer3, 0x6

;	mov	buffer1, n_samples1
;	mov	buffer2, n_samples2

;	rjmp	write_fast_with_wait

write_fast:
; write witout paying attention to the TXF flag
	out 	PORTE, buffer1
	cbi	PORTB, 5		; write
	sbi	PORTB, 5
	inc 	temp
	out 	PORTE, buffer2
	cbi	PORTB, 5		; write
	sbi	PORTB, 5
;	inc 	temp
	
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	subi 	buffer1, 1		; decrement 16bit loop counter
	sbci 	buffer2, 0
	sbci	buffer3, 0		;
	brcs	exit_write_fast		; exit loop if counter reached zero
	rjmp	write_fast


write_fast_with_wait:
; loop with 16 bit counter and its output
	out 	PORTE, buffer1		; first byte of counter to USB-data-port
wait_fast_1:
	sbic	PINB, 7
	rjmp	wait_fast_1 		; wait for TXF cleared
	
	cbi	PORTB, 5		; write to USB
	sbi	PORTB, 5

	out 	PORTE, buffer2		; second byte of counter to USB-data-port
wait_fast_2:
	sbic	PINB, 7
	rjmp	wait_fast_2 		; wait for TXF cleared
	
	cbi	PORTB, 5		; write to USB
	sbi	PORTB, 5

	subi 	buffer1, 1		; decrement 16bit loop counter
	sbci 	buffer2, 0		;

	brcs	exit_write_fast		; exit loop if counter reached zero

	rjmp 	write_fast_with_wait

exit_write_fast:
	ret
;===================;

;=============================;
; Increment the reset counter ;
;-----------------------------;
incr_reset_counter:
	ldi     ZL,low(reset_counter)  	; Set pointer to eeprom address
    	ldi     ZH,high(reset_counter) 	; 
	rcall	read_eeprom		; Read from eeprom to buffer
	inc	temp			; Increment counter in buffer
	rcall	write_eeprom		; Write buffer to eeprom

	ret
;=============================;

;==========================;
; Switch on Watchdog timer ;
;--------------------------;
wdt_on:
	; reset WDT
	wdr
	in r16, WDTCR
	ldi r16, (1<<WDCE)|(1<<WDE)
	; Write logical one to WDCE and WDE
	ori r16, (1<<WDCE)|(1<<WDE)
	out WDTCR, r16
	; Turn on WDT
	ldi r16, (1<<WDE)
	out WDTCR, r16
	ret

;===========================;
; Switch off Watchdog timer ;
;---------------------------;
wdt_off:
	; reset WDT
	wdr
	in r16, WDTCR
	ldi r16, (1<<WDCE)|(1<<WDE)
	; Write logical one to WDCE and WDE
	ori r16, (1<<WDCE)|(1<<WDE)
	out WDTCR, r16
	; Turn off WDT
	ldi r16, (0<<WDE)
	out WDTCR, r16
	ret

;============================================;
; Initialize the DS1621 with standard values ;
;--------------------------------------------;
init_thermostat:
	rcall	twi_init		; Init TWI


	rcall	twi_start		; --- SET CONFIG ----

	ldi	data, THERMO_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_CFG		; Send config command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	ldi	data, 0x00		; Set config active low and continous
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	rcall	twi_start		; --- START THERMOSTAT ----

	ldi	data, THERMO_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, START_CONV	; Send start command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	ret

;=========================================================;
; Initialize the DS1621 on the board with standard values ;
;---------------------------------------------------------;
init_board_thermostat:
	rcall	twi_init		; Init TWI


	rcall	twi_start		; --- SET CONFIG ----

	ldi	data, THERMO_ADDRS_BRD_W; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, SET_CFG		; Send config command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	ldi	data, 0x00		; Set config active low and continous
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	rcall	twi_start		; --- START THERMOSTAT ----

	ldi	data, THERMO_ADDRS_BRD_W; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
	rcall	twi_check		
	
	ldi	data, START_CONV	; Send start command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

	rcall	twi_stop

	ret


;#############################################################################
;	        	      E E P R O M   D A T A
;#############################################################################

.ESEG

; The 3 bytes of n_samples, default = 100
n1:
	.db	100
n2:
	.db	0
n3:
	.db	0

reset_counter:
	.db	-1
