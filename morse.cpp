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

#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "libmorse.h"

#define CALLS 3

#define WPM 15
#define TX_PAUSE 3 // pause before transmitting and between TXs

const uint pin = PICO_DEFAULT_LED_PIN;

Morse morse_led(M_GPIO, pin, TX_PAUSE, WPM);

int main(void);

int
main()
{
	uint8_t n = 0;

	const char *led_morse[][CALLS] = {
	    {
		    "de k7tle `ar`",
		    "Bk",
		    "Hello",
	    }
	};

	stdio_init_all();

	while (1) {
		if (!morse_led.gpio_get_transmitting()) {
			n = 0 + (rand() % CALLS);
			printf("Sending transmission %d: %s\n", n,
			    led_morse[0][n]);
			morse_led.gpio_tx(led_morse[0][n]);
		}
	}

	return 0;
}
