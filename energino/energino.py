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

from datetime import datetime

DEFAULT_DEVICE = '/dev/ttyACM'
DEFAULT_DEVICE_SPEED_BPS = 115200
DEFAULT_INTERVAL = 200
LOG_FORMAT = '%(asctime)-15s %(message)s'

def unpack_energino_v1(line):
    """ Unpack Energino status line. """

    logging.debug("line: %s", line.replace('\n',''))

    if ( type(line) is str
         and len(line) > 0
         and line[0] == "#"
         and line[-1] == '\n' ):

        readings = line[1:-1].split(",")
        if len(readings) == 8:
            return { 'voltage' : float(readings[2]),
                'current' : float(readings[3]),
                'power' : float(readings[4]),
                'switch' : int(readings[5]),
                'window' : int(readings[6]),
                'samples' : int(readings[7]) }

    raise ValueError("invalid line: %s" % line[0:-1])

def unpack_energino_ethernet_v1(line):
    """ Unpack Energino status line. """

    logging.debug("line: %s", line.replace('\n',''))

    if ( type(line) is str
         and len(line) > 0
         and line[0] == "#"
         and line[-1] == '\n' ):

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

    raise ValueError("invalid line: %s" % line[0:-1])

def unpack_energino_yun_v1(line):
    """ Unpack Energino YUN status line. """

    logging.debug("line: %s", line.replace('\n',''))

    if ( type(line) is str
         and len(line) > 0
         and line[0] == "#"
         and line[-1] == '\n' ):

        readings = line[1:-1].split(",")
        if len(readings) == 11:
            return { 'voltage' : float(readings[2]),
                'current' : float(readings[3]),
                'power' : float(readings[4]),
                'switch' : int(readings[5]),
                'window' : int(readings[6]),
                'samples' : int(readings[7]),
                'feed' : readings[8],
                'url' : readings[9],
                'key' : readings[10] }

    raise ValueError("invalid line: %s" % line[0:-1])

MODELS = { "Energino" : { 1 : unpack_energino_v1 },
           "EnerginoEthernet" : { 1 : unpack_energino_ethernet_v1 },
           "EnerginoYun" : { 1 : unpack_energino_yun_v1 } }

class PyEnergino(object):
    """ Energino class. """

    def __init__(self,
                 port=DEFAULT_DEVICE,
                 bps=DEFAULT_DEVICE_SPEED_BPS,
                 interval=DEFAULT_INTERVAL):

        self.unpack = None
        self.interval = interval
        self.ser = serial.Serial(baudrate=bps,
                                 parity=serial.PARITY_NONE,
                                 stopbits=serial.STOPBITS_ONE,
                                 bytesize=serial.EIGHTBITS)

        devs = glob.glob(port + "*")

        for dev in devs:
            logging.debug("scanning %s", dev)
            self.ser.port = dev
            self.ser.open()
            time.sleep(2)
            self.configure()
            self.send_cmds([ "#P%u" % self.interval ])
            logging.debug("attaching to port %s!", dev)
            return

        raise RuntimeError("unable to configure serial port")

    def send_cmd(self, cmd):
        """ Send command to serial port. """

        logging.debug("sending initialization sequence %s", cmd)
        self.write(cmd + '\n')
        time.sleep(2)

    def send_cmds(self, cmds):
        """ Send command list serial port. """

        for cmd in cmds:
            self.send_cmd(cmd)

    def configure(self):
        """ Attempt to configure Energino. """

        for _ in range(0, 5):
            line = self.ser.readline()
            logging.debug("line: %s", line.replace('\n',''))

            if ( type(line) is str
                 and len(line) > 0
                 and line[0] == "#"
                 and line[-1] == '\n' ):

                readings = line[1:-1].split(",")

                if ( readings[0] in MODELS.keys()
                     and readings[1].isdigit()
                     and int(readings[1]) in MODELS[readings[0]] ):

                    logging.debug("found %s version %s", readings[0],
                                                         readings[1] )

                    self.unpack = MODELS[readings[0]][int(readings[1])]
                    return

        raise RuntimeError("unable to identify model: %s" % line)

    def write(self, value):
        """ Write to serial port and flush. """

        self.ser.flushOutput()
        self.ser.write(value)

    def fetch(self, field = None):
        """ Read from serial port. """

        readings = self.unpack(self.ser.readline())
        readings['port'] = self.ser.port
        readings['at'] = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ")
        delta = math.fabs(self.interval - readings['window'])

        if delta / self.interval > 0.1:
            logging.debug("Target polling %u actual %u", self.interval,
                                                         readings['window'] )

        if field != None:
            return readings[field]

        return readings

def main():
    """ Launcher method. """

    parser = optparse.OptionParser()

    parser.add_option('--port', '-p', dest="port", default=DEFAULT_DEVICE)
    parser.add_option('--interval', '-i',
                      dest="interval",
                      type="int",
                      default=DEFAULT_INTERVAL)

    parser.add_option('--offset', '-o',
                      dest="offset",
                      type="int",
                      default=None)

    parser.add_option('--sensitivity', '-s',
                      dest="sensitivity",
                      type="int",
                      default=None)

    parser.add_option('--bps', '-b',
                      dest="bps",
                      type="int",
                      default=DEFAULT_DEVICE_SPEED_BPS)

    parser.add_option('--matlab', '-t', dest="matlab")

    parser.add_option('--verbose', '-v',
                      action="store_true",
                      dest="verbose",
                      default=False)

    parser.add_option('--log', '-l', dest="log")

    options, _ = parser.parse_args()
    init = []

    if options.offset != None:
        init.append("#C%u" % options.offset)
    if options.sensitivity != None:
        init.append("#D%u" % options.sensitivity)

    if options.verbose:
        lvl = logging.DEBUG
    else:
        lvl = logging.INFO

    logging.basicConfig(level=lvl,
                        format=LOG_FORMAT,
                        filename=options.log,
                        filemode='w')

    energino = PyEnergino(options.port, options.bps, options.interval)
    energino.send_cmds(init)

    if options.matlab != None:
        mat = []

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

            line = ( readings['voltage'],
                     readings['current'],
                     readings['power'],
                     readings['samples'],
                     readings['window'] )

            if options.matlab != None:
                mat.append(line)

            log = "%s [V] %s [A] %s [W] %s [samples] %s [window]" % line

            logging.info(log)

        if options.matlab != None:
            import numpy as np
            import scipy.io
            scipy.io.savemat(options.matlab,
                             { 'READINGS' : np.array(mat) },
                             oned_as = 'column')

if __name__ == "__main__":
    main()
