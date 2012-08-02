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
import threading

from subprocess import Popen, PIPE
from SimpleHTTPServer import SimpleHTTPRequestHandler
from SocketServer import TCPServer
from SocketServer import ThreadingMixIn
from dispatcher import Dispatcher, BACKOFF
from energino import PyEnergino

DEFAULT_SERVER_PORT=8180
DEFAULT_CONFIG = '/etc/pachubino.conf'

DEFAULT_DUTY_CYCLE = 50
DEFAULT_DUTY_CYCLE_WINDOW = 30

STATIONS_PATH = '/sys/kernel/debug/ieee80211/phy0/netdev:wlan0/stations'

MASTER_IFACE="wlan0"
MONITOR_IFACE="wlan0-1"

IFACE_MODE_UP="up"
IFACE_MODE_DOWN="down"

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
                self.wfile.write(json.dumps( [ ap.getDutyCycle(), ap.getDutyCycleWindow() ] ) + '\n')
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
                    ap.setDutyCycle(duty_cycle, ap.getDutyCycleWindow())
                    self.send_response(200)
                    self.send_header('Content-type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps( [ ap.getDutyCycle(), ap.getDutyCycleWindow() ] ) + '\n')
                elif tokens[1] ==  'duty_cycle_window':
                    duty_cycle_window = int(tokens[2])
                    ap.setDutyCycle(ap.getDutyCycle, duty_cycle_window)
                    self.send_response(200)
                    self.send_header('Content-type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps( [ ap.getDutyCycle(), ap.getDutyCycleWindow() ] ) + '\n')
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


class Command(object):

    def __init__(self, cmd):
        self.cmd = cmd

    def run(self):
        
        self.process = Popen(self.cmd, shell=False, stdout=PIPE, stderr=PIPE, close_fds=True)
        (self.stdout, self.stderr) = self.process.communicate()

        return self.process.returncode   

class AccessPoint(threading.Thread):
    
    def __init__(self):
        super(AccessPoint, self).__init__()
        self.daemon = True
        self.stop = threading.Event()        
        self.setDutyCycle(DEFAULT_DUTY_CYCLE, DEFAULT_DUTY_CYCLE_WINDOW)
        self.isDown = False

    def getClients(self):
        return len(os.listdir(STATIONS_PATH))
    
    def getDutyCycle(self):
        return self.duty_cycle

    def getDutyCycleWindow(self):
        return self.duty_cycle_window

    def setDutyCycle(self, duty_cycle, duty_cycle_window):
        # setting duty cycle
        if duty_cycle < 0 or duty_cycle > 100:
            self.duty_cycle = DEFAULT_DUTY_CYCLE
        else:
            self.duty_cycle = duty_cycle
        # setting duty cycle window
        if duty_cycle_window < 30 or duty_cycle_window > 300:
            self.duty_cycle_window = DEFAULT_DUTY_CYCLE_WINDOW
        else:
            self.duty_cycle_window = duty_cycle_window
        # computing intervals
        self.up_interval = int(self.duty_cycle_window * self.duty_cycle / 100)
        self.down_interval = int(self.duty_cycle_window * (100 - self.duty_cycle) / 100)
        logging.info("access point up interval set to %us" % self.up_interval)
        logging.info("access point down interval set to %us" % self.down_interval)

    def ifconfig(self, iface, mode):
        cmd = Command(["/sbin/ifconfig", iface, mode])
        ret = cmd.run()
        if ret is None or ret != 0:
            logging.warning("unable to execute command: %s" % " ".join(cmd.cmd))

    def ifdown(self):
        if self.getClients() == 0 and not self.isDown:
            logging.info("bringing down interfaces")
            self.isDown = True
            self.ifconfig(MASTER_IFACE, IFACE_MODE_DOWN)
            self.ifconfig(MONITOR_IFACE, IFACE_MODE_DOWN)

    def ifup(self):
        if self.getClients() == 0 and self.isDown:
            logging.info("bringing up interfaces")
            self.isDown = False
            self.ifconfig(MASTER_IFACE, IFACE_MODE_UP)
            self.ifconfig(MONITOR_IFACE, IFACE_MODE_UP)

    def shutdown(self):
        logging.info("shutting down pisolino")
        self.stop.set()

    def run(self):
        while True:
            if self.up_interval > 0:
                logging.info("up interval (%us), number of associated stations %u" % (self.up_interval, self.getClients()))
                self.ifup()
                time.sleep(self.up_interval)
            if self.down_interval > 0:
                logging.info("down interval (%us), number of associated stations %u" % (self.up_interval, self.getClients()))
                self.ifdown()
                time.sleep(self.down_interval)
                
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
                energino.send_cmds([ "#F%s" % self.feed, "#H%s" % self.host, "#S%s" % self.port  ])                
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
    ap.shutdown()
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
    ap.start()

    pisolino = Pisolino(options.uuid, options.config)
    pisolino.start()    

    listener = Listener()

    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)
        
    listener.serve_forever()
    