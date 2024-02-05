cd ~
[ ! -d tools ] && mkdir tools
cd tools
# https://abyz.me.uk/lg/download.html
[ ! -d lg ] && wget http://abyz.me.uk/lg/lg.zip && unzip lg.zip && cd lg && make && sudo make install && cd ..

cd ~/rpi

cd src/blinky
[ ! -d venv ] && python3 -m venv venv
source venv/bin/activate
python3 -m pip install -r requirements.txt
deactivate
cd ~/rpi

# next one here. just copy-paste. it's fine.
