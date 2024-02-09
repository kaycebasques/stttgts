import subprocess
import threading
import time

import gpiozero
from google.cloud import speech

# https://projects.raspberrypi.org/en/projects/physical-computing/5
button = gpiozero.Button(2)
audio_file = 'user.flac'

# https://codelabs.developers.google.com/codelabs/cloud-speech-text-python3#3
def speech_to_text():
    client = speech.SpeechClient()
    # https://cloud.google.com/speech-to-text/docs/sync-recognize
    with open(audio_file, 'rb') as f:
        content = f.read()
    config = speech.RecognitionConfig(
        language_code='en',
        encoding=speech.RecognitionConfig.AudioEncoding.LINEAR,
        sample_rate_hertz=32000
    )
    audio = speech.RecognitionAudio(content=content)
    response = client.recognize(config=config, audio=audio)
    best_response = response.alternatives[0]
    print(best_response)

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
    time.sleep(1)
    subprocess.run(['play', '-v', '3.0', audio_file])
    speech_to_text()
