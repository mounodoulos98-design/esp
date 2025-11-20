#mysql imports
#import mysql.connector

#sqlite3 imports
#import sqlite3

#from mysql.connector import errorcode
#from pprint import pprint
import datetime
from decimal import Decimal
import sys

class DBObject(object):
    def __init__(self, fieldsmap, row):

        for (attr, idx) in fieldsmap.items():
            setattr(self, attr, row[idx])

    def __str__(self):
        attrs = vars(self)
        return '[DBObject]\n'+'\n'.join("* %s: %s" % item for item in attrs.items())
    
    def __repr__(self) -> str:
        #return super().__repr__()        
        attrs = vars(self)
        return '[DBObject: '+','.join("%s: %s" % item for item in attrs.items()) + ']'

    def batchToUploadDict(self):
        attrs = vars(self)
        dict = {k:v for k,v in attrs.items() if k not in [
            'retrieved_on', 'key', 'status', 'diagstatus',
            'descriptors_json', 'diag_uploaded_flag', 'message'
            ]}
        #convert the datetime 'recorded_on' to PHP style
        dict['recorded_on'] = dict['recorded_on'].strftime('%Y-%m-%d %H:%M:%S')
        return dict

    def toDict(self):
        attrs = vars(self)
        result = {}
        for k, v in attrs.items():
            if isinstance(v, datetime.datetime):
                result[k] = v.strftime('%Y-%m-%d %H:%M:%S')
            #Decimal
            elif isinstance(v, Decimal):
                result[k] = float(v)
            else:
                result[k] = v
        return result
        

class DBAdapter:
    def __init__(self, dbconfig):
        self.dbconfig = dbconfig
        self.host = self.dbconfig['host']
        self.database = self.dbconfig['database']
        self.username = self.dbconfig['username']
        self.password = self.dbconfig['password']
        self.tables_prefix = self.dbconfig['tables_prefix']
        self.dbsystem = self.dbconfig.get('dbsystem', 'mysql')
        #print(f"DBAdapter initialized: {self.host}, {self.database}, {self.username}, {self.tables_prefix}, {self.dbsystem}")
        if self.dbsystem not in ['mysql', 'sqlite3']:
            raise ValueError(f"Unsupported dbsystem: {self.dbsystem}")        
        return

    def open(self):
        conn = None

        if self.dbsystem == 'sqlite3':
            import sqlite3
        elif self.dbsystem == 'mysql':
            import mysql.connector

        conn_config = {
        'user': self.username,
        'password': self.password,
        'host': self.host,
        'database': self.database,
        'raise_on_warnings': True
        }

        try:
            if self.dbsystem == 'sqlite3':
                conn = sqlite3.connect(self.database)

                self.createTablesIfNotExists(conn)
            elif self.dbsystem == 'mysql':
                conn = mysql.connector.connect(**conn_config)
        except Exception as e:
            print(f"Error opening DB connection: {e}")
            sys.exit(1)
            conn = None
        
        #print(f"DBAdapter opened connection: {conn}")
        return conn

    def createTablesIfNotExists(self, conn):
        #check if table 'hat_batches' exists
        cur = conn.cursor()
        cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='hat_batches'")
        if not cur.fetchone():
            #print("Creating table 'hat_batches' as it does not exist.")
            """
            This is the equivalent MySQL statement:
CREATE TABLE `hat_batches` (
  `ID` bigint(19) NOT NULL,
  `retrieved_on` timestamp NOT NULL DEFAULT utc_timestamp(),
  `recorded_on` datetime DEFAULT NULL,
  `key` varchar(512) NOT NULL,
  `status` smallint(6) NOT NULL DEFAULT 0 COMMENT '0: Pending Analysis, 1: Analysis Done',
  `csvfile` varchar(512) DEFAULT NULL,
  `systemID` int(11) NOT NULL DEFAULT 0,
  `message` text DEFAULT NULL,
  `subsystemID` int(11) NOT NULL DEFAULT 0,
  `mpID` int(11) NOT NULL DEFAULT 0,
  `descriptors_json` text DEFAULT NULL,
  `rating_level` int(11) NOT NULL DEFAULT 0,
  `findings` text DEFAULT NULL,
  `recomendations` text DEFAULT NULL,
  `diagstatus` int(11) NOT NULL DEFAULT 0,
  `diagnostics_json` text DEFAULT NULL,
  `diag_uploaded_flag` smallint(6) DEFAULT 0,
  `modbus_flag` smallint(6) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE `hat_batches`
  ADD PRIMARY KEY (`ID`),
  ADD KEY `retrieved_on` (`retrieved_on`),
  ADD KEY `recorded_on` (`recorded_on`),
  ADD KEY `batch_id` (`key`),
  ADD KEY `status` (`status`),
  ADD KEY `idx_status_ratinglevel_mp_recordedon` (`status`,`rating_level`,`mpID`,`recorded_on`),
  ADD KEY `idx_status_diagstatus_ratinglevel_mp_recordedon` (`status`,`diagstatus`,`rating_level`,`mpID`,`recorded_on`),
  ADD KEY `idx_systemID` (`systemID`),
  ADD KEY `ix_batches_mp_retrieved` (`mpID`,`retrieved_on`);

ALTER TABLE `hat_batches`
  MODIFY `ID` bigint(19) NOT NULL AUTO_INCREMENT;

We need the equivalent SQLite statement:
            """
            sqlite_create_table_query = """
CREATE TABLE hat_batches (
    ID INTEGER PRIMARY KEY AUTOINCREMENT,
    retrieved_on TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    recorded_on DATETIME DEFAULT NULL,
    key TEXT NOT NULL,
    status INTEGER NOT NULL DEFAULT 0,
    csvfile TEXT DEFAULT NULL,
    systemID INTEGER NOT NULL DEFAULT 0,
    message TEXT DEFAULT NULL,
    subsystemID INTEGER NOT NULL DEFAULT 0,
    mpID INTEGER NOT NULL DEFAULT 0,
    descriptors_json TEXT DEFAULT NULL,
    rating_level INTEGER NOT NULL DEFAULT 0,
    findings TEXT DEFAULT NULL,
    recomendations TEXT DEFAULT NULL,
    diagstatus INTEGER NOT NULL DEFAULT 0,
    diagnostics_json TEXT DEFAULT NULL,
    diag_uploaded_flag INTEGER DEFAULT 0,
    modbus_flag INTEGER DEFAULT 0
);
            """

            cur.execute(sqlite_create_table_query)
        
        #check if table 'hat_measurements' exists
        cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='hat_measurements'")
        if not cur.fetchone():
            #print("Creating table 'hat_measurements' as it does not exist.")
            """
            This is the equivalent MySQL statement:
CREATE TABLE `hat_measurements` (
  `ID` bigint(20) NOT NULL,
  `batch_key` varchar(512) NOT NULL,
  `recorded_on` datetime DEFAULT NULL,
  `systemID` bigint(20) NOT NULL,
  `mpID` bigint(20) NOT NULL,
  `axisID` bigint(20) NOT NULL,
  `descriptorID` bigint(20) NOT NULL,
  `notinoperation` smallint(6) NOT NULL DEFAULT 0,
  `value` double NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE `hat_measurements`
  ADD PRIMARY KEY (`ID`),
  ADD KEY `batch_key` (`batch_key`),
  ADD KEY `recorded_on` (`recorded_on`),
  ADD KEY `mpID` (`mpID`),
  ADD KEY `axisID` (`axisID`),
  ADD KEY `descriptorID` (`descriptorID`);

ALTER TABLE `hat_measurements`
  MODIFY `ID` bigint(20) NOT NULL AUTO_INCREMENT;
            """
            sqlite_create_table_query = """
CREATE TABLE hat_measurements (
    ID INTEGER PRIMARY KEY AUTOINCREMENT,
    batch_key TEXT NOT NULL,
    recorded_on DATETIME DEFAULT NULL,
    systemID INTEGER NOT NULL,
    mpID INTEGER NOT NULL,
    axisID INTEGER NOT NULL,
    descriptorID INTEGER NOT NULL,
    notinoperation INTEGER NOT NULL DEFAULT 0,
    value REAL NOT NULL
);
            """

            cur.execute(sqlite_create_table_query)
        
        cur.close()
        conn.commit()

    def execute(self, query, args = None, conn = None):

        connectionIsGiven = (conn != None)
        if not connectionIsGiven: conn = self.open()

        if self.dbsystem == 'sqlite3':
            cur = conn.cursor()
        elif self.dbsystem == 'mysql':
            cur = conn.cursor(buffered=True)

        try:
            if args is None:
                cur.execute(query)
            else:
                cur.execute(query, args)
        except Exception as e:
            print("ERROR: "+str(e))
            #return None

        if not connectionIsGiven: conn.close()

        return cur

    def fieldsmap(self, cursor):
        map = {}
        column = 0
        # For MySQL, use column_names; for sqlite3, use description
        if hasattr(cursor, 'column_names'):
            for col_name in cursor.column_names:
                map[col_name] = column
                column += 1
        elif hasattr(cursor, 'description'):
            for desc in cursor.description:
                map[desc[0]] = column
                column += 1
        return map
    
    def deleteRecord(self, tablename, where = None, order = None, conn = None):
        connectionIsGiven = (conn != None)
        if not connectionIsGiven: conn = self.open()

        query = "DELETE from `"+self.tables_prefix+tablename+"`"

        if where != None:
            query += " WHERE "+where

        cur = self.execute(query, None, conn)

        if cur.rowcount > 0:
            conn.commit()  

        if not connectionIsGiven: conn.close()


    def getDBObject(self, tablename, where = None, order = None, conn = None):
        obj = None

        connectionIsGiven = (conn != None)
        if not connectionIsGiven: conn = self.open()
        #print("connectionIsGiven="+str(connectionIsGiven))

        query = "SELECT * from `"+self.tables_prefix+tablename+"`"

        if where != None:
            query += " WHERE "+where

        if order != None:
            query += " ORDER BY "+order

        query += " LIMIT 1"

        #print(f"Executing query: {query}")
        cur = self.execute(query, None, conn)

        if self.dbsystem == 'sqlite3':
            #in sqlite3, the cursor does not have a rowcount
            #we should check if the cursor has any rows
            row = cur.fetchone()
            if row is not None:
                #print(f"Row fetched: {row}")
                fm = self.fieldsmap(cur)
                #print(f"Field map: {fm}")
                obj = DBObject(fm, row)
                #print(f"DBObject created: {obj}")
        elif self.dbsystem == 'mysql':
            if cur.rowcount > 0:
                fm = self.fieldsmap(cur)            
                row = cur.fetchone()
                obj = DBObject(fm, row)

        if not connectionIsGiven: conn.close()

        return obj

    def getDBObjects(self, tablename, where = None, order = None, limit = None, conn = None):
        objects = None

        connectionIsGiven = (conn != None)
        if not connectionIsGiven: conn = self.open()
        #print("connectionIsGiven="+str(connectionIsGiven))

        query = "SELECT * from `"+self.tables_prefix+tablename+"`"

        if where != None:
            query += " WHERE "+where

        if order != None:
            query += " ORDER BY "+order
        
        if limit != None:
            query += " LIMIT {0}".format(limit)
                
        #print(f"getDBObjects: executing query: {query}")
        cur = self.execute(query, None, conn)

        if self.dbsystem == 'sqlite3':
            #in sqlite3, the cursor does not have a rowcount
            #we should check if the cursor has any rows
            rows = cur.fetchall()
            if rows:
                fm = self.fieldsmap(cur)
                objects = [DBObject(fm, row) for row in rows]

        elif self.dbsystem == 'mysql':
            if cur.rowcount > 0:
                fm = self.fieldsmap(cur)            
                rows = cur.fetchall()
                objects = []
                for row in rows:                
                    obj = DBObject(fm, row)                
                    objects.append(obj)

        if not connectionIsGiven: conn.close()

        return objects

    def insertDict(self, tablename, valuesdict, conn = None, idField = "ID"):
        lastrowid = None

        connectionIsGiven = (conn != None)
        if not connectionIsGiven: conn = self.open()
        #print("connectionIsGiven="+str(connectionIsGiven))

        query = "INSERT INTO `"+self.tables_prefix+tablename+"` ("
        valsstr = ""
        idx = 0
        for fldname in valuesdict.keys():
            if idx>0:
                query += ", "
                valsstr += ", "
            query += "`"+fldname+"`"
            if self.dbsystem == 'sqlite3':
                valsstr += "?"+str(idx+1)
            elif self.dbsystem == 'mysql':
                valsstr += "%("+fldname+")s"
            idx+=1
        query += ") VALUES ("+valsstr+")"

        if self.dbsystem == 'sqlite3':
            #map the named values to indexed values
            insertDict = {}
            idx = 0
            for fldname in valuesdict.keys():
                insertDict[str(idx+1)] = valuesdict[fldname]
                idx += 1
            
        elif self.dbsystem == 'mysql':
            insertDict = valuesdict

        #print(query)
        #print(f"Insert query: {query}")
        #print(f"Values: {insertDict}")

        cur = self.execute(query, insertDict, conn)

        if cur.rowcount > 0:
            conn.commit()            
            lastrowid = cur.lastrowid 

        #cur = conn.cursor()
        #cur.executemany("""INSERT INTO bar(first_name,last_name) VALUES (%(first_name)s, %(last_name)s)""", namedict)

        if not connectionIsGiven: conn.close()
        return lastrowid

    def updateDict(self, tablename, valuesdict, idValue, idField = "ID", conn = None):
        lastrowid = None

        connectionIsGiven = (conn != None)
        if not connectionIsGiven: conn = self.open()
        #print("connectionIsGiven="+str(connectionIsGiven))

        query = "UPDATE `"+self.tables_prefix+tablename+"` SET "        
        idx = 0
        for fldname in valuesdict.keys():
            if idx>0:
                query += ", "
            if self.dbsystem == 'sqlite3':
                query += "`{0}`=?{1}".format(fldname, idx+1)
            elif self.dbsystem == 'mysql':
                query += "`{0}`=%({1})s".format(fldname, fldname)
            idx+=1
        
        # if idValue is list, then we have to use IN
        if isinstance(idValue, list):
            idValuesList = ','.join(str(x) for x in idValue)
            query += " WHERE `{0}` IN ({1})".format(idField, idValuesList)
        else:
            if self.dbsystem == 'sqlite3':
                query += " WHERE `{0}`=?{1}".format(idField, idx+1)
            elif self.dbsystem == 'mysql':
                query += " WHERE `{0}`=%({1})s".format(idField, idField)
            valuesdict[idField] = idValue
        
        if self.dbsystem == 'sqlite3':
            #map the named values to indexed values
            insertDict = {}
            idx = 0
            for fldname in valuesdict.keys():
                insertDict[str(idx+1)] = valuesdict[fldname]
                idx += 1
            
        elif self.dbsystem == 'mysql':
            insertDict = valuesdict

        #print(query)
        #print(insertDict)
        
        cur = self.execute(query, insertDict, conn)

        if cur.rowcount > 0:
            conn.commit()

        if not connectionIsGiven: conn.close()        
