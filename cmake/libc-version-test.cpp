#if __has_include(<component-version.h>)
#include <component-version.h>
#if ((COMPONENT_VERSION_MAJOR == 1) && (COMPONENT_VERSION_MINOR == 2) &&       \
     (BUILD_NUMBER < 118))
#error                                                                         \
    "Microchip pack component version greater than 1.2.118 required to support ATtiny3216"
#endif
#else
#include <avr/version.h>
#if !(__AVR_LIBC_VERSION__ > 20100UL)
#error "AVR LibC version greater than 2.1 does support ATtiny3216"
#endif
#endif

#include <avr/io.h>

int main() { return 0; }