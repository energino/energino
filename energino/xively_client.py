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
import os.path
import time
import httplib
import threading
import ConfigParser
import json

from collections import deque

from energino import PyEnergino
from energino import DEFAULT_INTERVAL
from energino import DEFAULT_DEVICE
from energino import DEFAULT_DEVICE_SPEED_BPS

DEFAULT_CONFIG = '/etc/xively.conf'

DEFAULT_HOST = 'api.xively.com'
DEFAULT_PORT = "80"
DEFAULT_PERIOD = "10"

LOG_FORMAT = '%(asctime)-15s %(message)s'

BACKOFF = 60

class DispatcherProcedure(threading.Thread):
    """ DispatcherProcedure class. Handles communication with Xively. """

    def __init__(self, dispatcher):
        super(DispatcherProcedure, self).__init__()
        self.daemon = True
        self.dispatcher = dispatcher
        self.stop = threading.Event()
        self.outgoing = deque()
        self.lock = threading.Lock()
        self.streams = {}

    def shutdown(self):
        """ Shutdown dispatcher. """

        logging.info("shutting down dispatcher")
        self.stop.set()

    def run(self):
        logging.info("starting up dispatcher")
        logging.info("dispatching every %us", self.dispatcher.config['period'])
        while True:
            if self.stop.isSet():
                break
            self.process()
            if not self.dispatcher.config['period']:
                break
            time.sleep(self.dispatcher.config['period'])

    def add_stream(self, stream, si_type, label, symbol):
        """ Add a new datastream. """

        self.streams[stream] = {"id" : stream,
                                "datapoints" : [],
                                "unit": {"type": si_type,
                                         "label": label,
                                         "symbol": symbol}}

    def process(self):
        """ Update feed. """

        if len(self.outgoing) == 0:
            return

        with self.lock:
            feed = self.dispatcher.get_feed()
            feed['datastreams'] = []
            for stream in self.streams.values():
                stream['datapoints'] = []
            pending = deque()
            while self.outgoing:
                readings = self.outgoing.popleft()
                pending.append(readings)
                for stream in self.streams.values():
                    stream['current_value'] = readings[stream['id']]
                    sample = {"at" : readings['at'],
                              "value" :  "%.3f" % readings[stream['id']]}
                    stream['datapoints'].append(sample)
            for stream in self.streams.values():
                feed['datastreams'].append(stream)

        feed_id = self.dispatcher.config['feed']
        logging.info("updating feed %s, sending %s samples", feed_id,
                                                             len(pending))
        try:

            conn = httplib.HTTPConnection(host=self.dispatcher.config['host'],
                                          port=self.dispatcher.config['port'],
                                          timeout=10)

            conn.request('PUT', "/v2/feeds/%s" % feed_id,
                         json.dumps(feed),
                         {'X-ApiKey' : self.dispatcher.config['key']})

            resp = conn.getresponse()
            conn.close()

            if resp.status != 200:

                logging.error("%s (%s), rolling back %u updates", resp.reason,
                                                                  resp.status,
                                                                  len(pending))
                self.dispatcher.discover()
                while pending:
                    self.outgoing.appendleft(pending.pop())

        except httplib.HTTPError as ex:

            logging.exception(ex)
            logging.error("exception, rolling back %u updates", len(pending))

            while pending:
                self.outgoing.appendleft(pending.pop())

    def enqueue(self, readings):
        """ Enque readings to outgoing queue. """

        with self.lock:
            self.outgoing.append(readings)

class XivelyDispatcher(threading.Thread):
    """ Xively Client. """

    def __init__(self, uuid, config_file, backend):
        super(XivelyDispatcher, self).__init__()
        logging.info("uuid %s", uuid)
        self.daemon = True
        self.stop = threading.Event()
        self.config_file = config_file
        self.config = {'uuid' : uuid, 'backend' : backend}
        self.load_config()
        self.dispatcher = DispatcherProcedure(self)
        self.streams = {}

    def add_stream(self, stream, unit_type, label, symbol):
        """ Add a new stream to the Xively client. """

        self.streams[stream] = {'unit_type' : unit_type,
                                'label' : label,
                                'symbol' : symbol}

    def start(self):

        # start dispatcher

        for stream in self.streams:
            self.dispatcher.add_stream(stream,
                                       self.streams[stream]['unit_type'],
                                       self.streams[stream]['label'],
                                       self.streams[stream]['symbol'])
        self.dispatcher.start()

        # start pool loop
        while True:
            try:
                # configure feed
                self.discover()
                # start updating
                logging.info("begin polling")
                while True:
                    if self.stop.isSet():
                        return
                    try:
                        readings = self.config['backend'].fetch()
                        self.dispatcher.enqueue(readings)
                    except ValueError:
                        logging.warning("sample lost")
            except RuntimeError as ex:
                logging.exception(ex)
                time.sleep(BACKOFF)

        # thread stopped
        logging.info("thread %s stopped", self.__class__.__name__)


    def shutdown(self):
        """ Shutdown Xively client. """
        logging.info("shutting down dispatcher")
        self.dispatcher.shutdown()
        self.stop.set()

    def get_feed(self):
        """ return feed as dictionary. """

        return {
          "version" : "1.0.0",
          "title" : self.config['uuid'],
          "website" : self.config['website'],
          "tags" : self.config['tags'],
          "location" : {
            "disposition" : self.config['disposition'],
            "lat" : self.config['lat'],
            "exposure" : self.config['exposure'],
            "lon" : self.config['lon'],
            "domain" : self.config['domain'],
            "name" : self.config['name']
          }
        }

    def load_config(self):
        """ Load configuration from file. """

        config = ConfigParser.SafeConfigParser({'host' : DEFAULT_HOST,
                                                'port' : DEFAULT_PORT,
                                                'feed' : '',
                                                'key' : '-',
                                                'period' : DEFAULT_PERIOD,
                                                'website' : '',
                                                'disposition' : 'fixed',
                                                'name':'',
                                                'lat':"0.0",
                                                'exposure':'indoor',
                                                'lon':"0.0",
                                                'domain':'physical',
                                                'tags':''})

        config.read(os.path.expanduser(self.config_file))

        if not config.has_section("General"):
            config.add_section("General")

        self.config['key'] = config.get("General", "key")
        self.config['host'] = config.get("General", "host")
        self.config['port'] = config.getint("General", "port")
        self.config['feed'] = config.get("General", "feed")
        self.config['period'] = config.getint("General", "period")

        if not config.has_section("Location"):
            config.add_section("Location")

        self.config['website'] = config.get("Location", "website")
        self.config['disposition'] = config.get("Location", "disposition")
        self.config['name'] = config.get("Location", "name")
        self.config['lat'] = config.getfloat("Location", "lat")
        self.config['exposure'] = config.get("Location", "exposure")
        self.config['lon'] = config.getfloat("Location", "lon")
        self.config['domain'] = config.get("Location", "domain")
        self.config['tags'] = config.get("Location", "tags").split(",")

        logging.info("loading configuration...")

        logging.info("key: %s", self.config['key'])
        logging.info("host: %s", self.config['host'])
        logging.info("port: %s", self.config['port'])

        if self.config['feed']:
            logging.info("feed: %s", self.config['feed'])

        logging.info("period: %s", self.config['period'])
        logging.info("website: %s", self.config['website'])

        logging.info("disposition: %s", self.config['disposition'])
        logging.info("name: %s", self.config['name'])
        logging.info("lat: %s", self.config['lat'])
        logging.info("exposure: %s", self.config['exposure'])
        logging.info("lon: %s", self.config['lon'])
        logging.info("domain: %s", self.config['domain'])
        logging.info("tags: %s", self.config['tags'])

        if not self.config['key']:
            raise Exception("invalid key")

    def discover(self):
        """ Check if feed is available, otherwise exit. """

        # check if feed is available
        if self.config['feed']:

            logging.info("trying to fetch http://%s:%u/v2/feeds/%s",
                         self.config['host'],
                         self.config['port'],
                         self.config['feed'])

            conn = httplib.HTTPConnection(host=self.config['host'],
                                          port=self.config['port'],
                                          timeout=10)

            conn.request('GET', "/v2/feeds/%s" %
                         self.config['feed'],
                         headers={'X-ApiKey' : self.config['key']})

            resp = conn.getresponse()
            conn.close()

            if resp.status == 200:
                # feed found
                logging.info("feed %s found!", self.config['feed'])
                return
            else:
                # feed not found
                raise Exception("error while fetching feed %s, %s (%s)",
                                self.config['feed'],
                                resp.reason,
                                resp.status)

        raise ValueError("feed id is not specified")

    def save_state(self):
        """ update configuration file. """

        config = ConfigParser.SafeConfigParser()

        config.add_section("General")
        config.set("General", "key", self.config['key'])
        config.set("General", "host", self.config['host'])
        config.set("General", "port", str(self.config['port']))
        config.set("General", "period", str(self.config['period']))

        if not self.config['feed']:
            config.set("General", "feed", self.config['feed'])

        config.add_section("Location")
        config.set("Location", "website", self.config['website'])
        config.set("Location", "disposition", self.config['disposition'])
        config.set("Location", "name", self.config['name'])
        config.set("Location", "lat", str(self.config['lat']))
        config.set("Location", "exposure", self.config['exposure'])
        config.set("Location", "lon", str(self.config['lon']))
        config.set("Location", "domain", self.config['domain'])
        config.set("Location", "tags", ",".join(self.config['tags']))

        config.write(open(self.config, "w"))

def sigint_handler(*_):
    """ Handle SIGINT. """

    sys.exit(0)

def main():
    """ Launch XivelyClient. """

    parser = optparse.OptionParser()

    parser.add_option('--uuid', '-u',
                      dest="uuid",
                      default="Energino")

    parser.add_option('--interval', '-i',
                      dest="interval",
                      type="int",
                      default=DEFAULT_INTERVAL)

    parser.add_option('--port', '-p',
                       dest="device",
                     default=DEFAULT_DEVICE)

    parser.add_option('--bps', '-b',
                      dest="device_speed_bps",
                      type="int",
                      default=DEFAULT_DEVICE_SPEED_BPS)

    parser.add_option('--config', '-c',
                      dest="config",
                      default=DEFAULT_CONFIG)

    parser.add_option('--log', '-l', dest="log")

    parser.add_option('--rapid', '-r',
                      action="store_true",
                      dest="rapid",
                      default=False)

    parser.add_option('--debug', '-d',
                      action="store_true",
                      dest="debug",
                      default=False)

    options, _ = parser.parse_args()

    if options.debug:
        lvl = logging.DEBUG
    else:
        lvl = logging.INFO

    if options.log != None:
        logging.basicConfig(level=lvl,
                            format=LOG_FORMAT,
                            filename=options.log,
                            filemode='w')
    else:
        logging.basicConfig(level=lvl, format=LOG_FORMAT)

    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)

    backend = PyEnergino(options.device,
                         options.device_speed_bps,
                         options.interval)

    xively = XivelyDispatcher(options.uuid, options.config, backend)

    xively.add_stream("power", "derivedSI", "Watts", "W")
    xively.add_stream("voltage", "derivedSI", "Volts", "V")
    xively.add_stream("current", "derivedSI", "Amperes", "A")
    xively.add_stream("switch", "derivedSI", "Switch", "S")

    xively.start()

    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)

if __name__ == "__main__":
    main()
