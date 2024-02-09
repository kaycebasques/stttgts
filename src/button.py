import os
import random
import subprocess
import threading
import time

import dotenv
import gpiozero
from google.cloud import speech
import google.generativeai as gemini

os.environ['GOOGLE_APPLICATION_CREDENTIALS'] = '/home/bookworm/service-account.json'
env = dotenv.dotenv_values('.env')
gemini.configure(api_key=env['GEMINI_API_KEY'])
model = gemini.GenerativeModel('gemini-pro')

# https://projects.raspberrypi.org/en/projects/physical-computing/5
button = gpiozero.Button(2)
audio_file = 'user.flac'
led = gpiozero.RGBLED(16, 20, 21)

# https://codelabs.developers.google.com/codelabs/cloud-speech-text-python3#3
def speech_to_text():
    client = speech.SpeechClient()
    # https://cloud.google.com/speech-to-text/docs/sync-recognize
    with open(audio_file, 'rb') as f:
        content = f.read()
    config = speech.RecognitionConfig(
        language_code='en',
        encoding=speech.RecognitionConfig.AudioEncoding.FLAC,
        sample_rate_hertz=48000,
        audio_channel_count=2
    )
    audio = speech.RecognitionAudio(content=content)
    response = client.recognize(config=config, audio=audio)
    transcript = ''
    for result in response.results:
        transcript += result.alternatives[0].transcript
    return transcript
    # print(response)
    # best_response = response.alternatives[0]
    # print(best_response)

def record():
    led.on()
    led.color = (1, 0, 0)
    # start the audio recording
    process = subprocess.Popen(['rec', audio_file, 'rate', '48k'])
    # https://unix.stackexchange.com/a/57593/79351
    while not stop_recording.is_set():
        # wait for it to end...
        time.sleep(1)
    process.terminate()
    led.off()

def thinky_blinky():
    """blink like an octopus dreaming... https://youtu.be/0vKCLJZbytU"""
    colors = [
        (0, 0, 0),
        (1, 0, 0),
        (1, 1, 0),
        (1, 0, 1),
        (0, 1, 0),
        (0, 1, 1),
        (1, 1, 1)
    ]
    led.on()
    while not stop_blinking.is_set():
        led.color = random.choice(colors)
        time.sleep(0.1)
    led.off()

while True:
    button.wait_for_press()
    stop_recording = threading.Event()
    thread = threading.Thread(target=record)
    thread.start()
    button.wait_for_release()
    stop_recording.set()
    thread.join()
    stop_blinking = threading.Event()
    thread2 = threading.Thread(target=thinky_blinky)
    thread2.start()
    # time.sleep(1)
    # subprocess.run(['play', '-v', '3.0', audio_file])
    text = speech_to_text()
    print(text)
    response = model.generate_content(text)
    print(response.text)
    stop_blinking.set()
    thread2.join()
    led.on()
    led.color = (0, 0.2, 1)
    p = subprocess.Popen(['spd-say', '--wait', '--volume', '+100', f'"{response.text}"'])
    while p.poll() is None:
        time.sleep(1)
    led.off()
