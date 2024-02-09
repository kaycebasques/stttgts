import os
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
    # start the audio recording
    process = subprocess.Popen(['rec', audio_file, 'rate', '48k'])
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
    text = speech_to_text()
    response = model.generate_content(text)
    print(response.text)
