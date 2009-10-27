;====================================================================;
;--------------------------------------------------------------------;
;
; -A-T-T-R-R-A-C---c-o-m-_-u-C-  
;
; Master control and USB communication programm for the pulse 
; generation PCB board. The main loop waits for commands from a PC
; via the FT245R USB iC and controlls the pulse generation uC and
; a DS1621 thermostat
; 
; -TWI BUS-
; The pulse generation uC and a DS1621 are controlled via
; hardware TWI
;			PINC0 = SCL
;			PINC1 = SDA
;
; -USB COMMUNICATION-
; Waits for commands, one byte long, from PC via FT245R USB chip and
; evaluate them.
;
; PORTC = control USB	PINC4 = RXF flag INPUT
;			PINC5 = RD flag OUTPUT
;			PINC6 = TXF flag INPUT
;			PINC7 = WR flag OUTPUT
; PORTD = I/O via USB
;
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
.def data  	   = r17 	; holds the byte sent via USB or TWI
.def buffer1       = r18
.def buffer2       = r19
.def n_samples3    = r20
.def eeprom_buffer = r21
.def t_msb 	   = r22	; Case temperature MSB
.def t_lsb	   = r23	; Case temperature LSB
.def twi_stat	   = r24

;=======================================;
; TWI Bitrate = 100 kHz for 8 MHz clock ;
;=======================================;
.equ TWI_BIT_RATE	= 32
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


;====================;
; DS1621 TWI ALIASES ;
;====================;
.equ THERMO_ADDRS_W	= 0b10010000	; Address for writting
.equ THERMO_ADDRS_R	= 0b10010001	; Address for reading

.equ READ_TEMP		= 0xAA		; Read temperature
.equ SET_TH		= 0xA1		; Set high temp limit
.equ SET_TL		= 0xA2		; Set low temp limit
.equ SET_CFG		= 0xAC		; Set config register
.equ START_CONV		= 0xEE		; Start conversion
.equ STOP_CONV		= 0x22		; Stop conversion

;==================;
; CPLD TWI ALIASES ;
;==================;

.equ CPLD_ADDRS_W	= 0b00001010	; Addres for writing
.equ CPLD_ADDRS_R	= 0b00001011	; Addres for reading

;========================================;
; Communication commands for CPLD and PC ;
;========================================;
.equ ERROR 		= 0x00

.equ SET_NUM_SAMPLES 	= 0b00000110
.equ SET_MODE		= 0b00001000
.equ SET_PW		= 0b00000010
.equ SET_DELAY		= 0b00000100
.equ START_MSRMNT 	= 0b00011110

.equ SET_CASE_TEMP	= 0x07
.equ GET_CASE_TEMP	= 0x17



reset:
  	ldi 	temp, LOW(RAMEND)	; LOW-Byte of upper RAM-Adress
        out 	SPL, temp
        ldi 	temp, HIGH(RAMEND)	; HIGH-Byte of upper RAM-Adress
        out 	SPH, temp 

	rcall	init_thermostat		; Initialize the DS1621 over TWI

main:
	; TEST
;	rcall com_get_case_temp
;	ldi data, 0xff
;	rcall write_byte_usb
;	rjmp main

	; Main usually starts here
	rcall	read_byte_usb	
	rcall	check_usb_bits
	rjmp	main



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

	cpi	data, SET_PW
	breq	com_set_pw

	cpi	data, SET_DELAY
	breq	com_set_delay

	cpi	data, SET_MODE
	breq	jmp_set_mode

	cpi	data, START_MSRMNT
	breq	jmp_start_msrmnt

	cpi	data, SET_CASE_TEMP
	breq	jmp_set_case_temp

	cpi	data, GET_CASE_TEMP
	breq	jmp_get_case_temp
		
	ldi	data, ERROR		; If no match was found, send error
	rcall	write_byte_usb
	ret				; Return to main

; helpers if branch out of reach
jmp_set_mode:
	rjmp	com_set_mode

jmp_start_msrmnt:
	rjmp	com_start_msrmnt

jmp_set_case_temp:
	rjmp	com_set_case_temp

jmp_get_case_temp:
	rjmp 	com_get_case_temp

;-------------------------------------;
; Sets the 2 byte value for n_samples ;
;-------------------------------------;
com_set_num_samples:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; get 1st byte from usb
	mov	buffer1, data; 
	rcall	read_byte_usb		; get 2nd byte from usb
	mov	buffer2, data; 

; Move the eeprom saving routine to a spererate command, 
; because saving it every time would damage the eeprom after
; 100.000 write cycles
;	rcall 	n_samples_to_eeprom	; Save value in EEPROM
;	rcall	n_samples_from_eeprom	; Get value from EEPROM

	mov	data, buffer1 
	rcall 	write_byte_usb		; write back 1st byte
	mov	data, buffer2 
	rcall 	write_byte_usb		; write back 2st byte

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_NUM_SAMPLES	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer1	; Write 1st byte to CPLD
	rcall	twi_write		; 
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer2	; Write 2nd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check	

	rcall	twi_stop

	ret


;-------------------------;
; Set pulse width in CPLD ;
;-------------------------;
com_set_pw:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read PW byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; PW on stack	

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

	ret

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

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for thermostat + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_DELAY		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer1	; Write 1st byte to CPLD
	rcall	twi_write		; 
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	mov	data, buffer2	; Write 2nd byte to CPLD
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ret

;------------------;
; Set mode in CPLD ;
;------------------;
com_set_mode:
	rcall	write_byte_usb		; write back received command byte to PC
	rcall	read_byte_usb		; Read PW byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; PW on stack	

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, SET_MODE		; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	pop	data			; Pop PW from stack
	rcall	twi_write		; Write it to CPLD
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ret

;-------------------;
; Start measurement ;
;-------------------;
com_start_msrmnt:
	rcall	write_byte_usb		; write back received command byte to PC

; not used anymore, but maybe implement some eeprom things
;	rcall 	n_samples_from_eeprom	; get n_samples(1,2,3) from eeprom
;	rcall	write_fast_init		; write data n_sample-times

	rcall	twi_init		; Init TWI

	rcall	twi_start		; --- START TWI ---

	ldi	data, CPLD_ADDRS_W	; Send address for CPLD + write
	rcall	twi_write
	ldi	twi_stat, SLA_W_ACK	; Check status (write ACK)
;	rcall	twi_check		
	
	ldi	data, START_MSRMNT	; Send command
	rcall	twi_write
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
;	rcall	twi_check

	rcall	twi_stop

	ret

;----------------------------------;
; Set the desired case temperature ;
;----------------------------------;
com_set_case_temp:
	rcall	write_byte_usb		; write back received command byte to PC
	
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

	rcall	read_byte_usb		; Read T_HIGH byte from USB
	rcall	write_byte_usb		; Write it back for error checking
	push 	data			; T_HIGH on stack
	rcall	twi_write		; Write it to thermostat
	ldi	twi_stat, DAT_W_ACK	; Check status (data transmitted)
	rcall	twi_check

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

	ret

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
; Write buffer1 2 3 to EEPROM ;
;--------------------------------;
n_samples_to_eeprom:
	ldi     ZL,low(n1)       	; Set pointer to eeprom address
    	ldi     ZH,high(n1)     	; 
	mov	eeprom_buffer,buffer1; Copy data to eeprom buffer
	rcall	write_eeprom		; Write buffer to eeprom

	ldi     ZL,low(n2)       	; Same for n2
    	ldi     ZH,high(n2)     	; 
	mov	eeprom_buffer,buffer2; 
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
; Read buffer1 2 3 from EEPROM ;
;---------------------------------;
n_samples_from_eeprom:
	ldi     ZL,low(n1)       	; Set pointer to eeprom address
    	ldi     ZH,high(n1)     	; 
	rcall	read_eeprom		; Write buffer to eeprom
	mov	buffer1,eeprom_buffer; Copy data from eeprom buffer

	ldi     ZL,low(n2)       	; Same for n2
    	ldi     ZH,high(n2)     	; 
	rcall	read_eeprom		; 
	mov	buffer2,eeprom_buffer;

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


;===================================;
; Read single byte command from USB ;
;-----------------------------------;
read_byte_usb:
	ldi 	temp, 0x00	
	out 	DDRD, temp		; PORTD is input
	ldi	temp, 0xFF
	out	PORTD, temp		; pull-ups active

	cbi 	DDRC, 4			; PORTC 4 input
	sbi	PORTC, 4		; pull-up active
	cbi	DDRC, 6			; PORTC 6 input
	sbi	PORTC, 6		; pull-up active

	sbi 	DDRC, 5			; PORTC 5 output
	sbi	PORTC, 5		; set it high
	sbi 	DDRC, 7			; PORTC 7 output
	sbi	PORTC, 7		; set it high

wait_for_RXF:
	sbis	PINC, 4 		; exit loop if RXF (read ready) is low
	rjmp 	read_byte
	rjmp 	wait_for_RXF	

read_byte:
	cbi 	PORTC, 5 		; pull RD low to read from usb chip
	nop				; !!! KEEP THIS FOR TIMING !!!
	in 	data, PIND		; read
	sbi 	PORTC, 5		; set RD to high again
	ret
;===================================;


;============================;
; Write a single byte to USB ;
;----------------------------;
write_byte_usb:
	ldi 	temp, 0xFF	
	out 	DDRD, temp		; PORTD is output

	cbi 	DDRC, 4			; PORTC 4 input
	sbi	PORTC, 4		; pull-up active
	cbi	DDRC, 6			; PORTC 6 input
	sbi	PORTC, 6		; pull-up active

	sbi 	DDRC, 5			; PORTC 5 output
	sbi	PORTC, 5		; set it high
	sbi 	DDRC, 7			; PORTC 7 output
	sbi	PORTC, 7		; set it high
	
wait_for_TXF:
	sbis	PINC, 6
	rjmp	write_byte_usb_bits
	rjmp	wait_for_TXF

write_byte_usb_bits:
	out	PORTD, data
	cbi	PORTC, 7
	sbi	PORTC, 7

	ret
;=============================;


;===================;
; Fast write output ;
;-------------------;
write_fast_init:
	ldi 	temp, 0xFF	
	out 	DDRD, temp		; PORTD is output

	cbi 	DDRC, 4			; PORTC 4 input
	sbi	PORTC, 4		; pull-up active
	cbi	DDRC, 6			; PORTC 6 input
	sbi	PORTC, 6		; pull-up active

	sbi 	DDRC, 5			; PORTC 5 output
	sbi	PORTC, 5		; set it high
	sbi 	DDRC, 7			; PORTC 7 output
	sbi	PORTC, 7		; set it high

	ldi	temp, 0

write_fast:
	out 	PORTD, temp
	cbi	PORTC, 7
	sbi	PORTC, 7
	inc	temp
	out 	PORTD, temp
	cbi	PORTC, 7
	sbi	PORTC, 7
	inc	temp
	nop
	nop

	subi 	buffer1, 1		; decrement 24bit loop counter 'n_samples'
	sbci 	buffer2, 0		;
	sbci 	n_samples3, 0		;

	breq	exit_write_fast		; exit loop if counter reached zero

	rjmp 	write_fast

exit_write_fast:
	ret
;===================;

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
