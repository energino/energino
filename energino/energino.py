#!/usr/bin/env python
#
# Copyright (c) 2013, Roberto Riggio
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
A command line utility for interfacing with the energino power 
consumption monitor.
"""

import optparse
import logging 
import sys
import serial
import glob
import math
import time
import datetime

DEFAULT_PORT = '/dev/ttyACM0'
DEFAULT_PORT_SPEED = 115200
DEFAULT_INTERVAL = 200
LOG_FORMAT = '%(asctime)-15s %(message)s'

def unpack_energino_v1(line):
    logging.debug("line: %s" % line.replace('\n',''))
    if type(line) is str and len(line) > 0 and line[0] == "#" and line[-1] == '\n':
        readings = line[1:-1].split(",")
        if len(readings) == 14:
            return { 'voltage' : float(readings[2]), 
                'current' : float(readings[3]), 
                'power' : float(readings[4]), 
                'switch' : int(readings[5]), 
                'window' : int(readings[6]), 
                'samples' : int(readings[7]), 
                'ip' : readings[8], 
                'server_port' : readings[9], 
                'host' : readings[10], 
                'host_port' : readings[11], 
                'feed' : readings[12], 
                'key' : readings[13]}
    raise Exception, "invalid line: %s" % line[0:-1]

MODELS = { "Energino" : { 1 : unpack_energino_v1 } }

class PyEnergino(object):

    def __init__(self, port=DEFAULT_PORT, bps=DEFAULT_PORT_SPEED, interval=DEFAULT_INTERVAL):
        self.interval = interval
        self.ser = serial.Serial(baudrate=bps, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, bytesize=serial.EIGHTBITS)
        devs = glob.glob(port + "*")
        for dev in devs:
            logging.debug("scanning %s" % dev)
            self.ser.port = dev
            self.ser.open()
            time.sleep(2)
            self.configure()
            self.send_cmds([ "#P%u" % self.interval ])
            logging.debug("attaching to port %s!" % dev)
            return
        raise Exception, "unable to configure serial port"

    def send_cmd(self, cmd):
        logging.debug("sending initialization sequence %s" % cmd)
        self.write(cmd + '\n')
        time.sleep(2)

    def send_cmds(self, cmds):
        for cmd in cmds:           
            self.send_cmd(cmd)
                
    def configure(self):
        for _ in range(0, 5):
            line = self.ser.readline()
            logging.debug("line: %s" % line.replace('\n',''))
            if type(line) is str and len(line) > 0 and line[0] == "#" and line[-1] == '\n':
                readings = line[1:-1].split(",")
                if readings[0] in MODELS.keys() and readings[1].isdigit() and int(readings[1]) in MODELS[readings[0]]:
                    logging.debug("found %s version %s" % (readings[0], readings[1]))
                    self.unpack = MODELS[readings[0]][int(readings[1])]
                    return
        raise Exception, "unable to identify model: %s" % line

    def write(self, value):
        self.ser.flushOutput()
        self.ser.write(value)

    def fetch(self, field = None):
        readings = self.unpack(self.ser.readline())
        readings['port'] = self.ser.port
        readings['at'] = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ")
        delta = math.fabs(self.interval - readings['window'])
        if delta / self.interval > 0.1:
            logging.debug("polling drift is higher than 10%%, target is %u actual is %u" % (self.interval, readings['window']))
        if field != None:
            return readings[field]
        return readings
                
def main():

    p = optparse.OptionParser()
    p.add_option('--port', '-p', dest="port", default=DEFAULT_PORT)
    p.add_option('--interval', '-i', dest="interval", default=DEFAULT_INTERVAL)
    p.add_option('--offset', '-o', dest="offset", default=None)
    p.add_option('--sensitivity', '-s', dest="sensitivity", default=None)
    p.add_option('--bps', '-b', dest="bps", default=DEFAULT_PORT_SPEED)
    p.add_option('--verbose', '-v', action="store_true", dest="verbose", default=False)    
    p.add_option('--log', '-l', dest="log")
    options, _ = p.parse_args()
    init = []
    if options.offset != None:
        init.append("#C%s" % options.offset)
    if options.sensitivity != None:
        init.append("#D%s" % options.sensitivity)
    if options.verbose:
        lvl = logging.DEBUG
    else:
        lvl = logging.INFO
    logging.basicConfig(level=lvl, format=LOG_FORMAT, filename=options.log, filemode='w')
    energino = PyEnergino(options.port, options.bps, int(options.interval))
    energino.send_cmds(init)
    while True:
        energino.ser.flushInput()
        try:
            readings = energino.fetch()
        except KeyboardInterrupt:
            logging.debug("Bye!")
            sys.exit()
        except:
            logging.debug("0 [V] 0 [A] 0 [W] 0 [samples] 0 [window]")
        else:
            logging.info("%s [V] %s [A] %s [W] %s [samples] %s [window]" % (readings['voltage'], readings['current'], readings['power'], readings['samples'], readings['window']))
    
if __name__ == "__main__":
    main()
    
