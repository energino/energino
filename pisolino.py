#!/usr/bin/env python
#
# Copyright (c) 2012, Roberto Riggio
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the CREATE-NET nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY CREATE-NET ''AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL CREATE-NET BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""
A system daemon using the Dispatcher base class
"""

import os
import time
import signal
import logging
import uuid
import sys
import optparse
import json 

from SimpleHTTPServer import SimpleHTTPRequestHandler
from SocketServer import TCPServer
from SocketServer import ThreadingMixIn
from dispatcher import Dispatcher, BACKOFF
from energino import PyEnergino

DEFAULT_SERVER_PORT=8180
DEFAULT_CONFIG = '/etc/pachubino.conf'

LOG_FORMAT = '%(asctime)-15s %(message)s'

class ListenerHandler(SimpleHTTPRequestHandler):
    
    def do_GET(self):
        tokens = self.path.strip("/").split("/")
        if len(tokens) == 2 and tokens[0] == 'ap':
            if tokens[1] == 'clients':
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps( [ ap.getClients() ] ) + '\n')
            elif tokens[1] ==  'duty_cycle':
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps( [ ap.getDutyCycle() ] ) + '\n')
            else:
                self.send_error(404)
        else:
            self.send_error(404)

    def do_PUT(self):
        tokens = self.path.strip("/").split("/")
        try:
            if len(tokens) == 3 and tokens[0] == 'ap':
                if tokens[1] ==  'duty_cycle':
                    duty_cycle = int(tokens[2])
                    ap.setDutyCycle(duty_cycle)
                    self.send_response(200)
                else:
                    self.send_error(404)
            else:
                self.send_error(404)
        except Exception:
            logging.exception("Exception:")
            self.send_error(500)
                                            
class Listener(ThreadingMixIn, TCPServer):
    def __init__(self, port = DEFAULT_SERVER_PORT):
        self.allow_reuse_address = True
        logging.info("RESTful interface listening on port: %u" % port)
        TCPServer.__init__(self, ("", port), ListenerHandler)

class AccessPoint(object):
    
    def __init__(self):
        self.path='/sys/kernel/debug/ieee80211/phy0/netdev:wlan0/stations'
        self.duty_cycle=100

    def getClients(self):
        return len(os.listdir(self.path))
    
    def getDutyCycle(self):
        return self.duty_cycle

    def setDutyCycle(self, duty_cycle):
        if len(os.listdir(self.path)) == 0:
            self.duty_cycle=duty_cycle
                
class Pisolino(Dispatcher):
        
    def run(self):
        # start dispatcher
        self.dispatcher.add_stream("clients", "derivedSI", "Clients", "#")
        self.dispatcher.add_stream("duty_cycle", "derivedSI", "Duty Cycle", "%")
        self.dispatcher.start()
        # start pisolino
        logging.info("starting up pisolino")
        while True:
            try:
                # configure feed
                self.discover()
                # setup serial port
                energino = PyEnergino(self.device, self.bps, self.interval)
                energino.send_cmds([ "#F%s" % self.feed ])                
                # start updating
                logging.info("begin polling")
                while True:
                    readings = energino.fetch()
                    readings['clients'] = ap.getClients()
                    readings['duty_cycle'] = ap.getDutyCycle()
                    logging.debug("appending new readings: %s [C]" % (readings['clients']))
                    self.dispatcher.enqueue(readings)
            except Exception:
                logging.exception("exception, backing off for %u seconds" % BACKOFF)
                time.sleep(BACKOFF)
        # thread stopped
        logging.info("thread %s stopped" % self.__class__.__name__) 

       
def sigint_handler(signal, frame):
    pisolino.shutdown()
    sys.exit(0)

if __name__ == "__main__":

    p = optparse.OptionParser()
    p.add_option('--uuid', '-u', dest="uuid", default=uuid.getnode())
    p.add_option('--config', '-c', dest="config", default=DEFAULT_CONFIG)
    p.add_option('--log', '-l', dest="log")
    p.add_option('--debug', '-d', action="store_true", dest="debug", default=False)       
    options, arguments = p.parse_args()

    if options.debug:
        lvl = logging.DEBUG
    else:
        lvl = logging.INFO

    if options.log != None:
        logging.basicConfig(level=lvl, format=LOG_FORMAT, filename=options.log, filemode='w')
    else:
        logging.basicConfig(level=lvl, format=LOG_FORMAT)

    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)

    ap = AccessPoint()

    pisolino = Pisolino(options.uuid, options.config)
    pisolino.start()    

    listener = Listener()

    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)
        
    listener.serve_forever()
    