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

#ifndef _LIBMORSE_H
#define _LIBMORSE_H

class
Morse
{
	public:
		Morse(uint8_t type, uint8_t the_pin,
		    unsigned long the_pause);
		Morse(uint8_t type, uint8_t the_pin,
		    unsigned long the_pause, uint8_t the_wpm);

		uint8_t gpio_get_transmitting(void);
		void gpio_set_transmitting(void);

		void gpio_tx(const char *);
		void gpio_tx_stop(void);
		void gpio_watchdog(void);
};

enum
{
	M_GPIO,
	M_DAC,
	M_ADC // in the event we ever want to translate incoming morse
};

#endif // _LIBMORSE_H
