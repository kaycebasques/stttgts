from gpiozero import LED
from time import sleep

# The number maps to "GPIO 17" in the URL below, not the number in the circle.
# https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
led = LED(17)

while True:
    led.on()
    sleep(1)
    led.off()
    sleep(1)
