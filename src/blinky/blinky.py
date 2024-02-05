import threading
import time

import gpiozero

def blink():
    # The number maps to "GPIO 17" in the URL below, not the number in the circle.
    # https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
    led = gpiozero.LED(17)
    while not stop.is_set():
        led.on()
        time.sleep(1)
        led.off()
        time.sleep(1)
    led.off()

stop = threading.Event()
thread = threading.Thread(target=blink)
thread.start()
input('Press Enter to stop...')
stop.set()
thread.join()
