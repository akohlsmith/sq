#!/usr/bin/python3

import http.server
import argparse
from urllib.parse import urlparse, parse_qs
from pathlib import Path
from socketserver import ThreadingMixIn
import json
import time
import errno

class camhandler(http.server.BaseHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin:', '*')
        http.server.SimpleHTTPRequestHandler.end_headers(self)

    def do_GET(self):
        pr = urlparse(self.path)
        pf = pr.path.split('/')

        if pf[-1] == 'ssupdates.html':
                self.send_response(200)
                self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
                self.end_headers()

                running=True
                while running:
                    with open('/sys/class/thermal/thermal_zone0/temp') as cput:
                        tstr = int(cput.readline().strip())/1000
                    datats = "["
                    datats += "[" + json.dumps(tstr) + "," + json.dumps(tstr/2) + "," + json.dumps(tstr*2/3) + "," + json.dumps(100-tstr) + "," + json.dumps(100-tstr/3) + "],"
                    datats += "[" + json.dumps(tstr/3) + "," + json.dumps(tstr) + "," + json.dumps(100-tstr*2/3) + "," + json.dumps(tstr/2) + "," + json.dumps(100-tstr/2) + "],"
                    datats += "[" + json.dumps(tstr+.5*tstr) + "," + json.dumps(100-tstr/2) + "," + json.dumps(tstr) + "," + json.dumps(50+tstr) + "," + json.dumps(tstr/3) + "]"
                    datats += "]"
                    print(datats)

                    try:
                        self.wfile.write(('data: %s\n\n' % datats).encode('utf-8'))
                    except Exception as e:
                        running=False
                        if e.errno!=errno.EPIPE:
                            raise

                    time.sleep(1.0)

        elif pf[-1] == '' or pf[-1] == 'index.html':
            print("send index")
            sfp=Path('index.html')
            with sfp.open('r') as sfile:
                xs=sfile.read()
                self.simpleSend(xs)

        elif pf[-1] == 'smoothie.js':
            print("send smoothie.js")
            sfp=Path('smoothie.js')
            with sfp.open('rb') as sfile:
                xs=sfile.read()
            self.send_response(200)
            self.send_header('Content-Type', 'text/javascript')
            self.end_headers()
            self.wfile.write(xs)

        else:
            print("what is", pf[-1])
            print(pf)

    def simpleSend(self, thcontent):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.end_headers()
        self.wfile.write(thcontent.encode('utf-8'))


class ThreadedHTTPServer(ThreadingMixIn, http.server.HTTPServer):
    """Handle requests in a separate thread."""

def findMyIp():
    """
    A noddy function to find local machines' IP address for simple cases....
    based on info from https://stackoverflow.com/questions/166506/finding-local-ip-addresses-using-pythons-stdlib
    
    returns an array of IP addresses (in simple cases there will only be one entry)
    """
    import socket
    return([ip for ip in socket.gethostbyname_ex(socket.gethostname())[2] if not ip.startswith("127.")] or 
            [[(s.connect(("8.8.8.8", 53)), s.getsockname()[0], s.close()) for s in [socket.socket(socket.AF_INET, socket.SOCK_DGRAM)]][0][1]]
          )

DEFWEBPORT      = 8088

if __name__ == '__main__':
    import sys, os, pathlib
    clparse = argparse.ArgumentParser(description='runs a simple webserver to chart the cpu temperature. ')
    clparse.add_argument( "-w", "--webport", type=int, default=DEFWEBPORT,
        help="port used for the webserver, default %d" % DEFWEBPORT)
    args=clparse.parse_args()
    webport = args.webport
    server = ThreadedHTTPServer(('',webport),camhandler)
    ips=findMyIp()
    if len(ips)==0:
        print('starting webserver on internal IP only (no external IP addresses found)')
    elif len(ips)==1:
        print('Starting webserver on %s:%d' % (ips[0],webport))
    else:
        print('Starting webserver on multiple ip addresses (%s), port:%d' % (str(ips),webport))
    try:
        server.serve_forever()
        print('webserver shut down')
    except KeyboardInterrupt:
        server.socket.close()
