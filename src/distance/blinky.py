import time

import gpiozero

ultrasonic = gpiozero.DistanceSensor(echo=17, trigger=4)
while True:
    print(ultrasonic.distance)
    time.sleep(3)
