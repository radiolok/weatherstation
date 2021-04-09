# -*- coding: utf-8 -*-

from PIL import Image, ImageDraw, ImageFont, ImageOps
import os
import pickle
import json
from time import sleep, time
import serial
from yaweather import Russia, YaWeather
import argparse
import requests
import smbus2
import bme280
from datetime import datetime

port = 1
address = 0x76
bus = smbus2.SMBus(port)

calibration_params = bme280.load_calibration_params(bus, address)

SCRIPT_FILE_PATH = os.path.abspath(os.path.dirname(__file__))

IMAGE_WIDTH=112
IMAGE_HEIGHT=16


def ConvertImage(imageName,  Pw, Ph, inversed = False):
    imageIn = Image.open(imageName)
    imageIn = imageIn.resize((Pw, Ph))
    imageGray = imageIn.convert('L')
    imageBw = imageGray.point(lambda x: 0 if x<128 else 1, '1')
    r_data = imageBw.getdata()
    l_data = list(r_data)
    hex_arr = []
    for i in range(0,len(l_data),8):
        hex_num = 0
        for k in range(0,8):
            hex_num = hex_num + l_data[i+k]*(2**(7-k))
        hex_arr.append(hex_num)
    return hex_arr

def ConvertMobitecImage(imageName,  inversed = False):
    imageIn = Image.open(imageName)
    Pw, Ph = imageIn.size
    imageGray = imageIn.convert('L')
    imageBw = imageGray.point(lambda x: 0 if x<128 else 1, '1')
    r_data = imageBw.getdata()
    l_data = list(r_data)
    hex_arr = []
    row_max = int(Ph/4) + int (Ph% 4)
    for row in range (0, row_max):
        for column in range (0, Pw):
            hex_num = 0
            for line in range(0,4):
                pixel = column + (Pw*line) + (Pw*4*row)
                if pixel < len(l_data):
                    hex_num = hex_num + l_data[pixel]*2**(line)
            if inversed:
                hex_num = (0x0f - hex_num) & 0x0f
            hex_arr.append(hex_num)
    return hex_arr, Pw, Ph

def GetWeather(data, pic_list):
    pic_file = None
    temp = None
    wind_speed = None
    humidity = None
    if data != None:
        temp = data["fact"]["temp"]
        condition = data["fact"]["condition"]
        wind_speed = data["fact"]["wind_speed"]
        humidity = data["fact"]["humidity"]
        if pic_list != None:
            if condition in pic_list["icons"]:
                pic_file = pic_list["icons"][condition]
                if type(pic_file) != str:
                    #we are in dict now:
                    daytime = data["fact"]["daytime"]
                    if daytime in pic_file:
                        pic_file = pic_file[daytime]
                    else:
                        pic_file = None
    if pic_file != None:
        pic_file = SCRIPT_FILE_PATH + '/' + pic_file
    return temp, wind_speed, humidity, pic_file

def GetWeatherForecast(data , pic_list, forecast):
    pic_file = None
    temp = None
    name = None
    if data != None:
        forecasts = data["forecasts"][0]["parts"]
        if len(forecasts) > forecast:
            keys = list(forecasts)
            keys = [ x for x in keys if "_short" not in x ]
            forecast_data = forecasts[keys[forecast]]
            name = keys[forecast] 
            temp = forecast_data["temp_avg"]
            condition = forecast_data["condition"]
        if pic_list != None:
            if condition in pic_list["icons"]:
                pic_file = pic_list["icons"][condition]
                if type(pic_file) != str:
                    #we are in dict now:
                    daytime = forecast_data["daytime"]
                    if daytime in pic_file:
                        pic_file = pic_file[daytime]
                    else:
                        pic_file = None
    if pic_file != None:
        pic_file = SCRIPT_FILE_PATH + '/' + pic_file
    return temp, name, pic_file

def AddChecksum(data):
    csum = 0
    for i in range(1, len(data)):
        csum += data[i]

    crc = csum & 0xff
    data.append(crc)
    if crc == 0xfe:
        data.append(0x00)
    elif crc == 0xff:
        data[-1] = 0xfe
        data.append(0x01)
    data.append(0xff)
    return data

def WriteTextBa(text, font = None, pos = None):
    if pos != None:
        msg.append(0xd2)
        msg.append(pos)
    if font != None:
        msg.append(0xd4)
        msg.append(font)
    msg = msg + text
    return msg


def AddHeader(text):
    return text + bytearray(b'\xff\x06\xa2')

def WriteText(msg, text,font = None, posx = None, posy = None):
    if posx != None:
        msg.append(0xd2)
        msg.append(posx)
    if posy != None:
        msg.append(0xd3)
        msg.append(posy)
    if font != None:
        msg.append(0xd4)
        msg.append(font)
    msg = msg + bytearray(text.encode('utf-8'))
    return msg
    
def SendMobitecImage(msg, hex_array, Pw, Ph, pos):
    row_max = int(Ph/4) + int (Ph% 4)
    for row in range (0, row_max):
        msg.append(0xd2)
        #x pos
        msg.append(pos)
        msg.append(0xd3)
        #y pos
        msg.append(row*4 + 4)
        msg.append(0xd4)
        msg.append(0x77)
        for i in range(0, Pw):
            msg.append(hex_array[Pw*row + i]+0x20)
    return msg

#used for forecasts
def TempPicText(tty, temp, pic, text):
    msg = bytearray()
    msg = AddHeader(msg)
    font = 0x62
    if len(text) > 5:
        font = 0x66
    msg = WriteText(msg, text, font=font, posy=14)
    #weather_icon
    image,Pw,Ph = ConvertMobitecImage(pic, True)
    msg = SendMobitecImage(msg, image, Pw, Ph, 50)
    msg = AddTemperature(msg, temp, 80)
    msg = AddChecksum(msg)
    tty.write(msg)

def WriteTime(tty, seconds):
    now = datetime.now()
    current = now.strftime("%a %-d %b %H:%M")
    msg = bytearray()
    msg = AddHeader(msg)
    shift = 30 
    msg = WriteText(msg, current, font=0x71)
    msg = AddChecksum(msg)
    tty.write(msg)

def TempPicWind(tty, temp, pic, wind_speed):
    msg = bytearray()
    msg = AddHeader(msg)
    string_left = "%dM/C" % (int(wind_speed))
    msg = WriteText(msg, string_left, font=0x62, posy = 14)
    #weather_icon
    image,Pw,Ph = ConvertMobitecImage(pic, True)
    msg = SendMobitecImage(msg, image, Pw, Ph, 50)
    msg = AddTemperature(msg, temp, 80)
    msg = AddChecksum(msg)
    tty.write(msg)

def HumidityPressure(tty, humidity, pressure):
    mmHg = pressure * 100 * 0.00750063755419211
    msg = bytearray()
    msg = AddHeader(msg)
    msg = AddHumidity(msg, humidity, 0)
    string_right = "%dmm" % (int(mmHg)) 
    msg = WriteText(msg, string_right, font = 0x68, posx = 60)
    msg = AddChecksum(msg)
    tty.write(msg)



def AddTemperature(msg, temp, pos):
    start_pos = pos 
    symbol = ' '
    if temp < 0:
        symbol = '-'
    #plus
    if temp > 0:
        image,Pw,Ph = ConvertMobitecImage(SCRIPT_FILE_PATH+"/plus.png", True)
        msg = SendMobitecImage(msg, image, Pw, Ph, start_pos - 1)
    #text:
    string_right = "%c%d" % (symbol, int(abs(temp))) 
    if temp > -10 and temp < 10:
        string_right += " C"
    msg = WriteText(msg, string_right, font = 0x68, posx = start_pos)
    #celsius
    shift = 6
    for symbol in str(int(abs(temp))):
        shift += 6 if symbol == '1' else 9
    image,Pw,Ph = ConvertMobitecImage(SCRIPT_FILE_PATH+"/degree.png", True)
    msg = SendMobitecImage(msg, image, Pw, Ph, start_pos + shift)
    return msg

def AddHumidity(msg, humidity, pos):
    #hymidity_icon
    image,Pw,Ph = ConvertMobitecImage(SCRIPT_FILE_PATH+"/drop.png", True)
    msg = SendMobitecImage(msg, image, Pw, Ph, pos)
    string_right = "%d" % (int(humidity)) 
    msg = WriteText(msg, string_right, font = 0x68, posx = pos + 10)
    shift = pos + 10
    for symbol in str(int(humidity)):
        shift += 6 if symbol == '1' else 9
    image,Pw,Ph = ConvertMobitecImage(SCRIPT_FILE_PATH+"/percent.png", True)
    msg = SendMobitecImage(msg, image, Pw, Ph, shift)
    return msg


def InnerTempHumidity(tty, temp, humidity):
    msg = bytearray()
    msg = AddHeader(msg)
    msg = AddHumidity(msg, humidity, 0)
    msg = AddTemperature(msg, temp, 80)
    msg = AddChecksum(msg)
    tty.write(msg)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-u', '--userport', required=True)
    parser.add_argument('-i', '--input')
    args = parser.parse_args()

    RinTTY = None

    term_parity = serial.PARITY_ODD
    userport = args.userport

    tty = serial.Serial(userport, 
                            4800, 
                            bytesize=8, 
                            stopbits = 1)

    with open(SCRIPT_FILE_PATH+"/yandex-weather.json", "r") as fp:
        data = json.load(fp)
    with open(SCRIPT_FILE_PATH+"/w_pic.json", "r") as pics:
        pic_list = json.load(pics)

    temp, wind_speed, humidity, pic_file = GetWeather(data, pic_list)
    temp_fc0, name_fc0, pic_file_fc0 = GetWeatherForecast(data, pic_list, 0)
    temp_fc1, name_fc1, pic_file_fc1 = GetWeatherForecast(data, pic_list, 1)
    
    TempPicWind(tty, temp, pic_file, wind_speed)
    sleep(9)
    TempPicText(tty, temp_fc0, pic_file_fc0, name_fc0)
    sleep(9)
    TempPicText(tty, temp_fc1, pic_file_fc1, name_fc1)
    sleep(9)
    data = bme280.sample(bus, address, calibration_params)
    HumidityPressure(tty, humidity, data.pressure)
    sleep(9)
    InnerTempHumidity(tty, data.temperature, data.humidity)
    sleep(9)
    WriteTime(tty, 10)
