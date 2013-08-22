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
A system daemon interfacing energino with Xively
"""

import signal
import logging
import sys
import optparse

from energino import PyEnergino, DEFAULT_INTERVAL, DEFAULT_PORT, DEFAULT_PORT_SPEED
from xively_dispatcher import XivelyDispatcher, DEFAULT_CONFIG, LOG_FORMAT

def sigint_handler(signal, frame):
    global xively
    xively.shutdown()
    sys.exit(0)

def main():

    p = optparse.OptionParser()
    p.add_option('--uuid', '-u', dest="uuid", default="Energino")
    p.add_option('--interval', '-i', dest="interval", type="int", default=DEFAULT_INTERVAL)
    p.add_option('--port', '-p', dest="port", default=DEFAULT_PORT)
    p.add_option('--bps', '-b', dest="bps", type="int", default=DEFAULT_PORT_SPEED)
    p.add_option('--config', '-c', dest="config", default=DEFAULT_CONFIG)
    p.add_option('--log', '-l', dest="log")
    p.add_option('--rapid', '-r', action="store_true", dest="rapid", default=False)       
    p.add_option('--debug', '-d', action="store_true", dest="debug", default=False)       
    options, _ = p.parse_args()

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

    global xively

    backend = PyEnergino(options.port, options.bps, options.interval)

    xively = XivelyDispatcher(options.uuid, options.config, backend, options.rapid)

    xively.add_stream("power", "derivedSI", "Watts", "W")
    xively.add_stream("voltage", "derivedSI", "Volts", "V")
    xively.add_stream("current", "derivedSI", "Amperes", "A")
    xively.add_stream("switch", "derivedSI", "Switch", "S")
            
    xively.start()   
         
    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)
        
if __name__ == "__main__":
    main()
    