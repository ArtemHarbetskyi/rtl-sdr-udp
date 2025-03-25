# RTL-SDR flow and control using UDP
### This is a fork of the project from https://github.com/sysrun/rtl-sdr, the approach for udp is completely rewritten.


```cd rtl-sdr-udp/```

```mkdir build```

```cd build```

```cmake ../```

```make```

```sudo make install```

```sudo ldconfig```

```rtl_udp -f 105500000 -s 2048000 -g 20 -p 1234 -u 0.0.0.0:9999``` 





Set gain (for example, 20.0 dB, which equals 200 in tenths)

```printf "\x03\x00\x00\x00\xC8" | nc -u 0.0.0.0 1234```

*Functionality checked in SDR++*
