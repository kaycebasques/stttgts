import subprocess
import threading
import time

import gpiozero

# https://projects.raspberrypi.org/en/projects/physical-computing/5
button = gpiozero.Button(2)
audio_file = 'user.flac'

def record():
    # start the audio recording
    process = subprocess.Popen(['rec', audio_file, 'rate', '32k'])
    # https://unix.stackexchange.com/a/57593/79351
    while not stop.is_set():
        # wait for it to end...
        time.sleep(1)
    process.terminate()

while True:
    button.wait_for_press()
    stop = threading.Event()
    thread = threading.Thread(target=record)
    thread.start()
    button.wait_for_release()
    stop.set()
    thread.join()
