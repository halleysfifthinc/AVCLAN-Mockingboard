if [[ -c /dev/ttyUSB0 ]]; then
    sudo chown root:$(id -gn $(whoami)) /dev/ttyUSB0
fi
if [[ -c /dev/ttyUSB1 ]]; then
    sudo chown root:$(id -gn $(whoami)) /dev/ttyUSB1
fi
