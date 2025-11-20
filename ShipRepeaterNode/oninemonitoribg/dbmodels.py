from typing import Any, Dict, Optional
from dbadapter import DBAdapter, DBObject
import json
import datetime
from pprint import pprint
from sensorsdaemon_codes import *

class DBModels:
    TABLE_SETTINGS = "settings"
    TABLE_BATCHES = "batches"
    TABLE_MEASUREMENTS = "measurements"
    TABLE_MAINTENANCE = "maintenance"
    TABLE_SENSORS = "sensors"
    TABLE_SENSOR_PROFILES = "sensor_profiles"
    TABLE_SENSOR_FIRMWARE = "sensor_firmware"
    TABLE_STATUS_SENSORS = "status_sensors"
    TABLE_STATUS_SYSTEMS = "status_systems"
    TABLE_SENSOR_EVENTS = "sensor_events"
    TABLE_SENSOR_AWAKE_SESSIONS = "sensor_awake_sessions"
    TABLE_AUDIT_LOG = "audit_log"
    TABLE_SENSOR_COMMANDS = "sensor_commands"

    def __init__(self, dbconfig):
        self.dba = DBAdapter(dbconfig)
        return

    #
    # Settings
    #

    def getSetting(self, code):
        setting = self.dba.getDBObject(DBModels.TABLE_SETTINGS, "`code` = '{0}'".format(code))
        return setting
    
    def saveSettingInt(self, code, value: int):
        setting = self.getSetting(code)
        if setting is None:
            new_id = self.dba.insertDict(DBModels.TABLE_SETTINGS, {
                'code': code,
                'value_int': value,
            })
            return new_id

        self.dba.updateDict(DBModels.TABLE_SETTINGS, {
            'value_int': value,
        }, setting.ID)
        return setting.ID

    def saveSettingFloat(self, code, value: float):
        setting = self.getSetting(code)
        if setting is None:
            new_id = self.dba.insertDict(DBModels.TABLE_SETTINGS, {
                'code': code,
                'value_dec': value,
            })
            return new_id

        self.dba.updateDict(DBModels.TABLE_SETTINGS, {
            'value_dec': value,
        }, setting.ID)
        return setting.ID
    
    def saveSettingString(self, code, value: str):
        setting = self.getSetting(code)
        if setting is None:
            new_id = self.dba.insertDict(DBModels.TABLE_SETTINGS, {
                'code': code,
                'value_txt': value,
            })
            return new_id

        self.dba.updateDict(DBModels.TABLE_SETTINGS, {
            'value_txt': value,
        }, setting.ID)
        return setting.ID

    #
    # Batches
    #

    def getBatches(self):
        batches = self.dba.getDBObjects(DBModels.TABLE_BATCHES)
        return batches

    def getBatchesForDiagnostics(self):
        batches = self.dba.getDBObjects(DBModels.TABLE_BATCHES, "`diagstatus` = 0")
        return batches

    def getBatch(self, id):
        job = self.dba.getDBObject(DBModels.TABLE_BATCHES, "`ID` = {0}".format(id))
        return job

    def getBatchByKey(self, key):
        job = self.dba.getDBObject(DBModels.TABLE_BATCHES, "`key` = '{0}'".format(key))
        return job

    def getBatchByCsvFile(self, csvfile):
        job = self.dba.getDBObject(DBModels.TABLE_BATCHES, "`csvfile` = '{0}'".format(csvfile))
        return job

    def createBatch(self, recorded_on, key, system_id, subsystem_id, mp_id):
        existing_batch = self.getBatchByKey(key)

        if existing_batch is not None:
            return existing_batch.ID

        date_now = datetime.datetime.now()
        now_iso = date_now.strftime("%Y-%m-%dT%H:%M:%S")
        
        new_id = self.dba.insertDict(DBModels.TABLE_BATCHES, {
            'retrieved_on': now_iso,
            'recorded_on': recorded_on,
            'key': key,
            'systemID': system_id,
            'subsystemID': subsystem_id,
            'mpID': mp_id,
            'status': 0,
            'csvfile': None,
        })       
        #print("job_id: {0}".format(job_id)) 
        return new_id
    
    def deleteBatch(self, id):
        existing_batch = self.getBatch(id)
        if existing_batch is None:
            return False
        
        self.dba.deleteRecord(DBModels.TABLE_BATCHES, "`ID` = {0}".format(id))
        return True

    def setBatchStatus(self, id, status : int, csvfile = None):
        existing_batch = self.getBatch(id)
        if existing_batch is None:
            return False
        
        valuesDict = {
            'status': status,
        }
        if csvfile is not None:
            valuesDict['csvfile'] = csvfile
        
        self.dba.updateDict(DBModels.TABLE_BATCHES, valuesDict, id)

        return True

    def setBatchDescriptors(self, id, descriptors_json: str):
        existing_batch = self.getBatch(id)
        if existing_batch is None:
            return False
        
        valuesDict = {
            'descriptors_json': descriptors_json,
            'diagstatus': 0,
        }
        
        self.dba.updateDict(DBModels.TABLE_BATCHES, valuesDict, id)

        return True

    def setBatchDiagnostics(self, id, rating_level: int, diagnostics_json: str, findings: str, recomendations: str, diagstatus: int):
        existing_batch = self.getBatch(id)
        if existing_batch is None:
            return False
        
        valuesDict = {
            'rating_level': rating_level,
            'diagnostics_json': diagnostics_json,
            'findings': findings,
            'recomendations': recomendations,
            'diagstatus': diagstatus,
        }
        
        self.dba.updateDict(DBModels.TABLE_BATCHES, valuesDict, id)

        return True

    def setBatchError(self, id, message = None):
        existing_batch = self.getBatch(id)
        if existing_batch is None:
            return False
        
        valuesDict = {
            'status': 9,
        }
        if message is not None:
            valuesDict['message'] = message

        self.dba.updateDict(DBModels.TABLE_BATCHES, valuesDict, id)

        return True
    
    def setBatchDiagUploadedFlag(self, id, diag_uploaded_flag):
        existing_batch = self.getBatch(id)
        if existing_batch is None:
            return False
        
        valuesDict = {
            'diag_uploaded_flag': diag_uploaded_flag,
        }
        
        self.dba.updateDict(DBModels.TABLE_BATCHES, valuesDict, id)

        return True

    def setBatchModbusFlag(self, id, modbus_flag):
        #existing_batch = self.getBatch(id)
        #if existing_batch is None:
        #    return False
        
        valuesDict = {
            'modbus_flag': modbus_flag,
        }
        
        self.dba.updateDict(DBModels.TABLE_BATCHES, valuesDict, id)

        return True
    
    # return all the rows of the latest recored day that has uploaded_flag=0
    #  
    # SELECT * 
    # FROM `hat_batches`
    # WHERE `diagstatus` = 1 AND `diag_uploaded_flag` = 0 AND `rating_level` >= 0
    # AND DATE(`recorded_on`) = (
    #     SELECT DATE(MAX(`recorded_on`))
    #     FROM `hat_batches`
    #     WHERE `diagstatus` = 1 AND `diag_uploaded_flag` = 0
    # )
    def getUploadBatches(self, limit = None):
        wherestr = "`diagstatus` = 1 AND `diag_uploaded_flag` = 0 AND `rating_level` >= 0 AND DATE(`recorded_on`) = (SELECT DATE(MAX(`recorded_on`)) FROM `hat_batches` WHERE `diagstatus` = 1 AND `diag_uploaded_flag` = 0 AND `rating_level` >= 0)"
        batches = self.dba.getDBObjects(DBModels.TABLE_BATCHES, wherestr, "`recorded_on` DESC", limit)
        return batches
    
    #
    # return all the rows that has diagstatus=1 and modbus_flag=0
    # ordered by systemID, subsystemID, mpID, recorded_on desc
    #
    # SELECT * FROM `hat_batches`
    # WHERE `diagstatus` = 1 AND `modbus_flag` = 0
    # ORDER BY `systemID`, `subsystemID`, `mpID`, `recorded_on` DESC
    def getModbusBatches(self):
        batches = self.dba.getDBObjects(DBModels.TABLE_BATCHES, "`diagstatus` = 1 AND `modbus_flag` = 0 AND `rating_level` >= 0", "`systemID`, `subsystemID`, `mpID`, `recorded_on` DESC")
        return batches

    #
    # Measurements
    #

    def saveMeasurement(self, recorded_on, batch_key, systemID, mpID, axisID, descriptorID, value):
        
        if isinstance(recorded_on, datetime.datetime):
            recorded_on_iso = recorded_on.strftime("%Y-%m-%dT%H:%M:%S")
        elif isinstance(recorded_on, str):
            recorded_on_iso = recorded_on

        measurement_dict = {
            'batch_key': batch_key,
            'recorded_on': recorded_on_iso,
            'systemID': systemID,
            'mpID': mpID,
            'axisID': axisID,
            'descriptorID': descriptorID,
            'value': float(value),
        }
       
        new_id = self.dba.insertDict(DBModels.TABLE_MEASUREMENTS, measurement_dict)
        return new_id

    # mark all the measurements of the batch as not in operation
    def markMeasurementsNotInOperation(self, batch_key):
        #update the table `hat_measurements` set `notinoperation` = 1
        #where `batch_key` = batch_key
        #use self.dba.updateDict(self, tablename, valuesdict, idValue, idField = "ID", conn = None)
        self.dba.updateDict(DBModels.TABLE_MEASUREMENTS, 
            {'notinoperation': 1},
            batch_key,
            "batch_key")


    #
    # table sensors
    #
    def getSensorByID(self, sensor_id: int) -> Optional[DBObject]:
        """
        Get a sensor by its ID.
        Returns None if not found.
        """
        sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        return sensor
    
    def getSensorBySerialNumber(self, sensor_sn: str) -> Optional[DBObject]:
        """
        Get a sensor by its serial number.
        Returns None if not found.
        """
        sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`sensorSN` = '{0}'".format(sensor_sn))
        return sensor
    
    def updateSensorHeartbeatsAfterMeasurement(self, sensor_id: int, heartbeats_after_measurement: int) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
        
        valuesDict = {
            'heartbeats_after_measurement': heartbeats_after_measurement,
        }
        
        self.dba.updateDict(DBModels.TABLE_SENSORS, valuesDict, sensor_id)

        return True
    
    def updateSensorLatestHeartbeatOn(self, sensor_id: int) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
        
        date_now = datetime.datetime.now(datetime.timezone.utc)
        now_iso = date_now.strftime("%Y-%m-%dT%H:%M:%S")

        valuesDict = {
            'latest_heartbeat_on': now_iso,
        }
        
        self.dba.updateDict(DBModels.TABLE_SENSORS, valuesDict, sensor_id)

        return True

    def updateSensorLatestStatusSentOn(self, sensor_id: int) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
        
        date_now = datetime.datetime.now(datetime.timezone.utc)
        now_iso = date_now.strftime("%Y-%m-%dT%H:%M:%S")

        valuesDict = {
            'latest_status_sent_on': now_iso,
        }
        
        self.dba.updateDict(DBModels.TABLE_SENSORS, valuesDict, sensor_id)

        return True

    def clearSensorUpdateConfigurationFlag(self, sensor_id: int) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
        
        now_iso = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
        valuesDict = {
            'flag_update_configuration': 0,
            'latest_configuration_sent_on': now_iso,
        }
        
        self.dba.updateDict(DBModels.TABLE_SENSORS, valuesDict, sensor_id)

        return True

    def resetSensorFirmwareVersion(self, sensor_id: int) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
        
        valuesDict = {
            'firmware_version': 0,
        }
        
        self.dba.updateDict(DBModels.TABLE_SENSORS, valuesDict, sensor_id)

        return True
    
    def updateSensorFirmwareUpdateStatus(self,  sensor_id: int, 
                                                fw_update_status: int,
                                                fw_update_progress: int,
                                                fw_update_message: Optional[str] = None) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
    
        valuesDict = {
            'firmware_update_status': fw_update_status,
            'firmware_update_progress': fw_update_progress,
        }

        if fw_update_message is not None:
            valuesDict['firmware_update_message'] = fw_update_message
        
        self.dba.updateDict(DBModels.TABLE_SENSORS, valuesDict, sensor_id)
        return True

    def updateSensorStatus(self, sensor_id: int, status_dict: Dict[str, Any]) -> bool:
        existing_sensor = self.dba.getDBObject(DBModels.TABLE_SENSORS, "`ID` = {0}".format(sensor_id))
        if existing_sensor is None:
            return False
        
        #we need to translate the status_dict keys to match the database fields
        #we will use the following mapping
        #status keys: FIRMWARE_VERSION=1.14,SSID=agvm,HOST_NAME=tolis,LOCAL_IP=192.168.0.235,MASK=255.255.255.0,MAC_ADDRESS=b4:3a:45:66:a2:c4,GATEWAY=192.168.0.1,TEMPERATURE=22.56,BATTERY_VOLTAGE=3.32,RSSI=-39,WIFI_SIGNAL=78
        #database fields: stat_firmware_version, stat_ssid, stat_hostname, stat_ip_address, stat_ip_mask, stat_ip_gateway, stat_mac_address, stat_temperature, stat_battery_voltage
        key_mapping = {
            'FIRMWARE_VERSION': 'stat_firmware_version',
            'SSID': 'stat_ssid',
            'HOST_NAME': 'stat_hostname',
            'LOCAL_IP': 'stat_ip_address',
            'MASK': 'stat_ip_mask',
            'GATEWAY': 'stat_ip_gateway',
            'MAC_ADDRESS': 'stat_mac_address',
            'TEMPERATURE': 'stat_temperature',
            'BATTERY_VOLTAGE': 'stat_battery_voltage',
            'RSSI': 'stat_wifi_rssi',
            'WIFI_SIGNAL': 'stat_wifi_signal',
        }

        update_dict = {}
        for key, value in status_dict.items():
            if key in key_mapping:
                update_dict[key_mapping[key]] = value

        #update the latest_status_result_on to now UTC
        date_now = datetime.datetime.now(datetime.timezone.utc)
        now_iso = date_now.strftime("%Y-%m-%dT%H:%M:%S")
        update_dict['latest_status_result_on'] = now_iso

        #if in the existing sensor the column 'name' is NULL,
        #update it with the HOST_NAME from the status_dict
        if existing_sensor.name is None and 'HOST_NAME' in status_dict:
            update_dict['name'] = status_dict['HOST_NAME']

        #print("Updating sensor ID {0} with status:".format(sensor_id))
        #pprint(update_dict)
        self.dba.updateDict(DBModels.TABLE_SENSORS, update_dict, existing_sensor.ID)

        return True

    #
    # sensor_profiles
    #
    def getSensorProfileByID(self, profile_id: int) -> Optional[DBObject]:
        """
        Get a sensor profile by its ID.
        Returns None if not found.
        """
        profile = self.dba.getDBObject(DBModels.TABLE_SENSOR_PROFILES, "`ID` = {0}".format(profile_id))
        return profile

    #
    # sensor_firmware
    #
    def getFirmwareByID(self, firmware_id: int) -> Optional[DBObject]:
        """
        Get a sensor firmware entry by its ID.
        Returns None if not found.
        """
        firmware = self.dba.getDBObject(DBModels.TABLE_SENSOR_FIRMWARE, "`ID` = {0}".format(firmware_id))
        return firmware

    #
    # status_systems
    #
    def getSystemsStatus(self):
        systems_status = self.dba.getDBObjects(DBModels.TABLE_STATUS_SYSTEMS)
        return systems_status

    #
    # status_sensors
    #
    def getSensorStatus(self, mpID: int) -> Optional[DBObject]:
        """
        Get the status of a sensor by its mpID.
        Returns None if no status is found.
        """
        sensor_status = self.dba.getDBObject(DBModels.TABLE_STATUS_SENSORS, "`mpID` = {0}".format(mpID))
        return sensor_status

    def getSensorsStatus(self):
        sensors_status = self.dba.getDBObjects(DBModels.TABLE_STATUS_SENSORS)
        return sensors_status

    def update_status_snapshot(self, mpID: int, status: Dict[str, Any]) -> None:
        existing_record = self.getSensorStatus(mpID)
        if existing_record:
            self.dba.updateDict(DBModels.TABLE_STATUS_SENSORS, status, mpID, "mpID")

    #
    # Maintenance
    #

    # getMaintenanceRecordsToUpload
    # return all the rows that have uploaded_flag=0
    # up to limit rows
    def getMaintenanceRecordsToUpload(self, limit = None):
        wherestr = "`uploaded_flag` = 0"        
        maintenance_records = self.dba.getDBObjects(DBModels.TABLE_MAINTENANCE, wherestr, "`recorded_on` DESC", limit)
        return maintenance_records
    
    #
    # setMaintenanceRecordUploadedFlag
    # set the uploaded_flag of the maintenance record with the given id to the given value
    #
    def setMaintenanceRecordUploadedFlag(self, id, uploaded_flag):
        existing_record = self.dba.getDBObject(DBModels.TABLE_MAINTENANCE, "`ID` = {0}".format(id))
        if existing_record is None:
            return False
        
        valuesDict = {
            'uploaded_flag': uploaded_flag,
        }
        
        self.dba.updateDict(DBModels.TABLE_MAINTENANCE, valuesDict, id)

        return True


    #
    # Sensor Events
    #
    def getLatestSensorEvents(self, last_id : int, limit=1000):
        """
SELECT ID, mpID, event_type, occurred_on
FROM hat_sensor_events
WHERE ID > :last_id
  AND origin = 1          -- sensor
  AND event_type IN (2,3) -- 2=batch, 3=heartbeat
  AND status = 0          -- 0: pending
ORDER BY ID ASC
LIMIT 1000;        

        getDBObjects(self, tablename, where = None, order = None, limit = None, conn = None)
        """
        wherestr = "`ID` > {0} AND `origin` = {1} AND `event_type` IN ({2}, {3}) AND `status` = {4}".format(
            last_id, ORIGIN_SENSOR, EVT_BATCH, EVT_HEARTBEAT, 0
        )
        return self.dba.getDBObjects(DBModels.TABLE_SENSOR_EVENTS, wherestr, "`ID` ASC", limit)

    def markEventDone(self, event_id: int) -> None:
        self.dba.updateDict(DBModels.TABLE_SENSOR_EVENTS, {'status': 1}, event_id)

    #
    # Sensor Awake Session
    #
    def create_awake_session(self, mpID: int, trigger_source: int, trigger_event_id: int):
        date_now = datetime.datetime.now()
        now_iso = date_now.strftime("%Y-%m-%dT%H:%M:%S")

        session_dict = {
            'mpID': mpID,
            'trigger_source': trigger_source,
            'trigger_event_id': trigger_event_id,
        }
        new_id = self.dba.insertDict(DBModels.TABLE_SENSOR_AWAKE_SESSIONS, session_dict)
        return new_id

    def update_awake_expiry(self, mpID: int, session_id: int, seconds_from_now: int) -> None:
        expiry_time = datetime.datetime.now() + datetime.timedelta(seconds=seconds_from_now)
        self.dba.updateDict(DBModels.TABLE_SENSOR_AWAKE_SESSIONS, {'expected_expiry_at': expiry_time}, session_id)

    #
    # Audit Log
    #
    def audit(self, mpID: int, session_id: int, event_type: int, event_id: Optional[int], action: str, payload: Optional[dict] = None):
        audit_dict = {
            'mpID': mpID,
            'session_id': session_id,
            'kind': event_type,
            'ref_id': event_id,
            'phase': action,
            'detail': json.dumps(payload) if payload else None,
        }
        self.dba.insertDict(DBModels.TABLE_AUDIT_LOG, audit_dict)


    #
    # Sensor Commands
    #

    # Collapse older queued commands of collapsible types
    """
-- Collapse older queued commands of collapsible types
UPDATE hat_sensor_commands sc
JOIN (
  SELECT command_type, MAX(ID) AS max_id
  FROM hat_sensor_commands
  WHERE mpID = :mpID AND status = 1 /* queued */ AND collapsible = 1
  GROUP BY command_type
) t ON t.command_type = sc.command_type
SET sc.status = 5 /* superseded */
WHERE sc.mpID = :mpID
  AND sc.status = 1 /* queued */
  AND sc.collapsible = 1
  AND sc.ID < t.max_id;
    """
    def collapse_queued_commands(self, mpID: int) -> None:
        query = f"""
        UPDATE {self.dba.tables_prefix}{DBModels.TABLE_SENSOR_COMMANDS} sc
        JOIN (
          SELECT command_type, MAX(ID) AS max_id
          FROM {self.dba.tables_prefix}{DBModels.TABLE_SENSOR_COMMANDS}
          WHERE mpID = {mpID} AND status = 1 /* queued */ AND collapsible = 1
          GROUP BY command_type
        ) t ON t.command_type = sc.command_type
        SET sc.status = 5 /* superseded */
        WHERE sc.mpID = {mpID}
          AND sc.status = 1 /* queued */
          AND sc.collapsible = 1
          AND sc.ID < t.max_id;
        """
        self.dba.execute(query)
    
    """
    -- Pick deterministically
    SELECT ID
    FROM hat_sensor_commands
    WHERE mpID = :mpID AND status = 1 /* queued */
    ORDER BY collapsible ASC, created_at ASC, ID ASC
    LIMIT 1
    FOR UPDATE;

    -- Claim
    UPDATE hat_sensor_commands
    SET status = 2 /* sending */, last_attempt_at = NOW(), attempts = attempts + 1
    WHERE ID = :cmd_id AND status = 1 /* queued */;
    """
    def pick_and_claim_next_command(self, mpID: int) -> Optional[Dict[str, Any]]:
        query = f"""
        -- Pick deterministically
        SELECT ID
        FROM {self.dba.tables_prefix}{DBModels.TABLE_SENSOR_COMMANDS}
        WHERE mpID = {mpID} AND status = {CMD_QUEUED}
        ORDER BY collapsible ASC, created_at ASC, ID ASC
        LIMIT 1
        FOR UPDATE;
        """
        cur = self.dba.execute(query)

        if cur.rowcount == 0:
            return None

        row = cur.fetchone()

        if row is None:
            return None

        cmd_id = row['ID']

        # Claim the command
        claim_query = f"""
        UPDATE {self.dba.tables_prefix}{DBModels.TABLE_SENSOR_COMMANDS}
        SET status = {CMD_SENDING}, last_attempt_at = NOW(), attempts = attempts + 1
        WHERE ID = {cmd_id} AND status = {CMD_QUEUED};
        """
        self.dba.execute(claim_query)

        return row
    