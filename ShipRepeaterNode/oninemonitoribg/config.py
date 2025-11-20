import sys
import os
sys.path.insert(1, os.path.realpath(os.path.join(os.path.dirname(__file__), './config')))

from hostid import HostID
from pprint import pprint
import os
import configparser

class Config:

    def __init__(self, configdir):
        self._inifilename = "config.{0}.ini".format(HostID.HOST)
        configdir_path = os.path.realpath(configdir)
        self._inifile = os.path.join(configdir_path, self._inifilename)
        
        if not os.path.isfile(self._inifile):
            return
        
        self._optional_enforced_types = {
            'General': {
                'systemid': int,
                'dev_debug': bool,
                'tablet_mode': bool,

                'sensorsdaemon_http_api_listen_port': int,
                'sensorsdaemon_wait_for_commands_seconds': int,
            }
        }

        self._enforced_types = {
            'General': {
                'shipid': int,

                'measurements_grace_seconds': int,
                'complete_batch_grace_seconds': int,
                'system_sensors_alert_minutes': int,

                'fs': float,
                
                #'http_api_listen_port': int,
                #'pool_workers': int,
            },
            'Database': {
                
            }
        }

        self._loadConfig()
    
    def _validateConfig(self):
        for sectionTag in self._enforced_types:            
            if sectionTag not in self._Config:
                sys.exit("[{0}] section not found in {1}".format(sectionTag, self._inifilename))
        
    def _loadConfig(self):
        self._Config = configparser.ConfigParser()
        self._Config.read(self._inifile, "utf8")

        self._validateConfig()
                
        for sectionTag in self._enforced_types:
            setattr(self, sectionTag, dict(self._Config[sectionTag]))
            #required fields
            for fieldName in self._enforced_types[sectionTag]:
                if fieldName not in self._Config[sectionTag]:
                    sys.exit("enforced type filed '{0}' not found in section [{1}] (in config file {2})".format(fieldName, sectionTag, self._inifilename))
                fieldType = self._enforced_types[sectionTag][fieldName]

                value = getattr(self, sectionTag)[fieldName]
                if fieldType is bool:
                    # Convert string to bool properly
                    value = value.lower() in ('true', '1', 'yes', 'on')
                else:
                    value = fieldType(value)
                getattr(self, sectionTag)[fieldName] = value
            
            #optional fields
            if sectionTag not in self._optional_enforced_types:
                continue
            for fieldName in self._optional_enforced_types[sectionTag]:
                if fieldName not in self._Config[sectionTag]:
                    continue
                fieldType = self._optional_enforced_types[sectionTag][fieldName]

                value = getattr(self, sectionTag)[fieldName]
                if fieldType is bool:
                    # Convert string to bool properly
                    value = value.lower() in ('true', '1', 'yes', 'on')
                else:
                    value = fieldType(value)
                getattr(self, sectionTag)[fieldName] = value

        #if not 'preferred_language' in self.General, add it
        if not 'preferred_language' in self.General:
            self.General['preferred_language'] = 'en'