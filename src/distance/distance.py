import time

import gpiozero

# https://gpiozero.readthedocs.io/en/stable/api_input.html#distancesensor-hc-sr04
# https://projects.raspberrypi.org/en/projects/physical-computing/12
ultrasonic = gpiozero.DistanceSensor(echo=17, trigger=4)
while True:
    print(ultrasonic.distance)
    time.sleep(1)
