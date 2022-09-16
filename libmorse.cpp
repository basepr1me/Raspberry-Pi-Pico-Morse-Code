/*
 * Copyright (c) 2022 Tracey Emery <tracey@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/malloc.h"

#include "libmorse.h"

#define DIT		 1.0
#define DAH		 3.0
#define IC_SP		 1.0
#define C_SP		 3.0
#define W_SP		 7.0

#define D_WPM		 10

#define UNIT_T(x) (60.0 / (50.0 * (float)x)) * 1000.0
#define bit_read(value, bit) (((value) >> (bit)) & 0x01)

static volatile uint8_t	 gpio_wpm;
static volatile uint8_t	 gpio_tx_pin;
static volatile uint64_t gpio_pause;

static volatile float	 gpio_unit_t;

static volatile uint8_t	 gpio_tx_sending = 0;
static volatile	uint8_t	 gpio_unit_handled;

static uint8_t		 gpio_tx_len, gpio_tx_enc;
static uint8_t		 ctob(uint8_t);
static uint8_t		 gpio_this_index = 0, gpio_next_index = 0;
static uint8_t		 gpio_handle_unit, gpio_bit;
static uint8_t		 gpio_digraph = 0, gpio_inited = 0;

static unsigned long	 gpio_handle_unit_millis;

static char		*gpio_tx_str = NULL;

void			 gpio_stop(void);
void			 gpio_handle_chars(void);
void			 gpio_handle_units(uint8_t c);

int64_t gpio_unit_handled_cb(alarm_id_t, void *);
int64_t gpio_pause_handled_cb(alarm_id_t, void *);
int64_t gpio_tx_handled_cb(alarm_id_t, void *);

Morse::Morse(uint8_t type, uint8_t the_pin, unsigned long the_pause)
{
	Morse(type, the_pin, the_pause, D_WPM);
}

Morse::Morse(uint8_t type, uint8_t the_pin, unsigned long the_pause,
    uint8_t the_wpm)
{
	switch(type) {
	case M_GPIO:
		if (!gpio_inited) {
			gpio_wpm = the_wpm;
			gpio_pause = the_pause * 1000;
			gpio_tx_pin = the_pin;
			gpio_init(gpio_tx_pin);
			gpio_set_dir(gpio_tx_pin, GPIO_OUT);
			gpio_unit_t = UNIT_T(gpio_wpm);
			gpio_inited = 1;
		}
		break;
	case M_DAC:
		// we don't have a dac, but we will setup pwm
		break;
	default:
		exit(1);
		break;
	}
}

void
Morse::gpio_set_transmitting(void)
{
	gpio_tx_sending = 1;
}

uint8_t
Morse::gpio_get_transmitting(void)
{
	return gpio_tx_sending;
}

void
Morse::gpio_tx_stop(void)
{
	gpio_stop();
}

void
Morse::gpio_tx(const char *morse)
{
	int i;

	if (gpio_tx_sending)
		return;
	else {
		gpio_put(gpio_tx_pin, 0);
		gpio_tx_sending = 1;

		gpio_tx_len = strlen(morse);
		gpio_tx_str = strndup(morse, gpio_tx_len);

		add_alarm_in_ms(gpio_pause, gpio_tx_handled_cb, NULL, false);
	}
}

void
gpio_stop(void)
{
	gpio_digraph = 0;
	gpio_tx_sending = 0;
	gpio_this_index = 0;
	gpio_next_index = 0;

}

int64_t
gpio_tx_handled_cb(alarm_id_t id, void *data)
{
	gpio_this_index = 0;
	gpio_next_index = 1;

	gpio_handle_chars();

	return 0;
}

struct pause_cb {
	uint8_t c;
};
struct pause_cb pcb;

int64_t
gpio_unit_handled_cb(alarm_id_t id, void *data)
{
	struct pause_cb *pcb = (struct pause_cb *) data;

	gpio_put(gpio_tx_pin, 0);
	gpio_unit_handled = 1;
	gpio_bit++;

	// handle IC_SP, or handle C_SP
	if (pcb->c >> (gpio_bit + 1) || gpio_digraph)
		gpio_handle_unit_millis = IC_SP * gpio_unit_t;
	else
		gpio_handle_unit_millis = C_SP * gpio_unit_t;

	add_alarm_in_ms(gpio_handle_unit_millis, gpio_pause_handled_cb, pcb,
	    false);

	return 0;
}

int64_t
gpio_pause_handled_cb(alarm_id_t id, void *data)
{
	struct pause_cb *pcb = (struct pause_cb *) data;

	gpio_unit_handled = 0;
	gpio_handle_unit = 0;

	// hit the end of that byte
	if (!(pcb->c >> (gpio_bit + 1))) {
		gpio_bit = 0;
		gpio_next_index = 1;
		gpio_tx_str++;
		gpio_this_index++;
		gpio_handle_chars();
	} else
		gpio_handle_units(pcb->c);

	if (gpio_this_index == gpio_tx_len)
		gpio_stop();

	return 0;
}

void
gpio_handle_chars(void)
{
	if (gpio_this_index == gpio_tx_len)
		gpio_stop();
	if (gpio_next_index) {
		gpio_next_index = 0;
		gpio_handle_unit = 0;
		gpio_unit_handled = 0;
		if (*gpio_tx_str == 126) {
			gpio_tx_str++;
			gpio_this_index++;
			gpio_next_index = 1;
		} else if (*gpio_tx_str == 96) {
			gpio_digraph = !gpio_digraph;
			gpio_tx_str++;
			gpio_this_index++;
			gpio_next_index = 1;
			gpio_handle_chars();
		} else {
			gpio_tx_enc = ctob(*gpio_tx_str);
			gpio_handle_units(gpio_tx_enc);
			gpio_bit = 0;
		}
	} else if (gpio_tx_sending)
		gpio_handle_units(gpio_tx_enc);

}

void
gpio_handle_units(uint8_t c)
{
	memset(&pcb, 0, sizeof(pcb));
	pcb.c = c;

	// set led on and space off
	if (!gpio_next_index && !gpio_handle_unit && !gpio_unit_handled &&
	    gpio_tx_sending) {
		if (c == 1)
			gpio_handle_unit_millis = W_SP * gpio_unit_t;
		else {
			gpio_put(gpio_tx_pin, 1);
			if (bit_read(c, gpio_bit))
				gpio_handle_unit_millis = DAH * gpio_unit_t;
			else
				gpio_handle_unit_millis = DIT * gpio_unit_t;
		}
		gpio_handle_unit = 1;

		add_alarm_in_ms(gpio_handle_unit_millis, gpio_unit_handled_cb,
		    &pcb, false);
	}

}

static uint8_t
ctob(uint8_t c)
{
	uint8_t uc = toupper(c);

	switch(uc) {
	case 32:	return 0b1;			// ' '
	case 33:	return 0b1110101;		// '!'
	case 34:	return 0b1010010;		// '"'
	case 36:	return 0b11001000;		// '$'
	case 38:	return 0b100010;		// '&'
	case 39:	return 0b1011110;		// '\''
	case 40:	return 0b101101;		// '('
	case 41:	return 0b1101101;		// ')'
	case 43:	return 0b101010;		// '+' 'AR'
	case 44:	return 0b1110011;		// ','
	case 45:	return 0b1100001;		// '-'
	case 46:	return 0b1101010;		// '.'
	case 47:	return 0b101001;		// '/'

	case 48:	return 0b111111;		// '0'
	case 49:	return 0b111110;		// '1'
	case 50:	return 0b111100;		// '2'
	case 51:	return 0b111000;		// '3'
	case 52:	return 0b110000;		// '4'
	case 53:	return 0b100000;		// '5'
	case 54:	return 0b100001;		// '6'
	case 55:	return 0b100011;		// '7'
	case 56:	return 0b100111;		// '8'
	case 57:	return 0b101111;		// '9'

	case 58:	return 0b1000111;		// ':'
	case 59:	return 0b1010101;		// ';'
	case 61:	return 0b110001;		// '=' 'BT'
	case 63:	return 0b1001100;		// '?'
	case 64:	return 0b1010110;		// '@'

	case 65:	return 0b110;			// 'A'
	case 66:	return 0b10001;			// 'B'
	case 67:	return 0b10101;			// 'C'
	case 68:	return 0b1001;			// 'D'
	case 69:	return 0b10;			// 'E'
	case 70:	return 0b10100;			// 'F'
	case 71:	return 0b1011;			// 'G'
	case 72:	return 0b10000;			// 'H'
	case 73:	return 0b100;			// 'I'
	case 74:	return 0b11110;			// 'J'
	case 75:	return 0b1101;			// 'K'
	case 76:	return 0b10010;			// 'L'
	case 77:	return 0b111;			// 'M'
	case 78:	return 0b101;			// 'N'
	case 79:	return 0b1111;			// 'O'
	case 80:	return 0b10110;			// 'P'
	case 81:	return 0b11011;			// 'Q'
	case 82:	return 0b1010;			// 'R'
	case 83:	return 0b1000;			// 'S'
	case 84:	return 0b11;			// 'T'
	case 85:	return 0b1100;			// 'U'
	case 86:	return 0b11000;			// 'V'
	case 87:	return 0b1110;			// 'W'
	case 88:	return 0b11001;			// 'X'
	case 89:	return 0b11101;			// 'Y'
	case 90:	return 0b10011;			// 'Z'

	case 95:	return 0b1101100;		// '_'

	default:	return 0b11000000;		// non (126)
	}
};
