#!/usr/bin/env python3

import shutil
import sys
from config import Config
import common
import re
import os
from datetime import datetime

import json
from pprint import pprint

import numpy as np
from filtering import HighPassFilter, Convert
from fft import FourierTransform
import hatlib
from dbmodels import DBModels
from hatdiagnostics import HATDiagnostics, MeasurementPointDescriptorsAssembler

debugPlots = False
class MeasurementPointExporter:
    _mp_name = None
    _axis_code = None
    lines = []
        
    def __init__(self, mp_name, axis_code):
        self._mp_name = mp_name
        self._axis_code = axis_code
        self.lines = []
    
    def addValue(self, code, value):
        #round the value to 3 decimal places
        value = round(value, 3)
        if code:
            self.lines.append('{0},{1},{2},{3}'.format(self._mp_name, self._axis_code, code, value))
        else:
            self.lines.append('{0},{1},{2}'.format(self._mp_name, self._axis_code, value))

    def addArray(self, code, array):
        line = '{0},{1},{2}'.format(self._mp_name, self._axis_code, code)
        for value in array:
            #if value is number, round it to 3 decimal places
            #else if it is a array/list, round each value to 3 decimal places
            if isinstance(value, (list, tuple, np.ndarray)):
                line += ',[{0}]'.format(','.join([str(round(v, 3)) for v in value]))
            else:
                line += ',{0}'.format(round(value, 3))
            #line += ',{0}'.format(value)
        
        self.lines.append(line)

def pr(msg):
    print(msg)
    sys.stdout.flush()

appIdentifier = 'hat AdviseOnline v1.4 - Build date 25/07/2023'

config_path = './config'

config = Config(config_path)

dbm = DBModels(config.Database)

devDebug = False
if 'dev_debug' in config.General:
    devDebug = config.General['dev_debug']

if devDebug:
    common.log('devDebug mode is ON')

tabletMode = False
if 'tablet_mode' in config.General:
    tabletMode = config.General['tablet_mode']

if tabletMode:
    common.log('tabletMode is ON')

sampling_frequency = config.General['fs']
sampling_period = 1 / sampling_frequency

if not tabletMode:
    from filelock import FileLock
    lock_file = config.General['organize_lock_file']
    organize_enable_file = config.General['organize_enable_file']

    #check if the organize_enable_file exists
    #it must exist and have a value = 1 in order to run the organize_measurements.py script
    organize_enable = common.read_enable_file(organize_enable_file)

    #common.log('organize_enable = {0}'.format(organize_enable))

    if not organize_enable:
        #quietly exit
        #common.log('organize_measurements.py is disabled')
        sys.exit(0)

    lock = FileLock(lock_file)

    try:
        lock.acquire(.1)
    except:
        common.error_exit("process already running")

common.log('organize_measurements.py started')

#pprint(config.General)
if tabletMode:
    measurements_grace_seconds = 0
    complete_batch_grace_seconds = 0
else:
    measurements_grace_seconds = config.General['measurements_grace_seconds']
    complete_batch_grace_seconds = config.General['complete_batch_grace_seconds']

diagnostics_path = os.path.join(config_path, 'diagnostics')
hat_config_file = os.path.join(config_path, 'hat_config.json')

def checkDiagnosticsRepoSyncPath(config_general):
    
    if not 'diagnostics_repo_sync_path' in config_general:
        return None, None, None
    
    diagnostics_repo_sync_path = os.path.realpath(config_general['diagnostics_repo_sync_path'])
    if not os.path.isdir(diagnostics_repo_sync_path):
        return None, None, None
    
    diagrepo_version_file = os.path.join(diagnostics_repo_sync_path, 'version.json')
    if not os.path.isfile(diagrepo_version_file):
        return None, None, None

    diagrepo_version = None
    #read the version.json file
    with open(diagrepo_version_file, 'r') as f:
        try:
            diagrepo_version = json.load(f)
        except Exception as e:
            return None, None, None
            

    diagnostics_repo_sync_repository = os.path.join(diagnostics_repo_sync_path, 'repository')
    if not os.path.isdir(diagnostics_repo_sync_repository):
        return None, None, None

    diagrepo_config_dir = os.path.join(diagnostics_repo_sync_repository, 'config')
    diagrepo_diagnostics_dir = os.path.join(diagnostics_repo_sync_repository, 'diagnostics')

    if not os.path.isdir(diagrepo_config_dir):
        return None, None, None
    
    if not os.path.isdir(diagrepo_diagnostics_dir):
        return None, None, None

    diagrepo_hatconfig_file = os.path.join(diagrepo_config_dir, 'hat_config.json')
    if not os.path.isfile(diagrepo_hatconfig_file):
        return None, None, None
    
    return diagrepo_version, diagrepo_hatconfig_file, diagrepo_diagnostics_dir

    
diagrepo_version, diagrepo_hat_config_file, diagrepo_diagnostics_path = checkDiagnosticsRepoSyncPath(config.General)

#if both diagrepo_hat_config_file and diagrepo_diagnostics_path are not None, update the diagnostics_path and hat_config_file
if diagrepo_hat_config_file and diagrepo_diagnostics_path:
    common.log('Diagnostics repo: version={0}, channel={1}'.format(diagrepo_version['version'], diagrepo_version['channel']))
    hat_config_file = diagrepo_hat_config_file
    diagnostics_path = diagrepo_diagnostics_path


hat_config = common.load_json(hat_config_file)
config_measurements = hat_config['measurements']
config_axes = hat_config['axes']
config_diagnostic_categories = hat_config['diagnostic_categories']
#pprint(config_axes)



sensors_config_file = os.path.join(config_path, 'sensors.json')
sensors_config = common.load_json(sensors_config_file)

ship_data_file = os.path.join(config_path, 'ship.json')
ship_data = common.load_json(ship_data_file)

if ship_data['id'] != config.General['shipid']:
    common.error_exit('ship with id {0} not found in {1}'.format(config.General['shipid'], ship_data_file))

ship_systems = {}
ship_sensors = {}
ship_mps = {}
for system in ship_data['system']:
    #define an object ms
    cur_system = {
        'id': system['id'],
        'sys_name_id': system['sys_name_id'],
        'mach_group_id': system['mach_group_id'],
        'mach_deck_id': system['mach_deck_id'],
        'mach_type_id': system['mach_type_id'],
        'sys_location_id': system['sys_location_id'],
        'sys_location_no_id': system['sys_location_no_id'],
        'name': system['name'],
        'subsystems': {}
    }
    for subsystem in system['subsystem']:
        cur_subsystem = {
            'id': subsystem['id'],
            'systemid': system['id'],
            'name': subsystem['name'],
            'speed': subsystem['speed'],
            'measurementpoints': {},
        }
        for mp in subsystem['measurementpoint']:
            #check if the mp['id'] is a key in the sensors_config
            mp_id_s = '{0}'.format(mp['id'])
            if not mp_id_s in sensors_config:
                common.error_exit('sensor with id {0} not found in sensors.json'.format(mp['id']))
            
            mapped_sensorid = sensors_config[mp_id_s]
            
            cur_measurementpoint = {
                'id': mp['id'],
                'subsystemid': cur_subsystem['id'],
                'systemid': cur_system['id'],
                'name': mp['name'],
                'sensorid': mapped_sensorid,
            }
            ship_sensors[cur_measurementpoint['sensorid']] = cur_measurementpoint
            ship_mps[cur_measurementpoint['id']] = cur_measurementpoint

            cur_subsystem['measurementpoints'][mp['id']] = cur_measurementpoint
        cur_system['subsystems'][subsystem['id']] = cur_subsystem
    
    ship_systems[system['id']] = cur_system    

#pprint(ship_systems)
#pprint(ship_sensors)

system_config_measurements_bands = {}

ship_systems_ids = ship_systems.keys()

hat_config_file_dir = os.path.dirname(hat_config_file)

for sysid in ship_systems_ids:
    #print('system {0}'.format(sysid))
    sys_subdirs = common.get_subdirs(hat_config_file_dir, '{0}\\s.*'.format(sysid))
    if len(sys_subdirs) == 0:
        continue
    #we found a system-specific hat_config folder
    sys_subdir = sys_subdirs[0]
    sys_subdir_path = os.path.join(hat_config_file_dir, sys_subdir)    
    #look for a 'hat_config.json' file in the subdir
    sys_files = common.get_files(sys_subdir_path, 'hat_config.json')
    if len(sys_files) == 0:
        continue
    #we found a hat_config.json file in the subdir
    sys_file = sys_files[0]
    sys_file_path = os.path.join(sys_subdir_path, sys_file)
    #load the json file
    sys_hat_config = None
    with open(sys_file_path, 'r') as sf:
        try:
            sys_hat_config = json.load(sf)
        except Exception as e:
            common.error_exit('error loading hat_config.json file {0}: {1}'.format(sys_file_path, e))
    
    if sys_hat_config is None:
        continue
    #sys_hat_config should contain a 'bands' key
    if not 'measurements' in sys_hat_config:
        continue

    sys_hat_config_measurements = sys_hat_config['measurements']

    #sys_hat_config_measurements should contain a 'bands' key
    if not 'bands' in sys_hat_config_measurements:
        continue

    sys_hat_config_measurements_bands = sys_hat_config_measurements['bands']
    
    system_config_measurements_bands[sysid] = sys_hat_config_measurements_bands
   
#
# Measurements History configuration
#
#check 'axes' is in config_measurements
if not 'axes' in config_measurements:
    common.error_exit('hat_config.json --> "measurements" does not contain "axes"')
config_measurements_axes_arr = config_measurements['axes']
#config_measurements_axes_arr should be an array
if not isinstance(config_measurements_axes_arr, list):
    common.error_exit('hat_config.json --> "measurements" --> "axes" is not an array')
config_measurements_axes = common.convertArrayToDict(config_measurements_axes_arr)
config_measurements_axes_hash = common.reverseHash(config_measurements_axes)
#pprint(config_measurements_axes)
#pprint(config_measurements_axes_hash)

#check 'measuring_points' is in config_measurements
if not 'measuring_points' in config_measurements:
    common.error_exit('hat_config.json --> "measurements" does not contain "measuring_points"')
config_measurements_mps_arr = config_measurements['measuring_points']
#config_measurements_mps_arr should be an array
if not isinstance(config_measurements_mps_arr, list):
    common.error_exit('hat_config.json --> "measurements" --> "measuring_points" is not an array')
config_measurements_mps = common.convertArrayToDict(config_measurements_mps_arr)
config_measurements_mps_hash = common.reverseHash(config_measurements_mps)
#pprint(config_measurements_mps)
#pprint(config_measurements_mps_hash)

#check 'descriptors' is in config_measurements
if not 'descriptors' in config_measurements:
    common.error_exit('hat_config.json --> "measurements" does not contain "descriptors"')
config_measurements_descriptors_arr = config_measurements['descriptors']
#config_measurements_descriptors_arr should be an array
if not isinstance(config_measurements_descriptors_arr, list):
    common.error_exit('hat_config.json --> "measurements" --> "descriptors" is not an array')
config_measurements_descriptors = common.convertArrayToDict(config_measurements_descriptors_arr)
config_measurements_descriptors_hash = common.reverseHash(config_measurements_descriptors)
#pprint(config_measurements_descriptors)
#pprint(config_measurements_descriptors_hash)


measurements_folder_relative = config.General['measurements_folder']
measurements_folder = os.path.realpath(measurements_folder_relative)

if not os.path.isdir(measurements_folder):
    common.error_exit('measurements folder {0} not found'.format(measurements_folder))

unused_measurements_folder = os.path.join(measurements_folder_relative, 'unused')
if not os.path.isdir(unused_measurements_folder):
    #create the unused measurements folder
    os.mkdir(unused_measurements_folder)



batches_folder_relative = config.General['batches_folder']
batches_folder = os.path.realpath(batches_folder_relative)

if not os.path.isdir(batches_folder):
    #create the batches folder
    os.mkdir(batches_folder)

pending_batches_folder = os.path.join(batches_folder, 'pending')
if not os.path.isdir(pending_batches_folder):
    #create the pending batches folder
    os.mkdir(pending_batches_folder)

complete_batches_folder = os.path.join(batches_folder, 'complete')
if not os.path.isdir(complete_batches_folder):
    #create the complete batches folder
    os.mkdir(complete_batches_folder)

incomplete_batches_folder = os.path.join(batches_folder, 'incomplete')
if not os.path.isdir(incomplete_batches_folder):
    #create the incomplete batches folder
    os.mkdir(incomplete_batches_folder)

processed_batches_folder = os.path.join(batches_folder, 'processed')
if not os.path.isdir(processed_batches_folder):
    #create the processed batches folder
    os.mkdir(processed_batches_folder)

bad_batches_folder = os.path.join(batches_folder, 'bad')
if not os.path.isdir(bad_batches_folder):
    #create the processed batches folder
    os.mkdir(bad_batches_folder)

csv_out_folder = os.path.join(batches_folder, 'csv')
if not os.path.isdir(csv_out_folder):
    #create the processed batches folder
    os.mkdir(csv_out_folder)

if tabletMode:
    json_out_folder = os.path.join(batches_folder, 'json')
    if not os.path.isdir(json_out_folder):
        #create the json output folder
        os.mkdir(json_out_folder)

#
# search in the measurements folder for files with this pattern:
# YYYY-MM-DD-hhmmss-fff_<sensorid>_<axis>_<WFMACC|TMP>_<random characters>.csv
# where:
#   YYYY-MM-DD-hhmmss-fff is the timestamp of the measurement
#   <sensorid> is the id of the sensor (chars)
#   <axis> is the axis of the measurement (X, Y or Z)
#   <WFMACC|TMP> is the type of measurement (accelerometer or temperature)
#   <random characters> is a string of random characters
#

#pattern = re.compile(r'(\d{4}-\d{2}-\d{2}-\d{6}-\d{3})_(\d+)_(X|Y|Z)_(WFMACC|TMP)_(\w+).csv')
pattern = re.compile(r'(\d{4}-\d{2}-\d{2}-\d{6}-\d{3})_(\w+)_(X|Y|Z)_(WFMACC|TMP|RMSVEL|RMSACC)_(\w+).csv')


measurement_files = []
for root, dirs, files in os.walk(measurements_folder):
    for file in files:
        match = pattern.match(file)
        if match:            
            #check the last modified time of the file
            #and compute the elapsed time since the last modification
            #if the elapsed time is less than 1 minute, skip the file
            file_path = os.path.join(root, file)
            file_stat = os.stat(file_path)
            file_mtime = file_stat.st_mtime
            file_mtime_dt = common.timestamp_to_datetime(file_mtime)
            file_mtime_elapsed = datetime.now() - file_mtime_dt
            
            if file_mtime_elapsed.total_seconds() >= measurements_grace_seconds:
                #extract info from the filename based on the match
                timestamp_str = match.group(1)
                sensorid = match.group(2)

                measurement_type = match.group(4)

                unused_file_types = ['RMSVEL', 'RMSACC']
                if measurement_type in unused_file_types:
                    #move the file to the unused folder
                    src_file = os.path.join(measurements_folder, file)
                    dst_file = os.path.join(unused_measurements_folder, file)
                    os.rename(src_file, dst_file)
                else:
                    #get the sensor object
                    sensor = ship_sensors[sensorid]

                    if not sensor is None:                
                        #parse the timestamp
                        timestamp = common.parse_timestamp(timestamp_str)

                        #round the timestamp to the nearest minute
                        timestamp_snap = timestamp.replace(second=0, microsecond=0)

                        #construct a batchid as YYYYMMDDhhmm from timestamp_snap
                        batchid = timestamp_snap.strftime('%Y%m%d%H%M')

                        file_info = {
                            'filename': file,
                            'timestamp': timestamp,
                            'timestamp_snap': timestamp_snap,
                            'sensorid': sensorid,
                            'sensor': sensor,
                            'axis': match.group(3),
                            'type': match.group(4),
                            'sn': match.group(5),
                            'batchid': batchid,
                        }
                        measurement_files.append(file_info)
    break

#
# move the new measurement files to the pending batches folder
# in subfolders named as
# <batchid>_<systemid>
#

for file_info in measurement_files:
    batch_key = '{0}_{1}_{2}'.format(file_info['batchid'], file_info['sensor']['systemid'], file_info['sensor']['sensorid'])
    batch_folder = os.path.join(pending_batches_folder, batch_key)
    if not os.path.isdir(batch_folder):
        os.mkdir(batch_folder)
    
    db_batch_id = dbm.createBatch(
        file_info['timestamp_snap'], 
        batch_key, 
        file_info['sensor']['systemid'],
        file_info['sensor']['subsystemid'],
        file_info['sensor']['id']
        )
    
    src_file = os.path.join(measurements_folder, file_info['filename'])
    dst_file = os.path.join(batch_folder, file_info['filename'])

    #if in devmode, do not move the files, but copy them
    if devDebug:
        shutil.copy2(src_file, dst_file)
    else:
        os.rename(src_file, dst_file)

    dbm.setBatchStatus(db_batch_id, hatlib.BatchStatus.Pending)

    #pr('moved file {0} to {1}'.format(src_file, dst_file))

#
# check the pending batches folder for batches that all the files are older than
# complete_batch_grace_seconds seconds

pending_batches_to_check = []
#iterate over the pending batches folder
#and check all the files in each batch
#if even one file is newer than complete_batch_grace_seconds seconds
#skip the batch
for root, dirs, files in os.walk(pending_batches_folder):
    for dir in dirs:
        batch_folder = os.path.join(root, dir)
        skip_batch = False
        for root2, dirs2, files2 in os.walk(batch_folder):
            for file2 in files2:
                file_path = os.path.join(root2, file2)
                file_stat = os.stat(file_path)
                file_mtime = file_stat.st_mtime
                file_mtime_dt = common.timestamp_to_datetime(file_mtime)
                file_mtime_elapsed = datetime.now() - file_mtime_dt
                
                if file_mtime_elapsed.total_seconds() < complete_batch_grace_seconds:
                    #skip_batch = True
                    break
            if skip_batch:
                break
        
        if not skip_batch:
            pending_batches_to_check.append(batch_folder)

#
# iterate over the pending_batches_to_check array
# extract the batchid, systemid and subsystemid from the folder name
# and check if the are Complete. A batch is Complete when it contains:
# - 1 TMP file with Z axis
# - 3 WFMACC files with X, Y and Z axis for all the sensors in this sybsystem
#

for batch_folder in pending_batches_to_check:
    batch_folder_basename = os.path.basename(batch_folder)
    batch_folder_basename_parts = batch_folder_basename.split('_')

    batchid = batch_folder_basename_parts[0]
    systemid = int(batch_folder_basename_parts[1])    
    sensorid = batch_folder_basename_parts[2]

    db_batch = dbm.getBatchByKey(batch_folder_basename)

    #pr('checking batch {0} {1}'.format(batchid, systemid))
    
    #pprint(db_batch.ID)    

    #get the system object
    system = ship_systems[systemid]
    sensor = ship_sensors[sensorid]
    #pprint(sensor)

    if False:
        measurementpoints = {}

        for subsystemid in system['subsystems']:
            cur_subsystem = system['subsystems'][subsystemid]
            cur_measurementpoints = cur_subsystem['measurementpoints']
            if sensor['id'] in cur_measurementpoints:
                print('found!')
                pprint(cur_measurementpoints[sensor['id']])
            
                #append the cur_measurementpoints to the measurementpoints array
                measurementpoints.update(cur_measurementpoints[sensor['id']])

    #for each measurementpoint check if there is a TMP file with Z axis and X, Y, Z WFMACC files
    #- if all the above are true, the batch is complete
    #- else
    #   - if more than complete_batch_grace_seconds seconds have passed since 
    #     the last modification of the most recent file, the batch is incomplete
    #   - else the batch is still pending    

    #prepare the check-list of files to check
    batch_chekclist = {}
    batch_chekclist['{0}_Z_TMP'.format(sensorid)] = False
    batch_chekclist['{0}_X_WFMACC'.format(sensorid)] = False
    batch_chekclist['{0}_Y_WFMACC'.format(sensorid)] = False
    batch_chekclist['{0}_Z_WFMACC'.format(sensorid)] = False
    
    if False:
        for measurementpointid in measurementpoints:
                measurementpoint = measurementpoints[measurementpointid]
                pprint(measurementpoint)
                sensorid = measurementpoint['sensorid']            
                batch_chekclist['{0}_Z_TMP'.format(sensorid)] = False
                batch_chekclist['{0}_X_WFMACC'.format(sensorid)] = False
                batch_chekclist['{0}_Y_WFMACC'.format(sensorid)] = False
                batch_chekclist['{0}_Z_WFMACC'.format(sensorid)] = False
   
    #walk through the batch folder
    #also find the most recent file
    most_recent_file_mtime = 0
    for root, dirs, files in os.walk(batch_folder):
        for file in files:
            match = pattern.match(file)
            if match:
                file_sensorid = match.group(2)
                file_axis = match.group(3)
                file_meastype = match.group(4)
                check_key = '{0}_{1}_{2}'.format(file_sensorid, file_axis, file_meastype)
                #print(check_key)
                if check_key in batch_chekclist:
                    batch_chekclist[check_key] = True
                
                #check if the file is the most recent
                file_path = os.path.join(root, file)
                file_stat = os.stat(file_path)
                file_mtime = file_stat.st_mtime
                if file_mtime > most_recent_file_mtime:
                    most_recent_file_mtime = file_mtime
                
    #if all in batch_chekclist are True, the batch is complete    
    batch_complete = True
    for check_key in batch_chekclist:
        if not batch_chekclist[check_key]:
            batch_complete = False
            break

    #if the batch is complete, move it to the complete batches folder
    if batch_complete:
        #move the batch folder to the complete batches folder
        dst_folder = os.path.join(complete_batches_folder, os.path.basename(batch_folder))
        #first check if the dst_folder already exists
        #if it does, move the contents of the batch folder to the complete batches folder
        #else move the batch folder to the complete batches folder
        #needs testing!!!
        if os.path.isdir(dst_folder):
            #move the contents of the batch folder to the complete batches folder
            for root, dirs, files in os.walk(batch_folder):
                for file in files:
                    src_file = os.path.join(root, file)
                    dst_file = os.path.join(dst_folder, file)
                    os.rename(src_file, dst_file)
            #delete the batch folder
            os.rmdir(batch_folder)
        else:
            os.rename(batch_folder, dst_folder)

        dbm.setBatchStatus(db_batch.ID, hatlib.BatchStatus.Complete)

        common.log('batch {0} {1} {2} is complete'.format(batchid, systemid, sensorid))
    else:
        #check if the most recent file in the batch is older than complete_batch_grace_seconds
        #common.log('checking if batch {0} {1} {2} is incomplete'.format(batchid, systemid, subsystemid))
        most_recent_file_mtime_dt = common.timestamp_to_datetime(most_recent_file_mtime)
        most_recent_file_mtime_elapsed = datetime.now() - most_recent_file_mtime_dt
        #print(most_recent_file_mtime_elapsed)
        if most_recent_file_mtime_elapsed.total_seconds() >= complete_batch_grace_seconds:
            #move the batch folder to the incomplete batches folder
            dst_folder = os.path.join(incomplete_batches_folder, os.path.basename(batch_folder))

            #if dst_folder already exists, move the contents of the batch folder to the incomplete batches folder
            #else move the batch folder to the incomplete batches folder
            if os.path.isdir(dst_folder):
                #move the contents of the batch folder to the incomplete batches folder
                for root, dirs, files in os.walk(batch_folder):
                    for file in files:
                        src_file = os.path.join(root, file)
                        dst_file = os.path.join(dst_folder, file)
                        os.rename(src_file, dst_file)
                #delete the batch folder
                os.rmdir(batch_folder)
            else:
                os.rename(batch_folder, dst_folder)
            #os.rename(batch_folder, dst_folder)

            dbm.setBatchStatus(db_batch.ID, hatlib.BatchStatus.Incomplete)
            common.log('batch {0} {1} {2} is incomplete'.format(batchid, systemid, sensorid))

#pprint(complete_batches)
#pprint(incomplete_batches)

#
# process the temperature file
# the file is a CSV file with 1 column
# the first row is the header (°C)
# the second row is the temperature value
#
def process_temperature_file(temperature_file, batch_folder):
    temperature = None
    if not temperature_file is None:
        temperature_file_path = os.path.join(batch_folder, temperature_file['filename'])
        with open(temperature_file_path, 'r') as f:
            lines = f.readlines()
            NumLines = len(lines)
            if NumLines >= 2:
                templine = lines[1].replace('°C', '').strip()
                #try to convert the templine to a float
                #templine may contain non-numerical characters after the temperature value
                #so we need to remove them
                #start from the beginning of the string and as long the character is a digit or a dot
                #keep it, when you find a non-digit or non-dot character, stop
                #and convert the string to a float
                temperature_str = ''
                for c in templine:
                    if c.isdigit() or c == '.':
                        temperature_str += c
                    else:
                        break
                try:
                    temperature = float(temperature_str)
                except:
                    temperature = None
            else:
                #common.error_exit('temperature file {0} has {1} lines'.format(temperature_file['filename'], len(lines)))
                temperature = None
    return temperature

#
# reads the measurement file
# the file is a CSV file with 1 column
# - first row is the header: Time Delta (us)
# - second row is the value of the Time Delta (in microseconds)
# - third row is the header: Y (g)
# - the rest of the rows are the acceleration values
#
def read_measurement_file(meas_file_info, batch_folder, measurementpoint_id, axis):
    global sampling_frequency, sampling_period

    #pprint(meas_file_info)
    meas_file = os.path.join(batch_folder, meas_file_info['filename'])

    #read the csv file
    with open(meas_file, 'r') as f:
        lines = f.readlines()
        if len(lines) < 4:
            #common.error_exit('measurement file {0} has {1} lines'.format(meas_file_info['filename'], len(lines)))
            common.log('measurement file {0} has {1} lines'.format(meas_file_info['filename'], len(lines)))
            return None, None, None
        
        #check the header
        if not lines[0].startswith('Time Delta (us)'):
            #common.error_exit('measurement file {0} has wrong header: {1}'.format(meas_file_info['filename'], lines[0]))
            common.log('measurement file {0} has wrong header: {1}'.format(meas_file_info['filename'], lines[0]))
            return None, None, None
        
        #check the Time Delta value (in microseconds)
        time_delta = float(lines[1]) * 1e-6

        #check the header
        if not lines[2].endswith('(g)\n'):
            #common.error_exit('measurement file {0} has wrong header: {1}'.format(meas_file_info['filename'], lines[2]))
            common.log('measurement file {0} has wrong header: {1}'.format(meas_file_info['filename'], lines[2]))
            return None, None, None
        
        #get the acceleration values
        acceleration_values = []
        for i in range(3, len(lines)):
            curline = lines[i].strip()
            if not curline.replace('.', '1').replace('-', '').isnumeric():
                return None, None, None
            acceleration_values.append(float(lines[i]))
        
        N = len(acceleration_values)

        total_time = N * sampling_period
        total_time_corrected = total_time - time_delta
        
        sampling_period_corrected = total_time_corrected / N

        sampling_period_corrected = time_delta
        sampling_frequency_corrected = 1 / sampling_period_corrected
        

        #pprint(sampling_frequency)
        #pprint(sampling_period)
        #pprint(total_time)
        #pprint(total_time_corrected)
        #pprint(time_delta)
        #pprint(sampling_period_corrected)
        #pprint(sampling_frequency_corrected)
        
        #construct the time array (0..N-1)*sampling_period_corrected
        t = []
        for i in range(0, N):
            t.append(i * sampling_period_corrected)

        return t, acceleration_values, sampling_frequency_corrected


#
# iterate the complete batches folder and call a process_batch function
#
def process_batch(batch_identifier):
    batch_folder = os.path.join(complete_batches_folder, batch_identifier)

    #extract the batchid, systemid and subsystemid from the batch_identifier
    batch_parts = batch_identifier.split('_')
    
    batchid = batch_parts[0]
    systemid = int(batch_parts[1])    

    # the batchid is of the form YYYYMMDDhhmm
    # convert it to a datetime object
    batch_datetime = datetime.strptime(batchid, '%Y%m%d%H%M')

    #get the system object
    system = ship_systems[systemid]

    #find the system nominal frequency (at least one subsystem must have a speed)
    systemNominalFrequency = None
    for subsystemid in system['subsystems']:
        cur_subsystem = system['subsystems'][subsystemid]

        if systemNominalFrequency == None and cur_subsystem['speed'] != None:
            systemNominalFrequency = cur_subsystem['speed']

    if systemNominalFrequency == None:
        common.error_exit('system {0} has no speed'.format(systemid))
    
    measurementpoints = {}
    #iterate over the subsystems and get all the measurementpoints
    for subsystemid in system['subsystems']:
        cur_subsystem = system['subsystems'][subsystemid]

        cur_subsystem_speed = cur_subsystem['speed']
        #if current subsystem speed is None, use the systemNominalFrequency
        if cur_subsystem_speed == None:
            cur_subsystem_speed = systemNominalFrequency
        #print('subsystem {0} speed = {1}'.format(subsystemid, cur_subsystem_speed))

        cur_measurementpoints = cur_subsystem['measurementpoints']

        #iterate over the measurementpoints and add the speed
        for measurementpointid in cur_measurementpoints:
            cur_measurementpoint = cur_measurementpoints[measurementpointid]
            cur_measurementpoint['speed'] = cur_subsystem_speed

        #append the cur_measurementpoints to the measurementpoints array
        measurementpoints.update(cur_measurementpoints)
        

    #get the TMP and WFMACC files
    temperature_files = {}
    acceleration_files = {}
    for root, dirs, files in os.walk(batch_folder):
        for file in files:
            #pprint(file)
            match = pattern.match(file)
            if match:
                timestamp_str = match.group(1)
                sensorid = match.group(2)
                axis = match.group(3)
                measurement_type = match.group(4)
                sn = match.group(5)

                sensor = ship_sensors[sensorid]

                measurementpoint_id = sensor['id']

                timestamp = common.parse_timestamp(timestamp_str)

                timestamp_snap = timestamp.replace(second=0, microsecond=0)

                file_info = {
                    'filename': file,
                    'timestamp': timestamp,
                    'timestamp_snap': timestamp_snap,
                    'sensorid': sensorid,
                    'sensor': sensor,
                    'axis': axis,
                    'type': measurement_type,
                    'sn': sn,
                }
                
                #pprint(measurement_type)
                if measurement_type == 'TMP' and axis == 'Z':
                    temperature_files[measurementpoint_id] = file_info
                    #break
                elif measurement_type == 'WFMACC':                    
                    #check if the acceleration_files dict contains the key measurementpoint_id
                    #if not place an empty dict
                    if not measurementpoint_id in acceleration_files:
                        acceleration_files[measurementpoint_id] = {}
                    
                    acceleration_files[measurementpoint_id][axis] = file_info

        #break

    #pprint(temperature_files)
    #pprint(acceleration_files)
    #pprint(batch_datetime)

    #temperature = process_temperature_file(temperature_file, batch_folder)
    #pprint(temperature)

    measurements = {}
    #process the acceleration files
    for measurementpoint_id in acceleration_files:
        #pprint(measurementpoint_id)
        measurements[measurementpoint_id] = {}
        for axis in acceleration_files[measurementpoint_id]:
            axis_name = config_axes[axis]
            #pprint(axis)
            #pprint(axis_name)
            #common.log('processing measurementpoint {0} axis {1}'.format(measurementpoint_id, axis))
            cur_file = acceleration_files[measurementpoint_id][axis]
            #pprint(cur_file)
            machineNominalFrequency = cur_file['sensor']['speed']
            
            t, acc, Fs = read_measurement_file(cur_file, batch_folder, measurementpoint_id, axis)
            if t is None:
                #the file is bad
                return None, None, None, None, None
            
            #
            # Velocity
            #
            V = Convert.acceleration2velocity(t, acc, Fs)

            #
            # Acceleration FFT
            #
            windowSize = len(acc)
            #print('FFT windowSize={0}'.format(windowSize))
            nOverlap   = 0 # overlap window    

            (accFFT, accFreqs) = FourierTransform.perform_fft_windowed(
                signal=acc, 
                fs=Fs,
                winSize=windowSize,
                nOverlap=nOverlap, 
                window='hann', 
                detrend = False, 
                mode = 'irvine')

            maxAccFFT = np.max(accFFT)

            #
            # Velocity FFT
            #

            windowSize_v = len(V)
            #print('velocity FFT windowSize={0}'.format(windowSize_v))
            nOverlap_v   = 0 # overlap window    

            (velFFT, velFreqs) = FourierTransform.perform_fft_windowed(
                signal=V, 
                fs=Fs,
                winSize=windowSize_v,
                nOverlap=nOverlap_v, 
                window='hann', 
                detrend = False, 
                mode = 'irvine')

            maxVelFFT = np.max(velFFT)

            #
            # Band RMS
            #
            rmsVelocityFFT = FourierTransform.bandRMS(velFreqs, velFFT, 50, 100)
            #common.log('  - rmsVelocityFFT = {0}'.format(rmsVelocityFFT))

            harmonics, arrHarmSampledEst, arrHarmSampledAmp, arrHarmEst, arrHarmEstAmp = hatlib.estimateFundamentalAndHarmonics(Fs, velFreqs, velFFT, machineNominalFrequency)
            #pprint(harmonics)
            #pprint(arrHarmEstAmp)

            measurements[measurementpoint_id][axis] = {
                'Fs': Fs,
                #'t': t,
                'acc': acc,
                'V': V,

                'accFreqs': accFreqs,
                'accFFT': accFFT,
                #'maxAccFFT': maxAccFFT,
                
                'velFreqs': velFreqs,
                'velFFT': velFFT,
                #'maxVelFFT': maxVelFFT,
                'rmsVelocityFFT': rmsVelocityFFT,

                'harmonics': harmonics,
            }

            if debugPlots:
                maxPlotFreq = 600
                common.draw_plot(t, acc, 'Acceleration [{0}, {1}]'.format(measurementpoint_id, axis), 'Time [s]', 'Acceleration [g]', 'b')
                common.draw_plot(t, V, 'Velocity [{0}, {1}]'.format(measurementpoint_id, axis), 'Time [s]', 'Velocity [m/s]', 'r')
                common.draw_plot(accFreqs, accFFT, 'Acceleration FFT [{0}, {1}]'.format(measurementpoint_id, axis), 'Frequency [Hz]', 'Amplitude (g)', 'b', 0.5, (0, Fs/2), (0, 1.25*maxAccFFT))
                common.draw_plot(velFreqs, velFFT, 'Velocity FFT [{0}, {1}]'.format(measurementpoint_id, axis), 'Frequency [Hz]', 'Amplitude (g)', 'r', 0.5, (0, maxPlotFreq), (0, 1.25*maxVelFFT))
    
    for measurementpoint_id in temperature_files:
        temperature_file = temperature_files[measurementpoint_id]
        temperature = process_temperature_file(temperature_file, batch_folder)
        if temperature is None:
            return None, None, None, None, None
        measurements[measurementpoint_id]['TMP'] = temperature
        
    return measurements, batch_datetime, batchid, systemid, subsystemid

jsonResults = {}

bad_batches = []
csv_files_to_save = {}
for root, dirs, files in os.walk(complete_batches_folder):
    #print(root, dirs, files)
    for batch_identifier in dirs:
        #print(batch_identifier)
        batch_identifier_parts = batch_identifier.split('_')
        batchkey_batchid = batch_identifier_parts[0]
        batchkey_systemid = int(batch_identifier_parts[1])    
        batchkey_sensorid = batch_identifier_parts[2]
        cur_sensor = ship_sensors[batchkey_sensorid]

        db_batch = dbm.getBatchByKey(batch_identifier)
        #pprint(db_batch)
        
        batch_folder = os.path.join(root, batch_identifier)
        common.log('processing batch {0}'.format(batch_identifier))
        
        #call the process_batch function
        measurements, batch_datetime, batchid, systemid, subsystemid = process_batch(batch_identifier)
        if measurements is None:
            #the batch is bad, add it to the bad_batches array
            common.log('batch {0} is bad'.format(batch_identifier))
            bad_batches.append(batch_identifier)
            continue

        #pprint(batch_datetime)
        #pprint(batchid)
        #pprint(systemid)
        #pprint(subsystemid)
        #pprint(batchkey_systemid)
        #pprint(batchkey_sensorid)
        #pprint(ship_data)
        #print('batch key: {0} : {1} : {2}'.format(batchkey_batchid, batchkey_systemid, batchkey_sensorid))
        #pprint()
        #sys.exit(0)

        #do a deep copy of the measurements bands
        config_measurements_bands = {}
        config_measurements_bands.update(config_measurements['bands'])
                
        if systemid in system_config_measurements_bands:
            this_system_config_measurements_bands = system_config_measurements_bands[systemid]
            if devDebug:
                common.log(' - system {0} has custom bands'.format(systemid))
            config_measurements_bands.update(this_system_config_measurements_bands)

        #find the system nominal frequency (at least one subsystem must have a speed)
        systemNominalFrequency = None
        for subsystemid in ship_systems[systemid]['subsystems']:
            cur_subsystem = ship_systems[systemid]['subsystems'][subsystemid]

            if systemNominalFrequency == None and cur_subsystem['speed'] != None:
                systemNominalFrequency = cur_subsystem['speed']
        #print('systemNominalFrequency', systemNominalFrequency)

        #save the measurements to a csv file
        #the file name is stats_YYYY.MM.DD_HH.mm.ss_SYSID_SHIPFNTID_COMPANYCODE.csv
        #YYYY.MM.DD_HH.mm.ss: from the batch_datetime
        #SYSID: from the systemid
        #SHIPFNTID: from ship_data['shipId]
        #COMPANYCODE: from ship_data['cmp_code']
        shipCodeId = ship_data['shipId']
        if shipCodeId == None:
            shipCodeId = ship_data['id']        
        csv_filename = 'stats_{0}_{1}_{2}_{3}_{4}.csv'.format(batch_datetime.strftime('%Y.%m.%d_%H.%M.%S'), systemid, shipCodeId, ship_data['cmp_code'], cur_sensor['id'])
        csv_file_path = os.path.join(csv_out_folder, csv_filename)

        #print('writing csv file {0}'.format(csv_file_path))
        
        #construct the csv lines
        csv_lines = []

        #first line: SHIPFNTID,COMPANYCODE,YYYY.MM.DD_HH.mm.ss,SYSID,MPLABELS,name,speed,appIdentifier
        mp_labels = ""  #[<subsystemid>,<measurementpoint_id>,<sensorid>],[<subsystemid>,<measurementpoint_id>,<sensorid>]...
        for measurementpoint_id in measurements:
            cur_mp = ship_mps[measurementpoint_id]
            #print(measurementpoint_id)
            #pprint(cur_mp)
            if mp_labels != "":
                mp_labels += ','
            mp_labels += '[{0},{1},{2}]'.format(cur_mp['subsystemid'], cur_mp['id'], cur_mp['sensorid'])
        csv_lines.append('{0},{1},{2},{3},{4},{5},{6},{7}'.format(shipCodeId, ship_data['cmp_code'], batch_datetime.strftime('%Y.%m.%d_%H.%M.%S'), systemid, mp_labels, system['name'], subsystem['speed'], appIdentifier))

        measurementpoints_descriptors = {}
        for measurementpoint_id in measurements:
            cur_mp = ship_mps[measurementpoint_id]
            mp_name = cur_mp['name']
            cur_measurement = measurements[measurementpoint_id]            
            #pprint(measurementpoint_id)
            #pprint(cur_mp)
            #pprint(cur_measurement)
            measurementpoints_descriptors[mp_name] = {}
            for axis_code in cur_measurement:
                #print(axis_code)
                cur_axis_measurements = cur_measurement[axis_code]
                if axis_code == 'TMP':
                    mp_exp = MeasurementPointExporter(mp_name, 'T')
                    #csv_lines.append('{0},{1},{2}'.format(mp_name, axis_code, cur_axis_measurements))
                    mp_exp.addValue('temperature', cur_axis_measurements)

                    axis_name = 'T'
                    mp_assembler = MeasurementPointDescriptorsAssembler(mp_name, axis_name)
                    mp_assembler.addValue('temperature', cur_axis_measurements)
                else:
                    #map the axis_code to the axis name
                    axis_name = config_axes[axis_code]
                    mp_exp = MeasurementPointExporter(mp_name, axis_name)
                    mp_assembler = MeasurementPointDescriptorsAssembler(mp_name, axis_name)
                    #print(axis_name)
                    #pprint(cur_axis_measurements)
                    cur_harmonics = cur_axis_measurements['harmonics']
                    freq1x = cur_harmonics[1]['estimatedFrequency']
                    mp_assembler.addValue('freq1x', freq1x)
                    
                    #export rmsVelocityFFT
                    cur_Fs = cur_axis_measurements['Fs']
                    cur_acc = cur_axis_measurements['acc']
                    cur_V = cur_axis_measurements['V']                    
                    velFreqs = cur_axis_measurements['velFreqs']
                    velFFT = cur_axis_measurements['velFFT']
                    accFreqs = cur_axis_measurements['accFreqs']
                    accFFT = cur_axis_measurements['accFFT']

                    #compute the RMS velocity FFT in the range 0.88x to 1000 Hz
                    rmsVelocityFFTRange = [0.88*freq1x, 1000]
                    rmsVelocityFFT = FourierTransform.bandRMS(velFreqs, velFFT, rmsVelocityFFTRange[0], rmsVelocityFFTRange[1])
                    mp_exp.addValue('velocityRMS', rmsVelocityFFT)
                    mp_assembler.addValue('velocityRMS', rmsVelocityFFT)

                    rmsAccFFT = FourierTransform.bandRMS(accFreqs, accFFT, rmsVelocityFFTRange[0], rmsVelocityFFTRange[1])
                    mp_exp.addValue('accelerationFrms', rmsAccFFT)
                    mp_assembler.addValue('accelerationFrms', rmsAccFFT)

                    #export the 'estimatedAmplitude' from the first 8 harmonics
                    #pprint(cur_axis_measurements['harmonics'])
                    #print('freq1x', freq1x)
                    harmonics_exp = []
                    for harm_idx in range(1, 9):
                        cur_harm = cur_harmonics[harm_idx]
                        cur_harm_amp = cur_harm['estimatedAmplitude']
                        harmonics_exp.append(cur_harm_amp)
                    #add line with freq1x, amplitude of first harmonic
                    mp_exp.addArray('freq1x', [freq1x, harmonics_exp[0]])
                    mp_assembler.addArray('freq1x', [freq1x, harmonics_exp[0]])
                                    
                    mp_exp.addArray('velocity1x', harmonics_exp)
                    mp_assembler.addArray('velocity1x', harmonics_exp)

                    #export bands
                    bands_rms = []
                    bands_peak = []
                    
                    #system_config_measurements_bands
                    for band_id in config_measurements_bands:
                        band = config_measurements_bands[band_id]
                        band_source = band['source']
                        band_range = band['range']
                        band_exported_values = band['values']
                        #print(band_id, band_source, band_range, band_exported_values)                        
                        if band_source == 'v':
                            source_freqs = velFreqs
                            source_fft = velFFT
                        elif band_source == 'a':
                            source_freqs = accFreqs
                            source_fft = accFFT
                        else:
                            common.error_exit('band source {0} not supported'.format(band_source))
                        
                        #if devDebug:
                        #    common.log(' - band {0} source {1} range {2} values {3}'.format(band_id, band_source, band_range, band_exported_values))
                        #band_range should be an array of 2 numbers
                        #denoting the start and end frequency of the band
                        #if the number is > 0, it is a frequency in Hz
                        #if the number is < 0, it is a fraction of the freq1x
                        
                        #check if the band_range is an array and contains 2 numbers
                        if not isinstance(band_range, list) or len(band_range) != 2:
                            common.error_exit('band range {0} is not an array of 2 numbers'.format(band_range))
                        
                        #check if the band_range contains numbers
                        for band_range_number in band_range:
                            if not isinstance(band_range_number, int) and not isinstance(band_range_number, float):
                                common.error_exit('band range {0} contains non-numeric values'.format(band_range))
                        
                        f_min = band_range[0]
                        f_max = band_range[1]

                        if f_min < 0:
                            #instead of measured freq1x, use the nominal frequency
                            #f_min = freq1x * abs(f_min)
                            f_min = systemNominalFrequency * abs(f_min)
                            #print('if computed based on freq1x, f_min=', freq1x * abs(f_min))
                            #print('if computed based on systemNominalFrequency, f_min=', systemNominalFrequency * abs(f_min))
                        
                        if f_max < 0:
                            #instead of measured freq1x, use the nominal frequency
                            #f_max = freq1x * abs(f_max)
                            f_max = systemNominalFrequency * abs(f_max)
                            #print('if computed based on freq1x, f_max=', freq1x * abs(f_max))
                            #print('if computed based on systemNominalFrequency, f_max=', systemNominalFrequency * abs(f_max))

                        
                        #print('range: {0} - {1}'.format(f_min, f_max))
                        cur_band_rms = []
                        cur_band_peak = []
                        #find the peak amplitude in the band and at what frequency it is
                        band_peak_freq, band_peak_amp = hatlib.bandPeakAmpFreq(source_freqs, source_fft, f_min, f_max)
                        
                        if False:
                            for exp_val_code in band_exported_values:

                                if exp_val_code == 'rms':
                                    band_rms = FourierTransform.bandRMS(source_freqs, source_fft, f_min, f_max)
                                    cur_band_rms.append(band_rms)
                                elif exp_val_code == 'peakampfreq':
                                    #print('band_peak_freq', band_peak_freq)
                                    #print('band_peak_amp', band_peak_amp)
                                    cur_band_rms.extend([band_peak_freq, band_peak_amp])
                        else:
                            band_rms = FourierTransform.bandRMS(source_freqs, source_fft, f_min, f_max)
                            cur_band_rms.append(band_rms)

                        cur_band_peak.append([band_peak_freq, band_peak_amp])

                        if len(cur_band_rms) == 0:
                            cur_band_rms = None
                        elif len(cur_band_rms) == 1:
                            cur_band_rms = cur_band_rms[0]
                        
                        if len(cur_band_peak) == 0:
                            cur_band_peak = None
                        elif len(cur_band_peak) == 1:
                            cur_band_peak = cur_band_peak[0]
                        
                        bands_rms.append(cur_band_rms)
                        bands_peak.append(cur_band_peak)
                    #pprint(band_values)
                    mp_exp.addArray('bandsRMS', bands_rms)
                    mp_exp.addArray('bandsPeak', bands_peak)
                    mp_assembler.addArray('bandsRMS', bands_rms)
                    mp_assembler.addArray('bandsPeak', bands_peak)

                    #export the peak-to-peak value from cur_acc
                    acc_pp = np.max(cur_acc) - np.min(cur_acc)
                    mp_exp.addValue('accelerationTpk2pk', acc_pp)
                    mp_assembler.addValue('accelerationTpk2pk', acc_pp)

                    #calculate the crest factor from cur_acc
                    acc_cf = hatlib.crestFactor(cur_acc)
                    mp_exp.addValue('accelerationCF', acc_cf)
                    mp_assembler.addValue('accelerationCF', acc_cf)

                    #calculate the Kurtosis from cur_acc
                    acc_kurtosis = hatlib.kurtosis(cur_acc)
                    mp_exp.addValue('accelerationKurtosis', acc_kurtosis)
                    mp_assembler.addValue('accelerationKurtosis', acc_kurtosis)

                    #acc_kurtosis2 = hatlib.kurtosis2(cur_acc)
                    #mp_exp.addValue('accelerationKurtosis2', acc_kurtosis2)
                
                #append mp_exp.lines to csv_lines
                #pprint(mp_exp.lines)
                csv_lines.extend(mp_exp.lines)
                mp_exp = None

                measurementpoints_descriptors[mp_name][axis_name] = mp_assembler.descriptors
                mp_assembler = None

        #pprint(measurementpoints_descriptors)
        #collect the descriptors that are listed in config_measurements_descriptors
        #into a new array
        store_descriptors = {}
        for mp_name in measurementpoints_descriptors:
            db_measuringpoint = config_measurements_mps_hash[mp_name]
            db_measuringpoint_id = db_measuringpoint['id']
            #pprint(db_measuringpoint)
            store_descriptors[db_measuringpoint_id] = {}
            for axis_name in measurementpoints_descriptors[mp_name]:
                db_axis = config_measurements_axes_hash[axis_name]
                db_axis_id = db_axis['id']
                #pprint(db_axis)
                store_descriptors[db_measuringpoint_id][db_axis_id] = {}
                for descriptor_id in config_measurements_descriptors:
                    descriptor = config_measurements_descriptors[descriptor_id]
                    descriptor_id = descriptor['id']
                    descriptor_code = descriptor['code']
                    if descriptor_code in measurementpoints_descriptors[mp_name][axis_name]:
                        #pprint(descriptor)
                        cur_value = measurementpoints_descriptors[mp_name][axis_name][descriptor_code]
                        #if cur_value is an array, then descriptor should
                        #have a 'index' key to choose the value from the array
                        if isinstance(cur_value, list):
                            if not 'index' in descriptor:
                                common.error_exit('descriptor {0} has no index key'.format(descriptor_code))
                            descriptor_index = descriptor['index']
                            #if the index is a number, then use it to choose the value from the array
                            #else if the index is a list, then we assume that cur_value is a nested array
                            if isinstance(descriptor_index, int):
                                cur_value = cur_value[descriptor['index']]
                            elif isinstance(descriptor_index, list):
                                cur_value = cur_value[descriptor_index[0]][descriptor_index[1]]
                        #pprint(cur_value)
                        #store_descriptors[db_measuringpoint_id][db_axis_id][descriptor_id] = cur_value
                        dbm.saveMeasurement(
                                db_batch.recorded_on,
                                db_batch.key,
                                db_batch.systemID,
                                db_measuringpoint_id,
                                db_axis_id,
                                descriptor_id,
                                cur_value
                            )
        #pprint(store_descriptors)
        #pprint(db_batch.recorded_on)
        #pprint(db_batch.key)
        #sys.exit(0)
        measurementpoints_descriptors_json = json.dumps(measurementpoints_descriptors)
        
        if batch_identifier not in jsonResults:
            jsonResults[batch_identifier] = {}
        jsonResults[batch_identifier]["descriptors"] = measurementpoints_descriptors
        
        dbm.setBatchDescriptors(db_batch.ID, measurementpoints_descriptors_json)        
        #write lines to csv file


        #with open(csv_file_path, 'w') as f:
        #    for csv_line in csv_lines:
        #        f.write(csv_line + '\n')
        #keep the information about the csv file in csv_files_to_save array
        #and save it later
        csv_files_to_save[batch_identifier] = {
            'csv_file_path': csv_file_path,
            'csv_lines': csv_lines,
            'batch_key': batch_identifier,
        }

        #now that the completed batch has been processed, move it to the processed batches folder
        #but to save disk space, zip the whole batch folder and move the zip file
        #the zip file name is the batch folder name with .zip extension
        #if not devDebug:
        zip_filename = '{0}.zip'.format(batch_identifier)
        zip_file_path = os.path.join(processed_batches_folder, zip_filename)
        if common.zip_dir(batch_folder, zip_file_path):
            #remove the batch folder
            common.remove_dir(batch_folder)
        
        dbm.setBatchStatus(db_batch.ID, hatlib.BatchStatus.Processed, csv_filename)

    
    #why do we brake here?
    #because we want to process only the first level of the complete_batches_folder
    break

diagnognostic_config_file_path = os.path.join(diagnostics_path, 'diagnostic_config.json')
if not os.path.isfile(diagnognostic_config_file_path):
    common.error_exit('diagnognostic_config.json file not found in {0}'.format(diagnostics_path))
diagnostic_config = common.load_json(diagnognostic_config_file_path)
if not 'rules' in diagnostic_config:
    common.error_exit('diagnostic_config.json file does not contain the rules array')
diagnostic_config_rules = diagnostic_config['rules']

batchesForDiagnostics = dbm.getBatchesForDiagnostics()
if batchesForDiagnostics is not None:
    for db_batch in batchesForDiagnostics:
        #pprint(db_batch.ID)
        #common.log('running diagnostics for batch {0}'.format(db_batch.key))
        measurementpoints_descriptors_json = db_batch.descriptors_json
        try:
            measurementpoints_descriptors = json.loads(measurementpoints_descriptors_json)
        except:
            dbm.setBatchDiagnostics(db_batch.ID, 0, None, None, None, 2)
            continue
        batchSystemID = db_batch.systemID
        #pprint(batchSystemID)
        cur_system = ship_systems[batchSystemID]
        #pprint(cur_system)

        diagnostics_files = None
        diagnostics_name = None

        #iterate over the diagnostic_config_rules array
        #and check the 'match' dict, which may contain one or more of the following keys:
        # - system_id
        # - sys_name_id
        # - mach_group_id
        # - mach_deck_id
        # - mach_type_id
        # - sys_location_id
        # - sys_location_no_id
        #if all present keys match the current system, then we found the correct rules
        #pprint(cur_system)
        for diagnostic_config_rule in diagnostic_config_rules:
            diagnostic_config_rule_match = diagnostic_config_rule['match']
            found_match = True
            #print('checking rule {0}'.format(diagnostic_config_rule['name']))
            for diagnostic_config_rule_match_key in diagnostic_config_rule_match:
                #print(' - checking key {0}'.format(diagnostic_config_rule_match_key))
                if not diagnostic_config_rule_match_key in cur_system:
                    found_match = False
                    break
                diagnostic_config_rule_match_value = diagnostic_config_rule_match[diagnostic_config_rule_match_key]
                #print the type of the diagnostic_config_rule_match_value
                #print(' - type of value {0} is {1}'.format(diagnostic_config_rule_match_value, type(diagnostic_config_rule_match_value)))
                if isinstance(diagnostic_config_rule_match_value, int):
                    #if type of diagnostic_config_rule_match_value is numeric, then we compare the values
                    if cur_system[diagnostic_config_rule_match_key] != diagnostic_config_rule_match[diagnostic_config_rule_match_key]:
                        found_match = False
                        break
                elif isinstance(diagnostic_config_rule_match_value, list):
                    #if type of diagnostic_config_rule_match_value is list, then we check if the value is in the list
                    if not cur_system[diagnostic_config_rule_match_key] in diagnostic_config_rule_match_value:
                        found_match = False
                        break
            if found_match:
                diagnostics_files = diagnostic_config_rule['diagnostics']
                diagnostics_name = diagnostic_config_rule['name']
                break
        
        #common.log('found_match', found_match)
        #continue

        if diagnostics_files == None:
            common.log('Warning: no diagnostics found for system {0}'.format(batchSystemID))
            dbm.setBatchDiagnostics(db_batch.ID, 0, None, None, None, 2)
            continue

        if devDebug:
            common.log(' * running diagnostics "{0}" for batch {1}'.format(diagnostics_name, db_batch.key))
            common.log('   diagnostics_path: {0}'.format(diagnostics_path))
            #pprint(diagnostics_files)

        worst_rating_level = 0
        # for all keys in config_diagnostic_categories create an empty array
        diagnostics_results = {key: [] for key in config_diagnostic_categories}
        worst_rating_levels = {key: 0 for key in config_diagnostic_categories}
        calculations = {}
        findings = []
        recomendations = []
        for selected_measurementpoint_id in measurementpoints_descriptors:    
            if devDebug:
                common.log(' @ {0}'.format(selected_measurementpoint_id))
            selected_measurementpoint_descriptors = measurementpoints_descriptors[selected_measurementpoint_id]
            #pprint(selected_measurementpoint_descriptors)
            
            for diagnostics_file in diagnostics_files:
                if devDebug:
                    common.log(' \\- {0}'.format(diagnostics_file))
                diagnostic_file_path = os.path.join(diagnostics_path, diagnostics_file)
                if os.path.isfile(diagnostic_file_path):
                    hat_diagnostic = HATDiagnostics(diagnostics_path)
                    
                    diagname, diagcategory, diagnosis, descriptors , metrics, sys_data = hat_diagnostic.perform_diagnosis(diagnostic_file_path, selected_measurementpoint_id, selected_measurementpoint_descriptors, db_batch.systemID)
                    if devDebug:
                        common.log('    - diagname: {0}'.format(diagname))
                        if sys_data is not None:
                            #pprint(sys_data)
                            common.log('    - override: {0}'.format(json.dumps(sys_data)))
                        common.log('    - diagnosis: {0}'.format(diagnosis))
                        common.log('    - worst_rating_level: {0}'.format(worst_rating_level))
                    
                    #print(' - diagnostics_file', diagnostics_file)
                    #print(' - diagcategory', diagcategory)
                    #print(' - diagnosis', diagnosis)
                    #print(' - descriptors', descriptors)
                    #print(' - metrics', metrics)
                    calculations[diagnostics_file] = {
                        "descriptors": descriptors,
                        "metrics": metrics
                    }

                    if not diagnosis is None:
                        cur_rating_level = diagnosis['rating_level']
                        if devDebug:
                            common.log('    - cur_rating_level: {0}'.format(cur_rating_level))
                            #print(' - cur_rating_level', cur_rating_level)
                        if cur_rating_level == -1:
                            #that means that the machine is not in operation
                            #if not diagnosis is done so far (worst_rating_level == 0)
                            #then characterize the machine as not in operation (-1)
                            if worst_rating_level == 0:
                                worst_rating_level = -1
                            
                            #we don't want to proceed the diagnosis if the machine is not in operation
                            #so we break the loop
                            break
                    
                    diagnostic_message = hat_diagnostic.get_diagnostic_message(diagnosis, config.General['preferred_language'])        
                    
                    if not diagnosis is None:
                        if not diagcategory in diagnostics_results:
                            diagnostics_results[diagcategory] = []
                        diagnostics_results[diagcategory].append({
                            'file': diagnostics_file,
                            'name': diagname,
                            'diagnosis': diagnosis,
                            'messages': diagnostic_message,
                        })
                        rating_level = diagnosis['rating_level']
                        if rating_level > worst_rating_level:
                            worst_rating_level = rating_level
                        if rating_level > worst_rating_levels[diagcategory]:
                            worst_rating_levels[diagcategory] = rating_level

                        diagnostic_message['findings'] = diagnostic_message['findings'].replace('%MPNAME%', selected_measurementpoint_id)
                        diagnostic_message['recomendations'] = diagnostic_message['recomendations'].replace('%MPNAME%', selected_measurementpoint_id)
                        findings.append(diagnostic_message['findings'])
                        recomendations.append(diagnostic_message['recomendations'])
                else:
                    common.log('Warning: diagnostic file {0} not found'.format(diagnostics_file))

        if worst_rating_level > 0:
            #print('worst_rating_level', worst_rating_level)
            #join the findings and recomendations arrays into respective strings, separated by \n
            findings_str = '\n'.join(findings)
            recomendations_str = '\n'.join(recomendations)
            #print(findings_str)
            #print(recomendations_str)
            #pprint(worst_rating_levels)
            #pprint(diagnostics_results)
            diagnostics_json = json.dumps({
                'results': diagnostics_results,
                'rating_levels': worst_rating_levels,
                'calculations': calculations
            })
            dbm.setBatchDiagnostics(db_batch.ID, worst_rating_level, diagnostics_json, findings_str, recomendations_str, 1)
        elif worst_rating_level == -1:
            #the machine is not in operation
            #print('machine is not in operation')
            diagnostics_json = json.dumps({
                'calculations': calculations
            })
            dbm.setBatchDiagnostics(db_batch.ID, -1, diagnostics_json, None, None, 1)
            dbm.markMeasurementsNotInOperation(db_batch.key)
        else:
            #pprint(calculations)
            diagnostics_json = json.dumps({
                'calculations': calculations
            })
            dbm.setBatchDiagnostics(db_batch.ID, 0, diagnostics_json, None, None, 1)

        if batch_identifier not in jsonResults:
            jsonResults[batch_identifier] = {}
        jsonResults[batch_identifier]["diagnostics"] = json.loads(diagnostics_json)

        if worst_rating_level >= 0:
            #save the kept csv file for the batch
            #pprint(csv_files_to_save)
            if db_batch.key in csv_files_to_save:
                csv_file_info = csv_files_to_save[db_batch.key]
                csv_file_path = csv_file_info['csv_file_path']
                csv_lines = csv_file_info['csv_lines']
                with open(csv_file_path, 'w') as f:
                    for csv_line in csv_lines:
                        f.write(csv_line + '\n')

# if we have bad batches, move them to the bad batches folder
if len(bad_batches) > 0:
    for bad_batch in bad_batches:
        src_folder = os.path.join(complete_batches_folder, bad_batch)
        dst_folder = os.path.join(bad_batches_folder, bad_batch)
        #first check if the destination folder exists
        if os.path.exists(dst_folder):
            #if it exists, just move all the files from the src_folder to the dst_folder
            #and then remove the src_folder
            for root, dirs, files in os.walk(src_folder):
                for file in files:
                    src_file = os.path.join(src_folder, file)
                    dst_file = os.path.join(dst_folder, file)
                    if os.path.exists(dst_file):
                        os.remove(dst_file)
                    os.rename(src_file, dst_file)
        else:
            os.rename(src_folder, dst_folder)

if debugPlots:
    common.arrange_plots()
    input('done (hit <Enter>)\n')

#print(batchesDescriptors)
#print(batchesDiagnostics)
if tabletMode:
    #write out batchesDescriptors and batchesDiagnostics
    #pprint(jsonResults)
    for batch_identifier in jsonResults:
        db_batch = dbm.getBatchByKey(batch_identifier)
        batch_datetime = db_batch.recorded_on
        systemid = db_batch.systemID
        cur_sensor = ship_mps[db_batch.mpID]
        #print(f"batch_datetime: {batch_datetime}, systemid: {systemid}, cur_sensor: {cur_sensor}")

        #if batch_datetime is string, parse it into datetime
        if isinstance(batch_datetime, str):
            batch_datetime = datetime.fromisoformat(batch_datetime)
            #print(f"Parsed batch_datetime: {batch_datetime}")

        json_filename = 'batch_{0}_{1}_{2}_{3}_{4}.json'.format(batch_datetime.strftime('%Y.%m.%d_%H.%M.%S'), systemid, shipCodeId, ship_data['cmp_code'], cur_sensor['id'])
        json_file_path = os.path.join(json_out_folder, json_filename)

        #pprint(jsonResults[batch_identifier])
        with open(json_file_path, 'w') as f:
            #pretty print the JSON
            json.dump(jsonResults[batch_identifier], f, indent=4)
