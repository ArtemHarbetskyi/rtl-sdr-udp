# RTL-SDR flow and control using UDP

rtl_udp -f 105500000 -s 2048000 -g 20 -p 1240 -u 192.168.2.10:9999


Set gain (for example, 20.0 dB, which equals 200 in tenths)

printf "\x03\x00\x00\x00\xC8" | nc -u 127.0.0.1 1235