#define TARGET_IS_BLIZZARD_RB1	//Rom.h definition
//#define	PREF(x)	x	//Use for debugging purposes to trace problems in driver.lib
#define	PREF(x)	MAP_ ## x	//Use to reduce code size

#include <stdint.h>
#include <stdbool.h>

#include "inc/hw_gpio.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"

#include "driverlib/fpu.h"
#include "driverlib/eeprom.h"
#include "driverlib/sysctl.h"
#include "driverlib/pin_map.h"
#include "driverlib/gpio.h"
#include "driverlib/i2c.h"
#include "driverlib/timer.h"
#include "driverlib/pwm.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"

//#include "driverlib/rom.h"
//#include "driverlib/rom_map.h"

//#define LAUNCHPADTEST
//#define CARDTEST
