/*
 * Copyright (c) 2022, 2024 Tracey Emery <tracey@openbsd.org>
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

#include "hardware/pwm.h"

#include "libmorse.h"

#define DIT		 1.0
#define DAH		 3.0
#define IC_SP		 1.0
#define C_SP		 3.0
#define W_SP		 7.0

#define D_WPM		 10
#define ST		 600

#define UNIT_T(x) (60.0 / (50.0 * (float)x)) * 1000.0
#define bit_read(value, bit) (((value) >> (bit)) & 0x01)

struct morse_cb_args {
	uint8_t c;
	char *morse;
	uint16_t len;
};

static volatile uint8_t	 gpio_wpm, gpio_tx_pin, gpio_stop_now;
static volatile uint64_t gpio_pause;

static volatile uint8_t	 dac_wpm, dac_tx_pin, dac_stop_now;
static volatile uint64_t dac_pause;

static volatile float	 gpio_unit_t;
static volatile float	 dac_unit_t;

static volatile uint8_t	 gpio_tx_sending = 0, gpio_tx_set = 0;
static volatile	uint8_t	 gpio_unit_handled;

static volatile uint8_t	 dac_tx_sending = 0, dac_tx_set = 0;
static volatile	uint8_t	 dac_unit_handled;

static uint8_t		 ctob(uint8_t);
static uint8_t		 gpio_this_index = 0, gpio_next_index = 0;
static uint8_t		 gpio_handle_unit, gpio_bit;
static uint8_t		 gpio_digraph = 0, gpio_inited = 0;

static unsigned long	 gpio_handle_unit_millis;
static unsigned long	 dac_handle_unit_millis;

static uint8_t		 dac_this_index = 0, dac_next_index = 0;
static uint8_t		 dac_handle_unit, dac_bit;
static uint8_t		 dac_digraph = 0, dac_inited = 0;

static void		 gpio_stop(struct morse_cb_args *);
static void		 gpio_handle_chars(struct morse_cb_args *);
static void		 gpio_handle_units(struct morse_cb_args *);

static void		 dac_stop(struct morse_cb_args *);
static void		 dac_handle_chars(struct morse_cb_args *);
static void		 dac_handle_units(struct morse_cb_args *);

static void		 pwm_set_freq_duty(uint8_t, uint8_t, uint16_t, uint8_t);

static int64_t		 gpio_unit_handled_cb(alarm_id_t, void *);
static int64_t		 gpio_pause_handled_cb(alarm_id_t, void *);
static int64_t		 gpio_tx_handled_cb(alarm_id_t, void *);

static int64_t		 dac_unit_handled_cb(alarm_id_t, void *);
static int64_t		 dac_pause_handled_cb(alarm_id_t, void *);
static int64_t		 dac_tx_handled_cb(alarm_id_t, void *);

uint8_t slice, channel;

Morse::Morse(uint8_t type, uint8_t the_pin, unsigned long the_pause)
{
	Morse(type, the_pin, the_pause, D_WPM);
}

Morse::Morse(uint8_t type, uint8_t the_pin, unsigned long the_pause,
    uint8_t the_wpm)
{
	Morse(type, the_pin, the_pause, the_wpm, ST);
}

Morse::Morse(uint8_t type, uint8_t the_pin, unsigned long the_pause,
    uint8_t the_wpm, uint16_t st_freq)
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
		if (!dac_inited) {
			dac_wpm = the_wpm;
			dac_pause = the_pause * 1000;
			dac_tx_pin = the_pin;
			dac_unit_t = UNIT_T(dac_wpm);
			dac_inited = 1;

			gpio_set_function(dac_tx_pin, GPIO_FUNC_PWM);
			slice = pwm_gpio_to_slice_num(dac_tx_pin);
			channel = pwm_gpio_to_channel(dac_tx_pin);
			pwm_set_freq_duty(slice, channel, st_freq, 50);
			pwm_set_enabled(slice, false);
		}
		break;
	default:
		exit(1);
		break;
	}
}

/* gpio */

void
Morse::gpio_set_transmitting(void)
{
	gpio_tx_sending = 1;
}

uint8_t
Morse::gpio_get_transmit_set(void)
{
	return gpio_tx_set;
}

uint8_t
Morse::gpio_get_transmitting(void)
{
	return gpio_tx_sending;
}

void
Morse::gpio_tx_stop(void)
{
	gpio_stop_now = 1;
}

void
Morse::gpio_tx(const char *morse)
{
	struct morse_cb_args *mcb = NULL;
	int i;

	if (gpio_tx_sending)
		return;
	else {
		mcb = (struct morse_cb_args *)
		    malloc(sizeof(struct morse_cb_args *));

		gpio_put(gpio_tx_pin, 0);
		gpio_tx_set = 1;
		gpio_stop_now = 0;

		mcb->len = strlen(morse);
		mcb->morse = strndup(morse, mcb->len);

		if (gpio_stop_now)
			gpio_stop(mcb);
		else
			add_alarm_in_ms(gpio_pause, gpio_tx_handled_cb, mcb,
			    false);
	}
}

void
Morse::gpio_set_wpm(uint8_t the_wpm)
{
	gpio_wpm = the_wpm;
	gpio_unit_t = UNIT_T(gpio_wpm);
	gpio_inited = 1;
}

static void
gpio_stop(struct morse_cb_args *mcb)
{
	gpio_digraph = 0;
	gpio_tx_sending = 0;
	gpio_tx_set = 0;
	gpio_this_index = 0;
	gpio_next_index = 0;
	free(mcb);
}

static int64_t
gpio_tx_handled_cb(alarm_id_t id, void *data)
{
	struct morse_cb_args *mcb = (struct morse_cb_args *) data;

	gpio_this_index = 0;
	gpio_next_index = 1;

	if (gpio_stop_now)
		gpio_stop(mcb);
	else
		gpio_tx_sending = 1;
		gpio_handle_chars(mcb);

	return 0;
}

static int64_t
gpio_unit_handled_cb(alarm_id_t id, void *data)
{
	struct morse_cb_args *mcb = (struct morse_cb_args *) data;

	gpio_put(gpio_tx_pin, 0);
	gpio_unit_handled = 1;
	gpio_bit++;

	// handle IC_SP, or handle C_SP
	if (mcb->c >> (gpio_bit + 1) || gpio_digraph)
		gpio_handle_unit_millis = IC_SP * gpio_unit_t;
	else
		gpio_handle_unit_millis = C_SP * gpio_unit_t;

	if (gpio_stop_now)
		gpio_stop(mcb);
	else
		add_alarm_in_ms(gpio_handle_unit_millis, gpio_pause_handled_cb,
		    mcb,false);

	return 0;
}

static int64_t
gpio_pause_handled_cb(alarm_id_t id, void *data)
{
	struct morse_cb_args *mcb = (struct morse_cb_args *) data;

	gpio_unit_handled = 0;
	gpio_handle_unit = 0;

	// hit the end of that byte
	if (!(mcb->c >> (gpio_bit + 1))) {
		gpio_bit = 0;
		gpio_next_index = 1;
		mcb->morse++;
		gpio_this_index++;
		if (gpio_stop_now)
			gpio_stop(mcb);
		else
			gpio_handle_chars(mcb);
	} else {
		if (gpio_stop_now)
			gpio_stop(mcb);
		else
			gpio_handle_chars(mcb);
	}

	return 0;
}

static void
gpio_handle_chars(struct morse_cb_args *mcb)
{
	if (gpio_this_index == mcb->len)
		gpio_stop(mcb);
	if (gpio_next_index) {
		gpio_next_index = 0;
		gpio_handle_unit = 0;
		gpio_unit_handled = 0;
		if (*mcb->morse == 126) {
			mcb->morse++;
			gpio_this_index++;
			gpio_next_index = 1;
		} else if (*mcb->morse == 96) {
			gpio_digraph = !gpio_digraph;
			mcb->morse++;
			gpio_this_index++;
			gpio_next_index = 1;
			if (gpio_stop_now)
				gpio_stop(mcb);
			else
				gpio_handle_chars(mcb);
		} else {
			mcb->c = ctob(*mcb->morse);
			if (gpio_stop_now)
				gpio_stop(mcb);
			else
				gpio_handle_units(mcb);
			gpio_bit = 0;
		}
	} else if (gpio_tx_sending) {
		if (gpio_stop_now)
			gpio_stop(mcb);
		else
			gpio_handle_units(mcb);
	}

}

static void
gpio_handle_units(struct morse_cb_args *mcb)
{
	// set led on and space off
	if (!gpio_next_index && !gpio_handle_unit && !gpio_unit_handled &&
	    gpio_tx_sending) {
		if (mcb->c == 1)
			gpio_handle_unit_millis = W_SP * gpio_unit_t;
		else {
			gpio_put(gpio_tx_pin, 1);
			if (bit_read(mcb->c, gpio_bit))
				gpio_handle_unit_millis = DAH * gpio_unit_t;
			else
				gpio_handle_unit_millis = DIT * gpio_unit_t;
		}
		gpio_handle_unit = 1;

		if (gpio_stop_now)
			gpio_stop(mcb);
		else
			add_alarm_in_ms(gpio_handle_unit_millis,
			    gpio_unit_handled_cb, mcb, false);
	}
}

/* dac */

void
Morse::dac_set_transmitting(void)
{
	dac_tx_sending = 1;
}

uint8_t
Morse::dac_get_transmit_set(void)
{
	return dac_tx_set;
}

uint8_t
Morse::dac_get_transmitting(void)
{
	return dac_tx_sending;
}

void
Morse::dac_tx_stop(void)
{
	dac_stop_now = 1;
}

void
Morse::dac_tx(const char *morse)
{
	struct morse_cb_args *mcb = NULL;
	int i;

	if (dac_tx_sending)
		return;
	else {
		mcb = (struct morse_cb_args *)
		    malloc(sizeof(struct morse_cb_args *));

		pwm_set_enabled(slice, false);
		dac_tx_set = 1;
		dac_stop_now = 0;

		mcb->len = strlen(morse);
		mcb->morse = strndup(morse, mcb->len);

		if (dac_stop_now)
			dac_stop(mcb);
		else
			add_alarm_in_ms(dac_pause, dac_tx_handled_cb, mcb,
			    false);
	}
}

void
Morse::dac_set_wpm(uint8_t the_wpm)
{
	dac_wpm = the_wpm;
	dac_unit_t = UNIT_T(gpio_wpm);
	dac_inited = 1;
}

static void
dac_stop(struct morse_cb_args *mcb)
{
	dac_digraph = 0;
	dac_tx_sending = 0;
	dac_tx_set = 0;
	dac_this_index = 0;
	dac_next_index = 0;
	free(mcb);
}

static int64_t
dac_tx_handled_cb(alarm_id_t id, void *data)
{
	struct morse_cb_args *mcb = (struct morse_cb_args *) data;

	dac_this_index = 0;
	dac_next_index = 1;

	if (dac_stop_now)
		dac_stop(mcb);
	else
		dac_tx_sending = 1;
		dac_handle_chars(mcb);

	return 0;
}

static int64_t
dac_unit_handled_cb(alarm_id_t id, void *data)
{
	struct morse_cb_args *mcb = (struct morse_cb_args *) data;

	pwm_set_enabled(slice, false);
	dac_unit_handled = 1;
	dac_bit++;

	// handle IC_SP, or handle C_SP
	if (mcb->c >> (dac_bit + 1) || dac_digraph)
		dac_handle_unit_millis = IC_SP * dac_unit_t;
	else
		dac_handle_unit_millis = C_SP * dac_unit_t;

	if (dac_stop_now)
		dac_stop(mcb);
	else
		add_alarm_in_ms(dac_handle_unit_millis, dac_pause_handled_cb,
		    mcb,false);

	return 0;
}

static int64_t
dac_pause_handled_cb(alarm_id_t id, void *data)
{
	struct morse_cb_args *mcb = (struct morse_cb_args *) data;

	dac_unit_handled = 0;
	dac_handle_unit = 0;

	// hit the end of that byte
	if (!(mcb->c >> (dac_bit + 1))) {
		dac_bit = 0;
		dac_next_index = 1;
		mcb->morse++;
		dac_this_index++;
		if (dac_stop_now)
			dac_stop(mcb);
		else
			dac_handle_chars(mcb);
	} else {
		if (dac_stop_now)
			dac_stop(mcb);
		else
			dac_handle_chars(mcb);
	}

	return 0;
}

static void
dac_handle_chars(struct morse_cb_args *mcb)
{
	if (dac_this_index == mcb->len)
		dac_stop(mcb);
	if (dac_next_index) {
		dac_next_index = 0;
		dac_handle_unit = 0;
		dac_unit_handled = 0;
		if (*mcb->morse == 126) {
			mcb->morse++;
			dac_this_index++;
			dac_next_index = 1;
		} else if (*mcb->morse == 96) {
			dac_digraph = !dac_digraph;
			mcb->morse++;
			dac_this_index++;
			dac_next_index = 1;
			if (dac_stop_now)
				dac_stop(mcb);
			else
				dac_handle_chars(mcb);
		} else {
			mcb->c = ctob(*mcb->morse);
			if (dac_stop_now)
				dac_stop(mcb);
			else
				dac_handle_units(mcb);
			dac_bit = 0;
		}
	} else if (dac_tx_sending) {
		if (dac_stop_now)
			dac_stop(mcb);
		else
			dac_handle_units(mcb);
	}

}

static void
dac_handle_units(struct morse_cb_args *mcb)
{
	// set led on and space off
	if (!dac_next_index && !dac_handle_unit && !dac_unit_handled &&
	    dac_tx_sending) {
		if (mcb->c == 1)
			dac_handle_unit_millis = W_SP * dac_unit_t;
		else {
			pwm_set_enabled(slice, true);
			if (bit_read(mcb->c, dac_bit))
				dac_handle_unit_millis = DAH * dac_unit_t;
			else
				dac_handle_unit_millis = DIT * dac_unit_t;
		}
		dac_handle_unit = 1;

		if (dac_stop_now)
			dac_stop(mcb);
		else
			add_alarm_in_ms(dac_handle_unit_millis,
			    dac_unit_handled_cb, mcb, false);
	}
}

/* other */

/* edited from
 * https://www.i-programmer.info/programming/hardware/14849-the-pico-in-c-basic-pwm.html?start=2
 */

static
void pwm_set_freq_duty(uint8_t slice, uint8_t channel, uint16_t freq,
    uint8_t duty)
{
	uint32_t clock, divider16, wrap;

	clock = 125000000;
	divider16 = clock / freq / 4096 + (clock % (freq * 4096) != 0);

	if (divider16 / 16 == 0)
		divider16 = 16;

	wrap = clock * 16 / divider16 / freq - 1;
	pwm_set_clkdiv_int_frac(slice, divider16/16, divider16 & 0xF);
	pwm_set_wrap(slice, wrap);
	pwm_set_chan_level(slice, channel, wrap * duty / 100);
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
