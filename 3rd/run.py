import requests
import socket
import base64
from http.server import HTTPServer, BaseHTTPRequestHandler


class Request(BaseHTTPRequestHandler):
    timeout = 5
    server_version = 'Apache'

    def do_GET(self):
        self.send_response(200)
        # self.send_header("type", "get")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        url = 'http://192.168.99.151/Snapshot/1/RemoteImageCapture?ImageFormat=2?'
        resp = requests.get(url, auth=('admin', 'LLY20001210'))

        self.wfile.write(base64.b64encode(resp.content))


def getMyIp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        print('running on ' + ip + ':80')
    finally:
        s.close()


host = ('0.0.0.0', 80)
server = HTTPServer(host, Request)
getMyIp()
server.serve_forever()
