# https://projects.raspberrypi.org/en/projects/physical-computing/5
import gpiozero

button = gpiozero.Button(2)
button.wait_for_press()
print('the button is being pressed')
button.wait_for_release()
print('the button was released')
