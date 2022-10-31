# weatherstation

# prereq

```
pip3 install yaweather pillow RPi.bme280 smbus2
```

in file ./.local/lib/python3.10/site-packages/yaweather/api.py replace 'forecast' to 'informers' as yandex get this instructions


That's my crontab.

```bash
0  *    * * *   ubuntu    python3 /home/ubuntu/weatherstation/yandex_w.py > /dev/null
30 *    * * *   ubuntu    python3 /home/ubuntu/weatherstation/yandex_w.py > /dev/null
*  *    * * *   root    /home/ubuntu/weatherstation/update.sh 

```

