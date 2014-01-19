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

import time
import logging
import httplib
import threading
import ConfigParser
import json

from collections import deque

DEFAULT_CONFIG = '/etc/xively.conf'

DEFAULT_HOST = 'api.xively.com'
DEFAULT_PORT = 80
DEFAULT_PERIOD = 10

LOG_FORMAT = '%(asctime)-15s %(message)s'

BACKOFF = 60

class DispatcherProcedure(threading.Thread):

    def __init__(self, dispatcher):
        super(DispatcherProcedure, self).__init__()
        self.daemon = True
        self.dispatcher = dispatcher
        self.stop = threading.Event()
        self.outgoing = deque()
        self.lock = threading.Lock()
        self.streams = {}

    def shutdown(self):
        logging.info("shutting down dispatcher")
        self.stop.set()

    def run(self):
        logging.info("starting up dispatcher")
        logging.info("dispatching every %us" % self.dispatcher.period)
        while True:
            if self.stop.isSet():
                break;
            self.process()
            if self.dispatcher.period == 0:
                break
            time.sleep(self.dispatcher.period)

    def add_stream(self, stream, si, label, symbol):
        self.streams[stream] = { "id" : stream, "datapoints" : [], "unit": { "type": si, "label": label, "symbol": symbol } }

    def process(self):
        if len(self.outgoing) == 0:
            return
        with self.lock:
            feed = self.dispatcher.getJsonFeed()
            feed['datastreams'] = []
            for stream in self.streams.values():
                stream['datapoints'] = []
            pending = deque()
            while self.outgoing:
                readings = self.outgoing.popleft()
                pending.append(readings)
                for stream in self.streams.values():
                    stream['current_value'] = readings[stream['id']]
                    stream['datapoints'].append({ "at" :  readings['at'], "value" :  "%.3f" % readings[stream['id']] })
            for stream in self.streams.values():
                feed['datastreams'].append(stream)
        logging.info("updating feed %s, sending %s samples" % (self.dispatcher.feed, len(pending)) )
        try:
            conn = httplib.HTTPConnection(host=self.dispatcher.host, port=self.dispatcher.port, timeout=10)
            conn.request('PUT', "/v2/feeds/%s" % self.dispatcher.feed, json.dumps(feed), { 'X-ApiKey' : self.dispatcher.key })
            resp = conn.getresponse()
            conn.close()
            if resp.status != 200:
                logging.error("%s (%s), rolling back %u updates" % (resp.reason, resp.status, len(pending)))
                self.dispatcher.discover()
                while pending:
                    self.outgoing.appendleft(pending.pop())
        except Exception, e:
            logging.error("exception %s, rolling back %u updates" % (str(e), len(pending)))
            while pending:
                self.outgoing.appendleft(pending.pop())

    def enqueue(self, readings, send = False):
        with self.lock:
            self.outgoing.append(readings)
        if send:
            self.process()

class XivelyDispatcher(threading.Thread):

    def __init__(self, uuid, config, backend, rapid = False):
        super(XivelyDispatcher, self).__init__()
        logging.info("uuid %s" % uuid)
        self.uuid = uuid
        self.backend = backend
        self.rapid = rapid
        self.daemon = True
        self.stop = threading.Event()
        self.config = config
        self.loadConfig()
        self.dispatcher = DispatcherProcedure(self)
        self.streams = {}

    def add_stream(self, stream, si, label, symbol):
        self.streams[stream] = { 'si' : si, 'label' : label, 'symbol' : symbol }

    def start(self):

        # start dispatcher

        for stream in self.streams:
            self.dispatcher.add_stream(stream, self.streams[stream]['si'], self.streams[stream]['label'], self.streams[stream]['symbol'])
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
                        readings = self.backend.fetch()
                        self.dispatcher.enqueue(readings, self.rapid)
                    except:
                        logging.warning("sample lost")
            except Exception:
                logging.exception("exception, backing off for %u seconds" % BACKOFF)
                time.sleep(BACKOFF)
        # thread stopped
        logging.info("thread %s stopped" % self.__class__.__name__)


    def shutdown(self):
        logging.info("shutting down dispatcher")
        self.dispatcher.shutdown()
        self.stop.set()

    def getJsonFeed(self):
        return {
          "version" : "1.0.0",
          "title" : self.uuid,
          "website" : self.website,
          "tags" : self.tags,
          "location" : {
            "disposition" : self.disposition,
            "name" : self.name,
            "lat" : self.lat,
            "exposure" : self.exposure,
            "lon" : self.lon,
            "domain" : self.domain,
            "name" : self.name
          }
        }

    def loadConfig(self):

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

        config.read(self.config)

        if not config.has_section("General"):
            config.add_section("General")

        self.key = config.get("General", "key")
        self.host = config.get("General", "host")
        self.port = config.getint("General", "port")
        self.feed = config.get("General", "feed")
        self.period = config.getint("General", "period")

        if not config.has_section("Location"):
            config.add_section("Location")

        self.website = config.get("Location", "website")
        self.disposition = config.get("Location", "disposition")
        self.name = config.get("Location", "name")
        self.lat = config.getfloat("Location", "lat")
        self.exposure = config.get("Location", "exposure")
        self.lon = config.getfloat("Location", "lon")
        self.domain = config.get("Location", "domain")
        self.tags = config.get("Location", "tags").split(",")

        logging.info("loading configuration...")

        logging.info("key: %s" % self.key)
        logging.info("host: %s" % self.host)
        logging.info("port: %s" % self.port)

        if self.feed != '':
            logging.info("feed: %s" % self.feed)

        logging.info("period: %s" % self.period)
        logging.info("website: %s" % self.website)

        logging.info("disposition: %s" % self.disposition)
        logging.info("name: %s" % self.name)
        logging.info("lat: %s" % self.lat)
        logging.info("exposure: %s" % self.exposure)
        logging.info("lon: %s" % self.lon)
        logging.info("domain: %s" % self.domain)
        logging.info("tags: %s" % self.tags)

        if self.key is None:
            raise Exception("invalid key")

        self.saveState()

    def discover(self):

        # check if feed is available
        if self.feed != None and self.feed != '':
            logging.info("trying to fetch http://%s:%u/v2/feeds/%s" % (self.host, self.port, self.feed))
            conn = httplib.HTTPConnection(host=self.host, port=self.port, timeout=10)
            conn.request('GET', "/v2/feeds/%s" % self.feed, headers={'X-ApiKey': self.key})
            resp = conn.getresponse()
            conn.close()
            if resp.status == 200:
                # feed found
                logging.info("feed %s found!" % self.feed)
                return
            else:
                # feed not found
                raise Exception("error while fetching feed %s, %s (%s)" % (self.feed, resp.reason, resp.status))

        # create new feed
        logging.info("creating new feed")
        conn = httplib.HTTPConnection(host=self.host, port=self.port, timeout=10)
        conn.request('POST', '/v2/feeds/', json.dumps(self.getJsonFeed()), {'X-ApiKey': self.key})
        resp = conn.getresponse()
        conn.close()

        if resp.status == 201:
            self.feed = resp.getheader('location').split("/")[-1]
            logging.info("feed %s created!" % self.feed)
        else:
            self.feed = None
            raise Exception("error while creating feed, %s (%s)" % (resp.reason, resp.status))

        self.saveState()

    def saveState(self):

        # update configuration file
        config = ConfigParser.SafeConfigParser()

        config.add_section("General")
        config.set("General", "key", self.key)
        config.set("General", "host", self.host)
        config.set("General", "port", str(self.port))
        config.set("General", "period", str(self.period))

        if not self.feed is None:
            config.set("General", "feed", self.feed)

        config.add_section("Location")
        config.set("Location", "website", self.website)
        config.set("Location", "disposition", self.disposition)
        config.set("Location", "name", self.name)
        config.set("Location", "lat", str(self.lat))
        config.set("Location", "exposure", self.exposure)
        config.set("Location", "lon", str(self.lon))
        config.set("Location", "domain", self.domain)
        config.set("Location", "tags", ",".join(self.tags))

        config.write(open(self.config,"w"))
