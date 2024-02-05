cd src/$1
source venv/bin/activate
python3 $1.py && deactivate
cd ../..
