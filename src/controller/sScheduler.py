#! /usr/bin/env python

'''

    Smart Sprinkler controller(Controller):

    This program will require a data file with the following:
        Evotranspiration rates
        Irrigation duration(max value), days to irrigate, ip/local address of the node(s)

    The scheduling will normalize the distribution of evapotranspiration rates to scale the
    duration of the irrigation cycle(s). Furthermore, the system will not run if there's
    50% probability of precipitation(pop) or if the rain sensor is detecting water.

    Note: Some sensors have a small reservoir and will detect water hours after the rain
    has stopped, which is ideal.


    Author: Roberto Pasilas (svtanthony@gmail.com)
'''

import sys
import time
import datetime
import urllib2
import copy
import re
import json
import calendar


# Define globals

#5 min * 60sec/min = 300 sec
maxDur = 300

#20 min * 60 sec/min = 1200 sec
delayTime = 1200

sensorURL = 'http://10.0.0.6/'

key =  'WUNDERGROUND_KEY'
state = 'STATE_ABBR'
zip = 'ZIP_CODE'

def inputData():
    # Read file
    schedule = []
    f = open(sys.argv[1], 'r')
    file = f.readlines()
    f.close()

    # Get schedule and evt rates
    for line in file:
        if not line.startswith("#"):
            if line.startswith("{"):
                EVT = json.loads(line)
            else:
                schedule.append(line.split(';'))

    # Process schedule
    for rIndex, row in enumerate(schedule):
        for col, value in enumerate(row):
            # Get valve reference
            if col == 0:
                schedule[rIndex][col] = int(value.strip().split(' ')[1])
            # Get duration
            elif col == 1:
                schedule[rIndex][col] = int(value.strip().split(' ')[0]) if value.strip().split(' ')[1] == "sec" else  int(value.strip().split(' ')[0])*60
            # Get watering days
            elif col == 2:
                days = [False]*7
                for l in value.strip().split(','):
                    days[time.strptime(l, "%a").tm_wday] = True
                    schedule[rIndex][col] = days
            #Get ip/link for valve controller
            elif col == 3:
                schedule[rIndex][col] = value.strip()

        row.append(time.time())

    return [EVT, schedule]

def run(evt,schedule):
    valve = 0
    duration = 1
    days = 2
    url = 3
    start = 4

    # Set current month and month with highest EVT rate
    maxMonth, today = max(evt.iteritems(), key=lambda x:x[1])
    today =  calendar.month_abbr[datetime.datetime.today().month]

    # Adjust watering schedule with evt(evotranspiration) data
    for data in schedule:
       data[duration] = int((float(data[duration]) * float(float(evt[today])/float(evt[maxMonth]))))

    # Run schedule and limit to max duration per session
    for data in schedule:
        # if today is a watering day, activate valve  Mon = 0 ,..., Sun = 6
        if data[days][datetime.datetime.today().weekday()]:
            # if duration exceeds our max, split the scheduled run time.
            if data[duration] > maxDur:
                tempSchedule = copy.deepcopy(data)
                data[duration] = maxDur
                tempSchedule[duration] = tempSchedule[duration] - maxDur
                tempSchedule[start] = time.time() + maxDur + delayTime
                schedule.append(tempSchedule)
            # Delay if needed
            if data[start] > time.time():
                print "we will delay for %d seconds, starting @ %s" % (data[start] - time.time(), time.asctime())
                time.sleep(data[start] - time.time())
                print time.asctime()
            try:
                # URL to trigger valves
                var = urllib2.urlopen(data[url] + "?valve=%d&dur=%d" % (data[valve], data[duration]))
                var.close()
                print data[url] + "?valve=%d&dur=%d" % (data[valve], data[duration])
                time.sleep(data[duration])
                print ''
            except:
                sys.stderr.write('URL NOT VALID OR 404\n')
                print (data[url] + "?valve=%d&dur=%d" % (data[valve], data[duration]))
        else:
            print "No watering today for valve %d" % data[valve]

def getRainData():

    sensor = False
    forecast = False

    try:
        sensorRead = urllib2.urlopen(sensorURL)
        sensorData = sensorRead.read()
        sensorRead.close()
        pattern = 'Rain: (.*)<'
        result = re.search(pattern,sensorData).group(1)
        if result == 'Detected':
            sensor = True
    except:
        print "Could not access sensor!"

    try:
        f = urllib2.urlopen('http://api.wunderground.com/api/%s/forecast/q/%s/%s.json' % (key,state,zip))
        jsonString = json.loads(f.read())
        f.close()
        if 49 < max(float(jsonString['forecast']['txt_forecast']['forecastday'][1]['pop']),
                       float(jsonString['forecast']['txt_forecast']['forecastday'][1]['pop'])):
            forecast = True
    except:
        print "Could not get weather forecast!"

    return True if (sensor == True or forecast == True) else False

def main():
    #'''
    if not getRainData():
        list = inputData()
        run(list[0],list[1])
    else:
        print "It is or will be raining, system will not water today!"

main()
