# https://projects.raspberrypi.org/en/projects/physical-computing/5

import threading
import time

import gpiozero

button = gpiozero.Button(2)

def record():
    while not stop.is_set():
        print('waiting...')
    print('done')

while True:
    button.wait_for_press()
    stop = threading.Event()
    thread = threading.Thread(target=record)
    thread.start()
    button.wait_for_release()
    stop.set()
    thread.join()
