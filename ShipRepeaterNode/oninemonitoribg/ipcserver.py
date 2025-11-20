#!/usr/bin/env python3

from threading import Event, Thread
import http.server
import socketserver
from pprint import pprint
import json


class IPCRequestResult:
    def __init__(self, status = 200, content_type = "text/html", body = ""):
        self.Status = status
        self.ContentType = content_type
        self.Body = body
    
    def JSON(self, data = None):
        self.ContentType = "application/json"
        if not data is None:
            if isinstance(data, dict):
                self.Body = json.dumps(data)
            else:
                self.Body = json.dumps(data.__dict__)
        return self

    def BadRequest(self):
        self.Status = 400
        self.Body = "Bad Request"
        return self

    def Unauthorized(self):
        self.Status = 401
        self.Body = "Unauthorized"
        return self

    def Forbidden(self):
        self.Status = 403
        self.Body = "Unauthorized"
        return self

    def NotFound(self):
        self.Status = 404
        self.Body = "Not Found"
        return self


class IPCRequestBaseHandler:
    def get(self, path) -> IPCRequestResult:
        return IPCRequestResult().NotFound()

    def post(self, path, post_data) -> IPCRequestResult:
        return IPCRequestResult().NotFound()

    def json_post(self, path, post_data) -> IPCRequestResult:
        return IPCRequestResult().JSON()

class IPCHTTPHandler(http.server.BaseHTTPRequestHandler):
    def __init__(self, ipcRequestHandler: IPCRequestBaseHandler):
        self._ipcRequestHandler = ipcRequestHandler
        pass

    def __call__(self, *args, **kwargs):
        #""" Handle a request """
        super().__init__(*args, **kwargs)

    #def handle(self) -> None:
    #    pprint(self)
    #    return super().handle()

    def log_request(self, code='-', size='-'):
        pass

    def _set_response(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end_headers()

    def _json_response(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/json')
        self.end_headers()

    def do_GET(self):
        #logging.info("GET request,\nPath: %s\nHeaders:\n%s\n", str(self.path), str(self.headers))
        #self._set_response()
        #self.wfile.write("GET request for {}".format(self.path).encode('utf-8'))
        result = self._ipcRequestHandler.get(self.path)
        if result is None:
            self.send_response(401)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write("Not found".format().encode('utf-8'))
        else:
            self.send_response(result.Status)
            self.send_header('Content-type', result.ContentType)
            self.end_headers()
            self.wfile.write(result.Body.encode('utf-8'))

    def do_POST(self):
        content_length = 0
        content_type = None
        post_data = None
        if 'Content-Length' in self.headers: 
            content_length = int(self.headers['Content-Length']) # <--- Gets the size of data            
            post_data = self.rfile.read(content_length) # <--- Gets the data itself
        if 'Content-Type' in self.headers:
            content_type = self.headers['Content-Type']
        #logging.info("POST request,\nPath: %s\nHeaders:\n%s\n\nBody:\n%s\n",
        #        str(self.path), str(self.headers), post_data.decode('utf-8'))

        result = None
        if content_type == None:
            result = self._ipcRequestHandler.post(self.path, post_data)
        elif content_type == 'application/json':
            post_obj = None
            try:
                post_obj = json.loads(post_data)
            except json.JSONDecodeError:
                post_obj = None
            
            if post_obj is None:
                result = IPCRequestResult().BadRequest()
                result.Body += " : " + str(post_data)
            else:
                result = self._ipcRequestHandler.json_post(self.path, post_obj)
        else:
            result = IPCRequestResult().NotFound()
        
        if result is None:
            self.send_response(401)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write("Not found".format().encode('utf-8'))
        else:
            self.send_response(result.Status)
            self.send_header('Content-type', result.ContentType)
            self.end_headers()
            self.wfile.write(result.Body.encode('utf-8'))


class IPCServer(Thread):
    SERVICE_NAME = "HTTP API"
    def __init__(self, listen_addr, port, requestHandlerClass : IPCRequestBaseHandler):
        super(IPCServer, self).__init__()
        self._port = port
        self._listen_addr = listen_addr
        self._requestHandlerClass = requestHandlerClass
        self._ipcRequestHandler = self._requestHandlerClass()
        self._httpHandler = IPCHTTPHandler(self._ipcRequestHandler)
        #print("IPCServer listening at {0}:{1}".format(self._listen_addr, self._port))
        self._httpd = socketserver.TCPServer((self._listen_addr, self._port), self._httpHandler)

    def run(self):
        print("[{0}] started listening at {1}:{2}".format(IPCServer.SERVICE_NAME, self._listen_addr , self._port))
        self._httpd.serve_forever()
        print("[{0}] terminated".format(IPCServer.SERVICE_NAME))
    
    def stop(self):        
        self._httpd.shutdown()



