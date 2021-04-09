from yaweather import Russia, YaWeather
import os
import sys
import json

SCRIPT_FILE_PATH = os.path.abspath(os.path.dirname(__file__))

API_KEY_FILE = SCRIPT_FILE_PATH + '/yandex-secret.key'
GPS_FILE = SCRIPT_FILE_PATH + '/gps.key'
JSON_FILE_PATH = SCRIPT_FILE_PATH + '/yandex-weather.json'
print(API_KEY_FILE)

def read_api_key(api_file):
    #read key API:
    API_KEY = None
    line = 0
    try:
        with open(api_file, "r") as f:
            line = line + 1
            if line == 1:
                API_KEY = f.readline().rstrip()
    except OSError as e:
        pass
    return API_KEY

def read_coordinates(gps_file):
    #read key API:
    GPS_KEY = None
    line = 0
    try:
        with open(gps_file, "r") as f:
            line = line + 1
            if line == 1:
                GPS_KEY = eval(f.readline().rstrip())
    except OSError as e:
        pass
    return GPS_KEY

def main():

    GPS_KEY = read_coordinates(GPS_FILE)
    if GPS_KEY == None:
        print("ERROR: no gps.key file!", file=sys.stderr)
        exit(-1)
    print(GPS_KEY)

    API_KEY = read_api_key(API_KEY_FILE)
    if API_KEY == None:
        print("ERROR: No Yandex API_KEY file!", file=sys.stderr)
        exit(-1)

    y = YaWeather(api_key=API_KEY)

    result = y.forecast_raw(GPS_KEY)
    print(result)
    with open(JSON_FILE_PATH, "w") as outfile:
        json.dump(result, outfile)

if __name__ == '__main__':
    main()
