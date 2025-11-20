#!/usr/bin/env python3

import sys
import os
import argparse

# Add the parent directory to the path so we can find the module to test
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from config import Config
import common
from dbmodels import DBModels
from threading import Event, Thread
from multiprocessing import Pool, TimeoutError, current_process
import subprocess
from ipcserver import IPCServer, IPCRequestBaseHandler, IPCRequestResult
from filelock import FileLock
import signal
import requests
import time
import json
import struct
from datetime import datetime, timezone
from pprint import pprint

#constants

#sensor sampling rate in Hz
SENSOR_SAMPLING_RATE_HZ = 26667
SENSOR_FIXED_POINT_G_FACTOR = 32767.0  # 2^15 - 1

def pr(msg):
    print(msg)
    sys.stdout.flush()

def verbose_pr(msg):
    global args
    if args.verbose:
        now_ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3] + " UTC"
        pr(f"[{now_ts}] (verbose) {msg}")

def sigHandler(signum, frame):
    global ipcServer, mainWait, measurementsProcessingWait, running

    if current_process().name != "MainProcess":
        return

    pr('Signal handler called with signal [%s]' % signum)
    if signum == signal.SIGTERM:
        #jobsManager.stop()
        ipcServer.stop()
        running = False
    elif signum == signal.SIGINT:
        if sys.platform == 'win32' or True:
            #jobsManager.stop()
            ipcServer.stop()
            running = False
        else:
            pass
            #jobsManager.wakeUp()

    measurementsProcessingWait.set()
    mainWait.set()

#
#
#
def appendToHeartbeatLog(sensor):
    """
     a. append a line to the sensor's heartbeat log file like:
        2025-01-31T12:34:56.789Z,323137
        YYYY-MM-DDTHH:MM:SS.sssZ,sensorSN

     b. limit the size of the heartbeat log file to last N lines per sensor (N=100)
    """
    global sensor_status_folder

    #append a line to the sensor's heartbeat log file
    heartbeat_csv_file = os.path.join(sensor_status_folder, "heartbeat_api.csv")
    verbose_pr(f"   - appending heartbeat to log file: {heartbeat_csv_file} [@{sensor.sensorSN}]")

    #if the file does not exist, create an empty file
    if not os.path.isfile(heartbeat_csv_file):
        with open(heartbeat_csv_file, 'w') as f:
            verbose_pr(f"   - creating heartbeat log file: {heartbeat_csv_file} [@{sensor.sensorSN}]")
    
    #append a line like:
    #2025-01-31T12:34:56.789Z,323137
    #YYYY-MM-DDTHH:MM:SS.sssZ,sensorSN
    with open(heartbeat_csv_file, 'a') as f:
        now_utc = datetime.now(timezone.utc)
        now_utc_str = now_utc.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        line = f"{now_utc_str},{sensor.sensorSN}\n"
        f.write(line)

def appendToSensorStatusLog(sensor, status_dict, suppress_output=False):
    """
    a. create a new sensor response file like: 2025-01-28-000000-000_323137_SRSP.csv
       in the sensor_responses folder
       with contents like:
       320470,100,6.12.38,104,Agen_AP,C6:C9:E3:2A:A5:9E
       <sensorSN>,<batteryLevel>,<firmwareVersion>,<wifi_signal>,<connected_ssid>,<connected_mac>
    
    b. do some housekeeping: keep only the last N files per sensor (N=20)
    
    """
    global sensor_responses_folder

    now_utc = datetime.now(timezone.utc)
    now_utc_str = now_utc.strftime("%Y-%m-%d-%H%M%S-%f")[:-3]
    response_filename = f"{now_utc_str}_{sensor.sensorSN}_SRSP.csv"
    if not suppress_output:
        verbose_pr(f"   - writing sensor status file: {response_filename} [@{sensor.sensorSN}]")
    response_filepath = os.path.join(sensor_responses_folder, response_filename)

    battery_voltage = status_dict.get('BATTERY_VOLTAGE', 0)
    #convert to float
    try:
        battery_voltage = float(battery_voltage)
    except ValueError:
        battery_voltage = 0.0

    """
        The table below is temperature/load-adjusted for ≈40 °C and ≈60 mA CCV.
        It maps the HAT Sensor 3.00 V minimum workable voltage to 0 %
        and captures the plateau-then-knee shape typical of Li-SOCl₂ batteries.
        https://www.batterystation.co.uk/content/datasheets_MSDS/Saft/Saft%20LS14500%20Product%20Data%20Sheet.pdf
        3.00, 0%
        3.26, 50%
        3.30, 100%
    """
    battery_level_mapping = [
        (3.00, 0),
        (3.26, 50),
        (3.30, 100),
    ]

    # Linear interpolation to find the battery level, clamp between 0 and 100
    if battery_voltage <= battery_level_mapping[0][0]:
        batteryLevel = battery_level_mapping[0][1]
    elif battery_voltage >= battery_level_mapping[-1][0]:
        batteryLevel = battery_level_mapping[-1][1]
    else:
        for i in range(len(battery_level_mapping) - 1):
            v1, l1 = battery_level_mapping[i]
            v2, l2 = battery_level_mapping[i + 1]
            if v1 <= battery_voltage <= v2:
                # linear interpolation
                batteryLevel = l1 + (battery_voltage - v1) * (l2 - l1) / (v2 - v1)
                break
    
    if not suppress_output:
        verbose_pr(f"   - battery voltage: {battery_voltage} V, battery level: {batteryLevel} % [@{sensor.sensorSN}]")
    batteryLevel = max(0, min(100, round(batteryLevel)))

    status_line = "{sensorSN},{batteryLevel},{firmwareVersion},{wifi_signal},{connected_ssid},{connected_mac}".format(
        sensorSN = sensor.sensorSN,
        batteryLevel = batteryLevel,
        firmwareVersion = status_dict.get('FIRMWARE_VERSION', 'N/A'),
        wifi_signal = status_dict.get('WIFI_SIGNAL', '00'),
        connected_ssid = status_dict.get('SSID', 'N/A'),
        connected_mac = status_dict.get('MAC_ADDRESS', 'N/A'),
    )
    if not suppress_output:
        verbose_pr(f"   - sensor status line: {status_line} [@{sensor.sensorSN}]")

    with open(response_filepath, 'w') as f:
        f.write(status_line + "\n")
    
    #b. housekeeping: keep only the last N files per sensor
    N = 20
    #list all files for this sensor but sort by name (which starts with timestamp, so it is chronological)
    sensor_files = [f for f in os.listdir(sensor_responses_folder) 
                    if os.path.isfile(os.path.join(sensor_responses_folder, f)) 
                        and f.endswith('_SRSP.csv') 
                        and f.split('_')[1] == sensor.sensorSN]
    sensor_files.sort()  # sort files by name (chronological order)

    # keep only the last N files
    for f in sensor_files[:-N]:
        if not suppress_output:
            verbose_pr(f"   - deleting old sensor status file: {f} [@{sensor.sensorSN}]")
        os.remove(os.path.join(sensor_responses_folder, f))


def sendSensorHttpRequest(url, sensor, timeout=5, expect_json=False, check_status_code=False):
    """
    Helper function to send HTTP GET requests to sensors with consistent error handling.
    
    Args:
        url: The full URL to request
        sensor: The sensor object (used for logging context)
        timeout: Request timeout in seconds (default: 5)
        expect_json: If True, parse response as JSON and validate structure (default: False)
        check_status_code: If True, raise exception on non-200 status (default: False)
    
    Returns:
        tuple: (success: bool, response_body: str, response_json: dict or None, error_msg: str or None)
    """
    response_body = None
    response_json = None
    error_msg = None
    
    try:
        req_headers = {
            'Connection': 'close'
        }
        response = requests.get(url, timeout=timeout, headers=req_headers)
        if check_status_code and response.status_code != 200:
            raise Exception("Received non-200 response code: {0}".format(response.status_code))
        response_body = response.text
    except requests.Timeout:
        error_msg = "request timed out"
        return False, None, None, error_msg
    except requests.RequestException as e:
        error_msg = str(e)
        return False, None, None, error_msg
    except Exception as e:
        error_msg = str(e)
        return False, None, None, error_msg
    
    # If JSON parsing is expected
    if expect_json:
        try:
            response_json = json.loads(response_body)
        except json.JSONDecodeError as e:
            error_msg = "error decoding JSON response: {0}".format(str(e))
            return False, response_body, None, error_msg
        
        # Validate JSON structure
        if not isinstance(response_json, dict):
            error_msg = "Invalid JSON response (not a dict): {0}".format(response_body)
            return False, response_body, None, error_msg
        
        if 'res' not in response_json or 'data' not in response_json:
            error_msg = "Invalid JSON response (missing res or data): {0}".format(response_body)
            return False, response_body, None, error_msg
        
        if response_json['res'].upper() != 'OK':
            error_msg = "Command failed with response: {0}".format(response_json['res'])
            return False, response_body, response_json, error_msg
    
    return True, response_body, response_json, None

def convertRssiToSignalLevel(rssi):
    """
    Convert RSSI value (in dBm) to a signal level percentage (0-100%).
    Typical RSSI values range from -100 dBm (weak) to -50 dBm (strong).
    """
    RSSI_MIN = -100
    RSSI_MAX = -50
    if rssi <= RSSI_MIN:
        return 0
    elif rssi >= RSSI_MAX:
        return 100
    else:
        # Linear mapping from RSSI range to 0-100%
        signal_level = int((rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN))
        return signal_level

def submitCommandStatus(sensor, wait_before_sending=True, suppress_output=False):
    global config, dbm
    
    if not suppress_output:
        verbose_pr(" - CommandStatus start [@{0}]".format(sensor.sensorSN))

    if wait_before_sending:
        #wait a few seconds to avoid overwhelming the sensors with commands
        sleep_seconds = config.General.get('sensorsdaemon_wait_for_commands_seconds', 2)
        if not suppress_output:
            verbose_pr(f"   - waiting {sleep_seconds} seconds [@{sensor.sensorSN}]")
        time.sleep(sleep_seconds)

    if not suppress_output:
        verbose_pr(f"   - CommandStatus submitting [@{sensor.sensorSN}]")
    #pprint(sensor.__dict__)

    # we need to construct a URL like:
    # http://192.168.0.235/api?command=STATUS&datetime=`date '+%s'`
    epoch_ms = common.now_timestamp() * 1000
    statuscmd_url = f"http://{sensor.stat_ip_address}/api?command=STATUS&datetime={epoch_ms}&"

    # debug url:
    #statuscmd_url = "http://192.168.0.11:9500/fake/status"

    if not suppress_output:
        verbose_pr("STATUS command URL: {0}".format(statuscmd_url))

    dbm.updateSensorLatestStatusSentOn(sensor.ID)

    # Send the request and parse JSON response
    success, response_body, response_json, error_msg = sendSensorHttpRequest(
        statuscmd_url, sensor, timeout=5, expect_json=True, check_status_code=True
    )
    
    if not success:
        if not suppress_output:
            verbose_pr("   - error sending command [@{0}] at {1}: {2}".format(
                sensor.sensorSN, sensor.stat_ip_address, error_msg))
        return False

    status_data = response_json['data']

    #status data is of the form:
    #MODE=STATION_Mode,FIRMWARE_VERSION=1.14,S/N=25000009,SSID=agvm,HOST_NAME=tolis,LOCAL_IP=192.168.0.235,MASK=255.255.255.0,MAC_ADDRESS=b4:3a:45:66:a2:c4,GATEWAY=192.168.0.1,TEMPERATURE=22.56,BATTERY_VOLTAGE=3.32,RSSI=-39
    #i. extract the values into a dict
    #ii. update the sensor object
    status_parts = status_data.split(',')
    status_dict = {}
    for part in status_parts:
        key_value = part.split('=')
        if len(key_value) != 2:
            continue
        key = key_value[0].strip()
        value = key_value[1].strip()
        status_dict[key] = value
    
    wifi_signal = 0
    if 'RSSI' in status_dict:
        try:
            rssi_value = int(status_dict['RSSI'])
            wifi_signal = convertRssiToSignalLevel(rssi_value)
        except ValueError:
            wifi_signal = 0

    status_dict['WIFI_SIGNAL'] = wifi_signal

    #print(status_dict)
    #print(sensor.__dict__)

    appendToSensorStatusLog(sensor, status_dict, suppress_output=suppress_output)
    dbm.updateSensorStatus(sensor.ID, status_dict)

    sensor = dbm.getSensorByID(sensor.ID)

    #current_fw_version = status_dict.get('FIRMWARE_VERSION', '')
    current_fw_version = sensor.stat_firmware_version

    #if FIRMWARE_VERSION starts with "BL_", the sensor is in bootloader mode
    if current_fw_version.startswith('BL_'):
        #Sensor is in Bootloader mode
        if not suppress_output:
            verbose_pr("   - sensor SN {0} is in bootloader mode [@{1}]".format(sensor.sensorSN, sensor.stat_ip_address))
        
        if shouldUpdateFirmware(sensor):
            submitCommandOther(sensor, wait_before_sending=False)
    else:
        #Sensor is in Application mode
        pass

    if not suppress_output:
        verbose_pr("   - CommandStatus completed [@{0}]".format(sensor.sensorSN))

    return True

def submitCommandUpdateConfiguration(sensor):
    global config, dbm

    verbose_pr(" - CommandUpdateConfiguration start [@{0}]".format(sensor.sensorSN))

    sensor_profile = dbm.getSensorProfileByID(sensor.profileID)
    if sensor_profile is None:
        verbose_pr("   - error: sensor profile ID {0} not found [@{1}]".format(sensor.profileID, sensor.sensorSN))
        return
    
    #verbose_pr(f"   - sensor profile: {sensor_profile.__dict__} [@{sensor.sensorSN}]")

    #construct the configuration update URL
    #http://192.168.0.235/api?command=CONFIGURE&datetime=`date '+%s'`000
    # &name=hostname
    # &sleep_time_after_sec_Station_mode=120
    # &sleep_time_after_sec_AP_mode=120
    # &wakeup_every_min=1
    # &temp_threshold=45
    # &send_heartbeat_every_sec=10
    # &start_time_for_accel_data_sec=0
    # &send_accel_data_every_min=5
    # &vibration_threshold_in_mg=800
    # &vibration_threshold_frequency_in_Hz=25
    # &acc_data_measure_time=5000
    # &accel_full_scale=16
    epoch_ms = common.now_timestamp() * 1000
    configcmd_url = f"http://{sensor.stat_ip_address}/api?command=CONFIGURE&datetime={epoch_ms}&"

    config_params = {
        'sleep_time_after_sec_Station_mode': sensor_profile.sleep_time_after_sec,
        'wakeup_every_min': sensor_profile.wakeup_every_min,
        'temp_threshold': round(sensor_profile.temperature_threshold),
        'send_heartbeat_every_sec': sensor_profile.send_heartbeat_every_sec,
        'start_time_for_accel_data_sec': sensor_profile.start_time_for_accel_data_sec,
        'send_accel_data_every_min': sensor_profile.send_accel_data_every_min,
        'vibration_threshold_in_mg': sensor_profile.vibration_threshold,
        'vibration_threshold_frequency_in_Hz': sensor_profile.vibration_threshold_frequency,
        'acc_data_measure_time': sensor_profile.acc_data_measure_time,
        'accel_full_scale': sensor_profile.accel_full_scale,
    }

    if sensor.name is not None:
        config_params['name'] = sensor.name

    for key, value in config_params.items():
        configcmd_url += f"{key}={value}&"

    verbose_pr("Configuration Update command URL: {0}".format(configcmd_url))

    # Send the request
    success, response_body, response_json, error_msg = sendSensorHttpRequest(
        configcmd_url, sensor, timeout=5, expect_json=False, check_status_code=False
    )

    if not success:
        verbose_pr("   - error sending configuration update command [@{0}] at {1}: {2}".format(
            sensor.sensorSN, sensor.stat_ip_address, error_msg))
        return

    if response_body:
        verbose_pr("   - response: {0} [@{1}]".format(response_body, sensor.sensorSN))
    
    dbm.clearSensorUpdateConfigurationFlag(sensor.ID)
    verbose_pr(" - CommandUpdateConfiguration finished [@{0}]".format(sensor.sensorSN))

def shouldUpdateFirmware(sensor):
    global config, dbm

    if sensor.firmware_version == 0:
        return False

    # for testing, force firmware update even if versions match
    #return True
    
    #sensor.firmware_version is an integer of the form
    # - 10009 if the version is 1.09
    # - 10015 if the version is 1.15
    # i.e., major * 10000 + minor * 1
    fw_major = sensor.firmware_version // 10000
    fw_minor = sensor.firmware_version % 10000
    fw_str = "{0}.{1:02d}".format(fw_major, fw_minor)

    verbose_pr(f"   - current firmware version: {sensor.stat_firmware_version} [@{sensor.sensorSN}]")
    verbose_pr(f"   - target firmware version: {fw_str} [@{sensor.sensorSN}]")

    if sensor.stat_firmware_version == fw_str:
        verbose_pr(f"   - firmware is up to date [@{sensor.sensorSN}]")
        return False

    verbose_pr(f"   - firmware update needed [@{sensor.sensorSN}]")
    return True

def submitCommandEnableBootloaderMode(sensor):
    global config, dbm

    verbose_pr(" - CommandEnableBootloaderMode start [@{0}]".format(sensor.sensorSN))

    #send the command
    # http://192.168.0.235/api?command=BOOT_LOADER&datetime=`date '+%s'`&
    epoch_ms = common.now_timestamp() * 1000
    bootloader_cmd_url = f"http://{sensor.stat_ip_address}/api?command=BOOT_LOADER&datetime={epoch_ms}&"

    verbose_pr("Enable Bootloader Mode command URL: {0}".format(bootloader_cmd_url))

    # Send the request
    success, response_body, response_json, error_msg = sendSensorHttpRequest(
        bootloader_cmd_url, sensor, timeout=5, expect_json=False, check_status_code=False
    )

    if not success:
        verbose_pr("   - error sending enable bootloader mode command [@{0}] at {1}: {2}".format(
            sensor.sensorSN, sensor.stat_ip_address, error_msg))
        return

    if response_body:
        verbose_pr("   - response: {0} [@{1}]".format(response_body, sensor.sensorSN))

    dbm.updateSensorFirmwareUpdateStatus(sensor.ID, 2, 0, "Bootloader mode enabled")
    dbm.updateSensorHeartbeatsAfterMeasurement(sensor.ID, 0)
    verbose_pr(" - CommandEnableBootloaderMode finished [@{0}]".format(sensor.sensorSN))

# dictionary of sensor SN to thread objects for firmware update
firmware_update_threads = {}

def submitFirmwareUpdateCommand(sensor, update_buffer, line_no: int):
    global config, dbm

    verbose_pr("    - [@{0}] fw line #{1} = {2}".format(sensor.sensorSN, line_no, update_buffer.strip()))

    # we need to construct a URL like:
    # http://192.168.0.235/api?command=FIRMWARE_UPDATE&hex=.020000040800F2&d=0
    # where:
    # - Replace colon ":" with dot "." in each line, since it is invalid character in GET parameter
    # - the d=0 at the end is dummy and is there in order to add a "&" to the end. It is required and documented by Diagnonic
    #epoch_ms = common.now_timestamp() * 1000
    hex_data = update_buffer.strip().replace(':', '.')
    fwupdatecmd_url = f"http://{sensor.stat_ip_address}/api?command=FIRMWARE_UPDATE&hex={hex_data}&d=0"

    #verbose_pr("FirmwareUpdate command URL: {0}".format(fwupdatecmd_url))
    dbm.updateSensorLatestHeartbeatOn(sensor.ID)

    # Send the request
    success, response_body, response_json, error_msg = sendSensorHttpRequest(
        fwupdatecmd_url, sensor, timeout=5, expect_json=False, check_status_code=True
    )
    
    if not success:
        #verbose_pr("   - error sending firmware update command [@{0}] at {1}: {2}".format(
        #    sensor.sensorSN, sensor.stat_ip_address, error_msg))
        return False
    
    #verbose_pr("FirmwareUpdate command response body: {0}".format(response_body))
    return True

def handleFirmwareUpdate(sensor):
    global config, dbm, firmware_update_threads, running

    MAX_TRIES = 3
    firmware_update_started = False
    firmware_update_success = False
    firmware_update_message = ""

    dbm.updateSensorFirmwareUpdateStatus(sensor.ID, 3, 0, "Firmware update started")

    verbose_pr("   - FirmwareUpdate thread started [@{0}]".format(sensor.sensorSN))

    try:
        #first, get a fresh status from the sensor
        tries = 0
        res = False
        while tries < MAX_TRIES and running:
            tries += 1
            res = submitCommandStatus(sensor, wait_before_sending=False, suppress_output=True)
            if res:
                verbose_pr(f"     - STATUS command OK on try #{tries}")
                break
            else:
                verbose_pr(f"     - STATUS command failed on try #{tries}, retrying...")
                time.sleep(4)
        
        if not res:
            verbose_pr(f"   - FirmwareUpdate failed to get initial STATUS after {MAX_TRIES} tries [@{sensor.sensorSN}]")
            raise Exception("Failed to get initial STATUS from sensor")


        #refresh the sensor object from the database
        sensor = dbm.getSensorByID(sensor.ID)

        #if fw version is not in bootloader mode, we cannot proceed
        if not sensor.stat_firmware_version.startswith('BL_'):
            verbose_pr(f"   - FirmwareUpdate cannot proceed, sensor not in bootloader mode [@{sensor.sensorSN}]")
            raise Exception("Sensor not in bootloader mode")

        #load firmware entry
        new_fw = dbm.getFirmwareByID(sensor.firmware_version)
        if new_fw is None:
            verbose_pr(f"   - FirmwareUpdate cannot proceed, firmware ID {sensor.firmware_version} not found [@{sensor.sensorSN}]")
            raise Exception("Firmware entry not found")
        
        new_fw_source = new_fw.source
        #new_fw_source should be a long text with lines
        #each line is a chunk of firmware data of the form:
        # :26FFDA00FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF4D6C113524
        # all lines start with ':' and contain hex data

        #if it's empty, we cannot proceed
        if not new_fw_source:
            verbose_pr(f"   - FirmwareUpdate cannot proceed, firmware source is empty [@{sensor.sensorSN}]")
            raise Exception("Firmware source is empty")
    
        new_fw_source = new_fw_source.strip()
        if len(new_fw_source) == 0:
            verbose_pr(f"   - FirmwareUpdate cannot proceed, firmware source is empty after stripping [@{sensor.sensorSN}]")
            raise Exception("Firmware source is empty after stripping")
        
        #split into lines
        fw_lines = new_fw_source.splitlines()
        if not fw_lines or len(fw_lines) == 0:
            verbose_pr(f"   - FirmwareUpdate cannot proceed, firmware source has no lines [@{sensor.sensorSN}]")
            raise Exception("Firmware source has no lines")
        
        #remove empty lines
        fw_lines = [line for line in fw_lines if line.strip()]
        
        #check that all lines are valie
        for line in fw_lines:
            line = line.strip()

            #each line should start with ':'
            if not line.startswith(':'):
                verbose_pr(f"   - FirmwareUpdate cannot proceed, invalid firmware line: {line} [@{sensor.sensorSN}]")
                raise Exception("Invalid firmware line format")
        
            #check that the rest of the line is valid hex
            hex_data = line[1:]
            try:
                bytes_data = bytes.fromhex(hex_data)
            except ValueError:
                verbose_pr(f"   - FirmwareUpdate cannot proceed, invalid hex data in firmware line: {line} [@{sensor.sensorSN}]")
                raise Exception("Invalid hex data in firmware line")


        firmware_update_started = True

        dbm.updateSensorFirmwareUpdateStatus(sensor.ID, 4, 0, "Firmware update in progress")


        for fw_line_iteration in range(len(fw_lines)):
            if not running:
                verbose_pr(f"   - FirmwareUpdate thread stopping due to shutdown [@{sensor.sensorSN}]")
                break

            progress_perc = int((fw_line_iteration + 1) * 100 / len(fw_lines))
            dbm.updateSensorFirmwareUpdateStatus(sensor.ID, 4, progress_perc)

            #stick the sensor heartbeats_after_measurement to 1
            dbm.updateSensorHeartbeatsAfterMeasurement(sensor.ID, 1)
            verbose_pr(f"   - FirmwareUpdate iteration {fw_line_iteration} [@{sensor.sensorSN}]")
            #wait a few seconds to simulate work
            tries = 0
            res = False
            while tries < MAX_TRIES and running:
                tries += 1
                #verbose_pr(f"   - try #{tries}")
                #get the current firmware line
                curUpdateBuffer = fw_lines[fw_line_iteration]
                res = submitFirmwareUpdateCommand(sensor, curUpdateBuffer, fw_line_iteration)
                if res:
                    verbose_pr(f"     - FWUpdate command OK on try #{tries}")
                    break
                else:
                    verbose_pr(f"     - FWUpdate command failed on try #{tries}, retrying...")
                    time.sleep(4)
            
            if not res:
                verbose_pr(f"   - FirmwareUpdate failed after {MAX_TRIES} tries [@{sensor.sensorSN}]")
                raise Exception("Firmware update command failed")
            # if res:
            #     verbose_pr(f"   - STATUS command OK")
            # else:
            #     verbose_pr(f"   - STATUS command failed")
            # time.sleep(1)

        firmware_update_success = True
        firmware_update_message = "Firmware update completed successfully"
        verbose_pr("   - FirmwareUpdate thread finished [@{0}]".format(sensor.sensorSN))
    except Exception as e:
        firmware_update_success = False
        firmware_update_message = str(e)
        verbose_pr(f"   - FirmwareUpdate thread error [@{sensor.sensorSN}]: {str(e)}")
    finally:
        dbm.updateSensorHeartbeatsAfterMeasurement(sensor.ID, -1)
        dbm.updateSensorFirmwareUpdateStatus(sensor.ID, 0, 0, firmware_update_message)
        if firmware_update_started:
            dbm.resetSensorFirmwareVersion(sensor.ID)
        # Clean up: remove this thread from the dictionary when done
        if sensor.sensorSN in firmware_update_threads:
            del firmware_update_threads[sensor.sensorSN]
            verbose_pr(f"   - FirmwareUpdate thread cleaned up from dictionary [@{sensor.sensorSN}]")

def submitCommandFirmwareUpdate(sensor):
    global config, dbm, firmware_update_threads

    verbose_pr(" - CommandFirmwareUpdate start [@{0}]".format(sensor.sensorSN))
    verbose_pr(f"   - heartbeats_after_measurement: {sensor.heartbeats_after_measurement} [@{sensor.sensorSN}]")

    #
    # we should launch a dedicated thread that will handle the firmware update process
    # every heartbeat from the sensor will be used as an opportunity
    # to check if there's a dedicated thread running for that sensor
    # if not, we launch it
    # for that, we can use a dictionary of sensor SN to thread objects
    #

    # check if there's already a thread in the dictionary for this sensor
    # and if it's alive
    if sensor.sensorSN in firmware_update_threads:
        thread = firmware_update_threads[sensor.sensorSN]
        if thread.is_alive():
            verbose_pr(f"   - firmware update thread already running for sensor SN {sensor.sensorSN} [@{sensor.sensorSN}]")
            return
        else:
            #this should not happen, but just in case,
            #remove the thread from the dictionary
            del firmware_update_threads[sensor.sensorSN]
    
    #launch a new thread for firmware update
    thread = Thread(target=handleFirmwareUpdate, args=(sensor,))
    thread.start()
    firmware_update_threads[sensor.sensorSN] = thread

def submitCommandOther(sensor, wait_before_sending=True):
    global config, dbm

    verbose_pr(" - CommandOther start [@{0}]".format(sensor.sensorSN))

    if wait_before_sending:
        #wait a few seconds to avoid overwhelming the sensors with commands
        sleep_seconds = config.General.get('sensorsdaemon_wait_for_commands_seconds', 2)
        verbose_pr(f"   - waiting {sleep_seconds} seconds [@{sensor.sensorSN}]")
        time.sleep(sleep_seconds)

    verbose_pr(f"   - CommandOther submitting [@{sensor.sensorSN}]")
    #pprint(sensor.__dict__)

    #if sensor firmware starts with "BL_", the sensor is in bootloader mode
    if sensor.stat_firmware_version.startswith("BL_"):
        verbose_pr(f"   - sensor SN {sensor.sensorSN} is in bootloader mode")
        if shouldUpdateFirmware(sensor):
            verbose_pr(f"   - submitting firmware update command [@{sensor.sensorSN}]")
            submitCommandFirmwareUpdate(sensor)
        else:
            verbose_pr(f"   - firmware is up to date, no action needed [@{sensor.sensorSN}]")
            #if heartbeats_after_measurement == 2, we just send a STATUS command to refresh sensor state
            # if sensor.heartbeats_after_measurement == 2:
            #     verbose_pr(f"   - just sending STATUS command to refresh sensor state [@{sensor.sensorSN}]")
            #     submitCommandStatus(sensor, wait_before_sending=False)
    else:
        # sensor is in application mode
        
        # first, we check if we need to update the firmware
        if shouldUpdateFirmware(sensor):
            submitCommandEnableBootloaderMode(sensor)
        # then, we check if we need to update the configuration
        elif sensor.flag_update_configuration:
            submitCommandUpdateConfiguration(sensor)
            return
        
class JSONResult:
    def __init__(self):
        self.success = False
        self.message = None
        self.data = None
    
    def Success(self, data, ok_message = None):
        self.success = True
        self.message = ok_message
        self.data = data
        return self

    def Error(self, message):
        self.success = False
        self.message = message
        return self
class IPCRequestHandler(IPCRequestBaseHandler):

    def get(self, path) -> IPCRequestResult:
        getResult = IPCRequestResult().NotFound()

        if path == "/":
            getResult = IPCRequestResult(200, "text/html", service_name)
        elif path == "/status":
            getResult = IPCRequestResult(200, "text/html", "OK")
        else:
            getResult = IPCRequestResult().NotFound()
        
        return getResult

    def json_post(self, path, post_data) -> IPCRequestResult:
        jsonResult = JSONResult().Error("unkown error")
        
        if path == '/status':
            jsonResult = self.handleStatus(post_data)
        elif path == '/event/heartbeat':
            jsonResult = self.handleEventHeartbeat(post_data)
        elif path == '/event/measurement':
            jsonResult = self.handleEventMeasurement(post_data)
        else:
            return IPCRequestResult().NotFound()

        return IPCRequestResult().JSON(jsonResult)

    def handleStatus(self, post_data):
        global config, dbm

        jsonResult = JSONResult().Success({
            
        }, "Engine is up and running")
        return jsonResult

    def handleEventHeartbeat(self, post_data):
        global config, dbm


        sensor_sn = post_data.get('sensor_sn', None)
        if sensor_sn is None:
            return JSONResult().Error("missing sensor_sn")

        sensor = dbm.getSensorBySerialNumber(sensor_sn)
        if sensor is None:
            return JSONResult().Error("sensor with SN {0} not found".format(sensor_sn))

        verbose_pr("Sensor Heartbeat #{0} [@{1}]".format(sensor.heartbeats_after_measurement, post_data))

        appendToHeartbeatLog(sensor)

        #we need to chech heartbeats_after_measurement
        # if it is < 1, we do not process this heartbeat
        # if it is = 1, we send a STATUS command to the sensor
        # if it is > 1, we send other commands as needed (e.g., configuration update)
        #if sensor.heartbeats_after_measurement < 1:
        #    return JSONResult().Success({}, "heartbeat ignored, waiting for measurement")
        
        action = None
        if sensor.heartbeats_after_measurement == -1:
            verbose_pr(f"   - heartbeats_after_measurement is {sensor.heartbeats_after_measurement} [@{sensor.sensorSN}]")
            verbose_pr(f"     - status firmware version: {sensor.stat_firmware_version} [@{sensor.sensorSN}]")
            #if sensor is in Bootloader mode, send a status command to refresh state
            if sensor.stat_firmware_version.startswith("BL_"):
                action = 'Status Command (Bootloader Mode)'
                verbose_pr(f"   - just sending STATUS command to refresh sensor state [@{sensor.sensorSN}]")
                t = Thread(target=submitCommandStatus, args=(sensor,))
                t.start()
        elif sensor.heartbeats_after_measurement == 1:
            action = 'Status Command'
            #submitCommandStatus(sensor)
            #run submitCommandStatus in a separate thread to avoid blocking
            t = Thread(target=submitCommandStatus, args=(sensor,))
            t.start()
        elif sensor.heartbeats_after_measurement > 1:
            action = 'Other Command'
            #submitCommandOther(sensor)
            #run submitCommandOther in a separate thread to avoid blocking
            t = Thread(target=submitCommandOther, args=(sensor,))
            #t = Thread(target=submitCommandStatus, args=(sensor,))
            t.start()
        else:
            return JSONResult().Success({}, "heartbeat ignored, waiting for measurement")
            #return JSONResult().Error(f'invalid heartbeats_after_measurement value: {sensor.heartbeats_after_measurement}')
    
        return JSONResult().Success({}, f'heartbeat processed, action: {action}')

    def handleEventMeasurement(self, post_data):
        global config, dbm


        sensor_sn = post_data.get('sensor_sn', None)
        if sensor_sn is None:
            return JSONResult().Error("missing sensor_sn")

        sensor = dbm.getSensorBySerialNumber(sensor_sn)
        if sensor is None:
            return JSONResult().Error("sensor with SN {0} not found".format(sensor_sn))

        verbose_pr("Sensor Measurement Event [@{0}]".format(post_data))

        #wake up the measurements processing loop
        measurementsProcessingWait.set()

        return JSONResult().Success({}, "measurement event processed")


#
# checkMeasurementFileValidity
# checks if a measurement file matches the expected pattern
# (20251027_101710_90000001.bin)
# and if so, extracts the measurement date and sensor serial number
#
def checkMeasurementFileValidity(filename):
    import re
    pattern = r'^(\d{8})_(\d{6})_(\d{8})\.bin$'
    match = re.match(pattern, filename)
    if not match:
        return False, None, None

    date_str = match.group(1)  # YYYYMMDD
    time_str = match.group(2)  # HHMMSS
    sensor_sn = match.group(3) # sensor serial number

    #construct measurement datetime
    measurement_datetime_str = f"{date_str}{time_str}"  # YYYYMMDDHHMMSS
    from datetime import datetime
    measurement_datetime = datetime.strptime(measurement_datetime_str, "%Y%m%d%H%M%S")

    return True, measurement_datetime, sensor_sn


#
# processMeasurements
# a batch process that parses and translates new measurement files
#
def processMeasurements():
    global config, dbm, measurements_folder, hatsensors_measurements_folder, hatsensors_measurements_done_folder, hatsensors_measurements_bad_folder

    #verbose_pr("[processMeasurements] checking for new measurements...")

    #we expect measurement files in the hatsensors_measurements_folder
    #with names like: 20251027_101710_90000001.bin
    
    measurement_files = [f for f in os.listdir(hatsensors_measurements_folder) if os.path.isfile(os.path.join(hatsensors_measurements_folder, f)) and f.endswith('.bin')]

    #verbose_pr("[processMeasurements] found {0} measurement files".format(len(measurement_files)))
    
    if len(measurement_files) == 0:
        return
    
    for measurement_file in measurement_files:
        #check that it matches the expected pattern
        fileIsValid, measurementDate, sensorSN = checkMeasurementFileValidity(measurement_file)
        #print (f" - processing file: {measurement_file}, valid: {fileIsValid}, date: {measurementDate}, sensorSN: {sensorSN}")

        #load the sensor from the database
        sensor = dbm.getSensorBySerialNumber(sensorSN)
        if sensor is None:
            fileIsValid = False
            verbose_pr(" - sensor with SN {0} not found for measurement file: {1}".format(sensorSN, measurement_file))

        #pprint(sensor.__dict__)

        #load the sensor profile
        sensor_profile = dbm.getSensorProfileByID(sensor.profileID)
        if sensor_profile is None:
            fileIsValid = False
            verbose_pr(" - sensor profile with ID {0} not found for measurement file: {1}".format(sensor.profileID, measurement_file))
        
        measurementFilePath = os.path.join(hatsensors_measurements_folder, measurement_file)

        if not fileIsValid:
            verbose_pr(" - skipping invalid measurement file: {0}".format(measurement_file))
            #move the file to the bad folder
            badFilePath = os.path.join(hatsensors_measurements_bad_folder, measurement_file)
            os.rename(measurementFilePath, badFilePath)
            continue
        
        #load the data as binary and parse it
        
        with open(measurementFilePath, 'rb') as f:
            measurementData = f.read()
        
        #parse the measurement data
        # the data are in records of 7 bytes:
        # each record is:
        # - 1 byte: control byte (don't care for now)
        # - 2 bytes: acceleration X (signed int16)
        # - 2 bytes: acceleration Y (signed int16)
        # - 2 bytes: acceleration Z (signed int16)
        record_size = 7
        num_records = len(measurementData) // record_size

        if num_records < 1000:
            verbose_pr(" - measurement file {0} has too few records ({1}), skipping".format(measurement_file, num_records))
            #move the file to the bad folder
            badFilePath = os.path.join(hatsensors_measurements_bad_folder, measurement_file)
            os.rename(measurementFilePath, badFilePath)
            continue

        verbose_pr(" - measurement file: {0}, records: {1}".format(measurement_file, num_records))
        
        #convert raw acceleration to g
        accel_full_scale = sensor_profile.accel_full_scale
        scaling_factor = accel_full_scale / SENSOR_FIXED_POINT_G_FACTOR
        verbose_pr(" - accel full scale: {0} g".format(accel_full_scale))
        verbose_pr(" - scaling factor: {0}".format(scaling_factor))

        measurements = []        
        for i in range(num_records):
            record = measurementData[i*record_size:(i+1)*record_size]
            control_byte = record[0]
            raw_acc_x, raw_acc_y, raw_acc_z = struct.unpack('<hhh', record[1:7])

            acc_x = raw_acc_x * scaling_factor
            acc_y = raw_acc_y * scaling_factor
            acc_z = raw_acc_z * scaling_factor

            measurements.append({
                'control_byte': control_byte,
                'acc_x': acc_x,
                'acc_y': acc_y,
                'acc_z': acc_z
            })

        #print the first 10 measurements
        #verbose_pr(" - sensor temperature: {0} C".format(sensor.stat_temperature))
        #verbose_pr("   - first 10 measurements: {0}".format(measurements[:10]))

        #now we should create 3 CSV files for X, Y, Z and 1 csv file for temperature
        # like:
        # - 2025-02-28-085300-000_324278_X_WFMACC_0110D6AABAAH_AAAB_wAAA.csv
        # - 2025-02-28-085300-000_324278_Y_WFMACC_0120D6AABAAH_AAAB_wAAA.csv
        # - 2025-02-28-085300-000_324278_Z_WFMACC_0130D6AABAAH_AAAB_wAAA.csv
        # - 2025-02-28-085300-000_324278_Z_TMP_0A3FA0AAAAAH_AAAB_wAAA.csv
        # where 324278 is the sensor serial number
        base_filename = measurementDate.strftime("%Y-%m-%d-%H%M%S-000") + f"_{sensorSN}"
        #verbose_pr(" - base filename: {0}".format(base_filename))
        x_filename = f"{base_filename}_X_WFMACC_0110D6AABAAH_AAAB_wAAA.csv"
        y_filename = f"{base_filename}_Y_WFMACC_0120D6AABAAH_AAAB_wAAA.csv"
        z_filename = f"{base_filename}_Z_WFMACC_0130D6AABAAH_AAAB_wAAA.csv"
        tmp_filename = f"{base_filename}_Z_TMP_0A3FA0AAAAAH_AAAB_wAAA.csv"
        x_filepath = os.path.join(measurements_folder, x_filename)
        y_filepath = os.path.join(measurements_folder, y_filename)
        z_filepath = os.path.join(measurements_folder, z_filename)
        tmp_filepath = os.path.join(measurements_folder, tmp_filename)

        #write the temperature file with contents like:
        #°C
        #23.2500
        verbose_pr(" - writing temperature file: {0}".format(tmp_filename))
        with open(tmp_filepath, 'w') as f:
            f.write("°C\n")
            f.write("{0:.4f}".format(sensor.stat_temperature))
        
        #write the X, Y, Z files as:
        # Time Delta (us)
        # 150.166748
        # Y (g)
        # 0.640287
        # 0.557280
        # 0.714018
        # ...
        # where Time Delta is 1 / sampling_rate * 1e6
        time_delta_us = 1.0 / SENSOR_SAMPLING_RATE_HZ * 1E6
        #verbose_pr(" - time delta (us): {0}".format(time_delta_us))

        #open all 3 files
        #verbose_pr(" - writing X, Y, Z files")
        header_lines = ["Time Delta (us)\n", f"{time_delta_us:.6f}\n" ]
        with open(x_filepath, 'w') as fx, open(y_filepath, 'w') as fy, open(z_filepath, 'w') as fz:
            fx.writelines(header_lines)
            fy.writelines(header_lines)
            fz.writelines(header_lines)

            fx.write("X (g)\n")
            fy.write("Y (g)\n")
            fz.write("Z (g)\n")

            for measurement in measurements:
                fx.write("{0:.6f}\n".format(measurement['acc_x']))
                fy.write("{0:.6f}\n".format(measurement['acc_y']))
                fz.write("{0:.6f}\n".format(measurement['acc_z']))

        #move the processed measurement file to the done folder
        doneFilePath = os.path.join(hatsensors_measurements_done_folder, measurement_file)
        os.rename(measurementFilePath, doneFilePath)

        verbose_pr(" - measurement files written")




#
# measurementsProcessingLoop
#
measurementsProcessingWait=Event()
def measurementsProcessingLoop():
    global config, dbm, running

    verbose_pr("[measurementsProcessingLoop] thread started")

    while running:
        processMeasurements()

        #verbose_pr("[measurementsProcessingLoop] waiting for event...")
        measurementsProcessingWait.wait(10)
        measurementsProcessingWait.clear()
        

    verbose_pr("[measurementsProcessingLoop] thread terminated")

#
# set timezone to UTC
#
os.environ['TZ'] = 'UTC'
if sys.platform != 'win32':
    import time
    time.tzset()

service_name="HAT Sensors Daemon"
config_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../config')
config = Config(config_dir)

#make sure that config.General has the needed entries
required_config_keys = [
    'sensorsdaemon_lock_file',
    'sensorsdaemon_http_api_listen_address',
    'sensorsdaemon_http_api_listen_port',
    'sensorsdaemon_wait_for_commands_seconds'
]
for key in required_config_keys:
    if key not in config.General:
        common.error_exit(f"Missing required config key: {key}")

measurements_folder_relative = config.General['measurements_folder']
# measurements_folder_relative is relative to the script's parent directory
script_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(script_dir)
measurements_folder = os.path.join(parent_dir, measurements_folder_relative)
measurements_folder = os.path.realpath(measurements_folder)

if not os.path.isdir(measurements_folder):
    common.error_exit('measurements folder {0} not found'.format(measurements_folder))

hatsensors_measurements_folder = os.path.join(measurements_folder, 'hat_sensors')
if not os.path.isdir(hatsensors_measurements_folder):
    #create the hat_sensors measurements folder
    os.mkdir(hatsensors_measurements_folder)

hatsensors_measurements_done_folder = os.path.join(hatsensors_measurements_folder, 'done')
if not os.path.isdir(hatsensors_measurements_done_folder):
    os.mkdir(hatsensors_measurements_done_folder)

hatsensors_measurements_bad_folder = os.path.join(hatsensors_measurements_folder, 'bad')
if not os.path.isdir(hatsensors_measurements_bad_folder):
    os.mkdir(hatsensors_measurements_bad_folder)

sensor_status_rel_folder = config.General['sensor_status_folder']
sensor_status_folder = os.path.join(parent_dir, sensor_status_rel_folder)
sensor_status_folder = os.path.realpath(sensor_status_folder)

sensor_responses_folder = os.path.join(sensor_status_folder, 'sensor_responses')
if not os.path.isdir(sensor_responses_folder):
    os.mkdir(sensor_responses_folder)


dbm = DBModels(config.Database)

if __name__ == '__main__':
    running=True
    mainWait=Event()

    parser = argparse.ArgumentParser(description=service_name)
    parser.add_argument('-v', '--verbose', action='store_true', help='verbose output')
    args = parser.parse_args()
                                     
    lock_file = config.General['sensorsdaemon_lock_file']
    verbose_pr(os.path.dirname(os.path.abspath(__file__)))
    lock_file_fullpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), lock_file)

    verbose_pr("Using lock file: {0}".format(lock_file_fullpath))

    lock = FileLock(lock_file_fullpath)

    try:
        lock.acquire(.1)
    except:
        common.error_exit("service already running")

    pr("{0} started".format(service_name))

    mainLoopWaitTime = 5
    signal.signal(signal.SIGINT, sigHandler)
    signal.signal(signal.SIGABRT, sigHandler)
    signal.signal(signal.SIGTERM, sigHandler)
    if sys.platform == 'win32':
        signal.signal(signal.SIGBREAK, sigHandler)
        #pr("signal.SIGBREAK={0}".format(signal.SIGBREAK))
        mainLoopWaitTime=0.5

    print("Starting IPC server at {0}:{1}...".format(config.General['sensorsdaemon_http_api_listen_address'], config.General['sensorsdaemon_http_api_listen_port']))
    ipcServer = IPCServer(config.General['sensorsdaemon_http_api_listen_address'], config.General['sensorsdaemon_http_api_listen_port'], IPCRequestHandler)
    ipcServer.start()

    # jobsManager = JobsManager()
    # jobsManager.start()

    # start a thread that will process measurement events
    measurementsProcessingThread = Thread(target=measurementsProcessingLoop, args=())
    measurementsProcessingThread.start()

    while running:
        mainWait.wait(mainLoopWaitTime)
        mainWait.clear()

    #pr("(waiting for JobsManager to finish)")
    #jobsManager.join()

    #join measurement processing thread
    verbose_pr("Waiting for measurements processing thread to finish...")
    measurementsProcessingThread.join()    

    pr("{0} terminated".format(service_name))
