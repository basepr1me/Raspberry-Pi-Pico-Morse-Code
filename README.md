# Raspberry Pi Pico Morse Code

Raspberry Pi Pico Morse Code Library

This Morse Code library allows the programmer to generate Morse Code via a GPIO
pin.

Usage
-----

Clone the library to your project:

		git clone https://github.com/basepr1me/Raspberry-Pi-Morse-Code.git

Three class instantiation methods are in place:

		Morse(TYPE, PIN, TX_PAUSE);
		Morse(TYPE, PIN, TX_PAUSE, WPM);
		Morse(TYPE, PIN, TX_PAUSE, WPM, FREQ);

Types available are:
		
		M_GPIO	// GPIO Pin Type
		M_DAC	// PWM Output

Pro-sign Morse Code can be generated using backticks:

		cont char *led_morse = "Hello giant world `ar`";

To set the WPM, two functions are available:

		gpio_set_wpm(WPM);
		dac_set_wpm(WPM);

Example
-------

See the [Morse Example](morse.cpp) file for more information.

Notes
-----

The make.sh file is designed for OpenBSD ksh.

Author
------

[Tracey Emery](https://github.com/basepr1me/)

If you like this software, consider [donating](https://k7tle.com/?donate=1).

See the [License](LICENSE.md) file for more information.
