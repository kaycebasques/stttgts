sudo apt install -y sox  # audio recording command (`rec`)
sudo apt install -y speech-dispatcher  # text-to-speech command (`spd-say`)

cd tools/lg
make
sudo make install
cd ../..

cd src
[ ! -d venv ] && python3 -m venv venv
source venv/bin/activate
python3 -m pip install -r requirements.txt
deactivate
cd ..
