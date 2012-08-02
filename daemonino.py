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
A system daemon interfacing with energino
"""

import socket
import logging
import simplejson as json
import os
import sys
import signal
import optparse
import threading
import time
import httplib
from datetime import datetime, timedelta
from socket import SHUT_RDWR 
from SimpleHTTPServer import SimpleHTTPRequestHandler
from SocketServer import TCPServer
from SocketServer import ThreadingMixIn
from urllib2 import urlopen, URLError, HTTPError
from energino import MODEL_ENERGINO_ETHERNET
from pisolino import DEFAULT_DUTY_CYCLE

DEFAULT_PORT=8181
DEFAULT_WWW_ROOT='/etc/energinod/www/'

STATE_ONLINE="online"
STATE_IDLE="idle"
STATE_OFFLINE="offline"

ONLINE_DUTY_CYCLE = 100
IDLE_DUTY_CYCLE = DEFAULT_DUTY_CYCLE
OFFLINE_DUTY_CYCLE = 0

ONLINE_TIMEOUT=30
IDLE_TIMEOUT=45
TICK=1

ALWAYS_ON=[]

class JSONMergeError(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

class JSONMerge(object):
    
    _json = {}
    
    def __init__(self, json): 
        self._json = json
        if not type(json) is dict and not type(json) is list:
            raise JSONMergeError("Invalid JSON")
        
    def merge(self, json):

        if not type(json) is dict and not type(json) is list:
            raise JSONMergeError("Invalid JSON")

        mergeRes = {}

        for key in json:

            if not key in self._json:
                mergeRes[key] = json[key]
            elif not type(json[key]) == type(self._json[key]):
                mergeRes[key] = json[key]
            else:
                if type(self._json[key]) is dict:
                    out = JSONMerge(self._json[key]).merge(json[key])
                    if out != {} and out != []:
                        mergeRes[key] = out;
                elif not json[key] is None:
                    mergeRes[key] = json[key]

        for key in self._json:
            if not key in json:
                mergeRes[key] = self._json[key]

        return mergeRes
        
class ListenerHandler(SimpleHTTPRequestHandler):
    
    def do_GET(self):
        self.path = self.path.replace('/v2/', '/')
        self.path = self.path.replace('.json', '')
        self.path = self.path.split("?")[0]
        tokens = self.path.strip("/").split("/")
        if len(tokens) >= 1 and hasattr(self.server, tokens[0]):
            try:
                status = getattr(self.server, tokens[0]).get(tokens[1:])
                if status[0] == 200:
                    self.send_response(200)
                    self.send_header('Content-type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps(status[1]) + '\n')
                else:
                    self.send_error(404)
            except Exception:
                logging.exception("Exception:")
                self.send_error(500)
        else:
            try:
                os.chdir(self.server.www_root)
                SimpleHTTPRequestHandler.do_GET(self)
            except Exception:
                logging.exception("Exception:")
                self.send_error(500)

    def do_POST(self):
        self.path = self.path.replace('/v2/', '/')
        self.path = self.path.replace('.json', '')
        self.path = self.path.split("?")[0]
        host = self.headers.getheader('host').split(":")
        tokens = self.path.strip("/").split("/")
        if len(tokens) >= 1 and hasattr(self.server, tokens[0]):
            try:
                if self.headers.getheader('content-length') == None:
                    content_len = 0
                else:
                    content_len = int(self.headers.getheader('content-length'))
                value = json.loads(self.rfile.read(content_len))
                status = getattr(self.server, tokens[0]).post(value)
                if status[0] == 201:
                    self.send_response(201)
                    self.send_header('Location', 'http://%s:%s/v2/feeds/%u' % (host[0], host[1], status[1]))
                    self.end_headers()
                else:
                    self.send_error(status[0])
            except Exception:
                logging.exception("Exception:")
                self.send_error(500)
        else:
            self.send_error(400)

    def do_PUT(self):
        self.path = self.path.replace('/v2/', '/')
        self.path = self.path.replace('.json', '')
        self.path = self.path.split("?")[0]
        tokens = self.path.strip("/").split("/")
        if len(tokens) > 1 and hasattr(self.server, tokens[0]):
            try:
                user_agent = self.headers.getheader('user-agent')
                if self.headers.getheader('content-length') == None:
                    content_len = 0
                else:
                    content_len = int(self.headers.getheader('content-length'))
                value = json.loads(self.rfile.read(content_len))
                status = getattr(self.server, tokens[0]).put(tokens[1:], value, self.client_address[0], user_agent)
                if status[0] == 200:
                    self.send_response(200)
                else:
                    self.send_error(status[0])
            except Exception:
                logging.exception("Exception:")
                self.send_error(500)
        else:
            self.send_error(400)

    def do_DELETE(self):
        self.path = self.path.replace('/v2/', '/')
        self.path = self.path.replace('.json', '')
        self.path = self.path.split("?")[0]
        tokens = self.path.strip("/").split("/")
        if len(tokens) > 1 and hasattr(self.server, tokens[0]):
            try:
                status = getattr(self.server, tokens[0]).delete(int(tokens[1]))
                if status[0] == 200:
                    self.send_response(200)
                else:
                    self.send_error(status[0])
            except Exception:
                logging.exception("Exception:")
                self.send_error(500)
        else:
            self.send_error(400)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - - [%s] %s\n" %
                          (self.client_address[0],
                          self.log_date_time_string(),
                          fmt%args))
        
class Feeds(object):
    
    def __init__(self, host, port):
        self.__host = host
        self.__port = port
        self.__key = 0
        self.__feeds= {}
    
    def get_next_key(self):
        self.__key = self.__key + 1
        return self.__key

    def get(self, feed):
        if len(feed) == 0:
            result = { 'results' : self.__feeds.values(), 'totalResults' : len(self.__feeds), 'startIndex' : 0, 'itemsPerPage' : 100 }
            return (200, result)
        elif int(feed[0]) in self.__feeds:
            results =  JSONMerge({}).merge(self.__feeds[int(feed[0])]) 
            last = datetime.strptime(results['updated'], "%Y-%m-%dT%H:%M:%S.%fZ")
            now = datetime.now()
            delta = timedelta(seconds=30)
            if now - last > delta:
                results['status'] = 'dead'
            else:
                results['status'] = 'live'
            results['datastreams'] = results['datastreams'].values()   
            return (200, results)
        return (404, '')
    
    def post(self, value):
        if not "title" in value or not "version" in value:
            return (401, '')
        feed = self.get_next_key() 
        self.__feeds[feed] = JSONMerge({}).merge(value)
        self.__feeds[feed]['id'] = feed        
        self.__feeds[feed]['created'] = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ")        
        self.__feeds[feed]['updated'] = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ")        
        self.__feeds[feed]['private'] = "false"        
        self.__feeds[feed]['feed'] = 'http://%s:%u/feeds/%u.json' % (self.__host, self.__port, feed)       
        self.__feeds[feed]['datastreams'] = {}        
        return (201, feed)

    def put(self, feed, value, address, agent):

        if int(feed[0]) in self.__feeds:
            
            if len(feed) == 1:
                
                if not ("version" in value or "datastreams" in value):
                    return (401, '')
        
                # update address

                if agent == MODEL_ENERGINO_ETHERNET:
                    self.__feeds[int(feed[0])]['energino'] = address
                else:    
                    self.__feeds[int(feed[0])]['dispatcher'] = address

                # update datastreams
                for incoming in value['datastreams']:
                    if incoming['id'] in self.__feeds[int(feed[0])]['datastreams']:
                        local = self.__feeds[int(feed[0])]['datastreams'][incoming['id']]
                        local['at'] = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ")
                        local['current_value'] = incoming['current_value']
                        if local['max_value'] < local['current_value']:
                            local['max_value'] = local['current_value']
                        if local['min_value'] > local['current_value']:
                            local['min_value'] = local['current_value']
                    else:
                        self.__feeds[int(feed[0])]['datastreams'][incoming['id']] = { 'at' : datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ"), 
                                                                              'max_value' : incoming['current_value'],
                                                                              'min_value' : incoming['current_value'],
                                                                              'id' : incoming['id'],'current_value' : incoming['current_value'] }
                # delete datastreams from incoming document
                del value['datastreams']
                # merge everything else
                self.__feeds[int(feed[0])] = JSONMerge(self.__feeds[int(feed[0])]).merge(value)
                # update here and there
                self.__feeds[int(feed[0])]['updated'] = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ")                  
                #return sucess
                return (200, 'OK')
            
            else:
                
                try:
                    resolved = self.__feeds[int(feed[0])]['energino']
                    conn = httplib.HTTPConnection(host=resolved, port=8180, timeout=10)
                    conn.request('GET', "/write/%s/%s" % (feed[2], value['current_value']))
                    res = conn.getresponse()
                except HTTPError, e:
                    logging.error("energino could not execute the command %s, error code %u" % (feed[1], e.code))
                    return (500, '')
                except URLError, e:
                    logging.error("url not available at %s, reason %s" % (feed[1], e.reason))
                    return (500, '')
                else:
                    return (200, json.loads(res.read()))
        
        return (404, '')

    def delete(self, feed):
        if feed in self.__feeds:
            del self.__feeds[feed]
            return (200, '')
        return (404, '')

class Listener(ThreadingMixIn, TCPServer):
    
    def __init__(self, host, feeds, port = DEFAULT_PORT, www_root = DEFAULT_WWW_ROOT):
        self.allow_reuse_address = True
        self.www_root = www_root
        self.feeds = feeds
        logging.info("hostname: %s" % host)
        logging.info("listening on port: %u" % port)
        logging.info("serving from: %s" % self.www_root)
        TCPServer.__init__(self, ("", port), ListenerHandler)

class Daemonino(threading.Thread):
    
    def __init__(self, feeds, autonomic):
        super(Daemonino, self).__init__()
        self.feeds = feeds
        self.autonomic = autonomic
        self.daemon = True
        self.stop = threading.Event()
        self.state = {}
  
    def get_tot_num_clients(self):
        tot_clients = 0
        feeds = self.feeds.get('')
        if feeds[0] != 200:
            return tot_clients
        if not 'results' in feeds[1]:
            return tot_clients
        for result in feeds[1]['results']:
            if "clients" in result['datastreams']:
                clients = result['datastreams']['clients']['current_value']
                tot_clients = tot_clients + clients
        return tot_clients
                              
    def run(self):
        logging.info("starting up daemonino")
        while True:
            if not self.autonomic:
                time.sleep(TICK)
                continue
            tot_clients = self.get_tot_num_clients()
            if tot_clients >= 4:
                ALWAYS_ON = [ 1, 2, 3 ]
            elif tot_clients >= 2:
                ALWAYS_ON = [ 2, 3 ]
            else:
                ALWAYS_ON = [ 3 ]
            feeds = self.feeds.get('')
            if feeds[0] != 200:
                continue 
            if not 'results' in feeds[1]:
                continue
            for result in feeds[1]['results']:
                id_feed = result['id']
                if not id_feed in self.state:
                    self.state[id_feed ] = { 'state' :  STATE_ONLINE, 'counter' : 0 }
                if "clients" in result['datastreams']:
                    clients = result['datastreams']['clients']['current_value']
                    tot_clients = tot_clients + clients
                    if clients == 0:
                        if self.state[id_feed]['state'] != STATE_OFFLINE:
                            self.state[id_feed]['counter'] = self.state[id_feed]['counter'] + 1
                        if self.state[id_feed]['state'] == STATE_ONLINE and self.state[id_feed]['counter'] > ONLINE_TIMEOUT:
                            logging.debug("no clients for node %u setting state to %s" % (id_feed, STATE_IDLE))
                            self.state[id_feed]['state'] = STATE_IDLE
                            self.state[id_feed]['counter'] = 0
                        if self.state[id_feed]['state'] == STATE_IDLE and self.state[id_feed]['counter'] > IDLE_TIMEOUT:
                            logging.debug("no clients for node %u setting state to %s" % (id_feed, STATE_OFFLINE))
                            self.state[id_feed]['state'] = STATE_OFFLINE
                            self.state[id_feed]['counter'] = 0
                    else:
                        self.state[id_feed]['state'] = STATE_ONLINE
                        self.state[id_feed]['counter'] = 0
                if id_feed in ALWAYS_ON:
                    self.state[id_feed]['state'] = STATE_ONLINE
                    self.state[id_feed]['counter'] = 0
                self.set_switch(result, self.state[id_feed]['state'])
                self.set_idle(result, self.state[id_feed]['state'])
            time.sleep(TICK)

    def set_switch(self, result, state):
        try:
            if 'switch' in result['datastreams']:
                switch = result['datastreams']['switch']['current_value']
                url = None
                if (state in [ STATE_ONLINE, STATE_IDLE ]) and (switch == '1'):
                    url = 'http://' + result['energino'] + ':8180/write/switch/0'
                if (state in [ STATE_OFFLINE ]) and (switch == '0'):
                    url = 'http://' + result['energino'] + ':8180/write/switch/1'
                if url != None:
                    logging.info("opening url %s" % url)
                    res = urlopen(url, timeout=2)
                    status = json.loads(res.read())
                    result['datastreams']['switch']['current_value'] = status[0]
        except HTTPError, e:
            logging.error("energino could not execute the command %s, error code %u" % (url, e.code))
        except URLError, e:
            logging.error("url not available %s, reason %s" % (url, e.reason))
        except:
            logging.exception("Exception:")        

    def set_idle(self, result, state):
        try:
            if 'duty_cycle' in result['datastreams']:
                url = None
                if state in [ STATE_OFFLINE ]:
                    result['datastreams']['duty_cycle']['current_value'] = OFFLINE_DUTY_CYCLE
                else:
                    if state == STATE_ONLINE:                    
                        newDutyCycle = ONLINE_DUTY_CYCLE
                    else:
                        newDutyCycle = IDLE_DUTY_CYCLE
                    if result['datastreams']['duty_cycle']['current_value'] != newDutyCycle:
                        logging.info("feed %u, setting duty cycle to %u" % (result['id'], newDutyCycle) )
                        conn = httplib.HTTPConnection(host=result['dispatcher'], port=8180, timeout=10)
                        conn.request('PUT', "/ap/duty_cycle/%u" % newDutyCycle)
                        resp = conn.getresponse()
                        if resp.status == 200:
                            data = json.loads(resp.read())
                            result['datastreams']['duty_cycle']['current_value'] = data[0]
        except HTTPError, e:
            logging.error("energino could not execute the command %s, error code %u" % (url, e.code))
        except URLError, e:
            logging.error("url not available %s, reason %s" % (url, e.reason))
        except:
            logging.exception("Exception:")        

    def shutdown(self):
        self.stop.set()
                    
def sigint_handler(signal, frame):
    listener.socket.shutdown(SHUT_RDWR)
    listener.socket.close()
    daemonino.shutdown()
    sys.exit(0)

if __name__ == "__main__":

    version_info = sys.version_info
    version_needed = (2, 6)
    
    if version_info < version_needed:
        sys.stderr.write("Error: wrong python interpreter (need %d.%d or later)\n" % version_needed)
        sys.exit(1)

    p = optparse.OptionParser()
    p.add_option('--port', '-p', dest="port", default=DEFAULT_PORT)
    p.add_option('--www', '-w', dest="www", default=DEFAULT_WWW_ROOT)
    p.add_option('--log', '-l', dest="log")
    p.add_option('--autonomic', '-a', action="store_true", dest="autonomic", default=False)    
    options, arguments = p.parse_args()

    logging.basicConfig(level=logging.DEBUG, format='%(asctime)-15s %(message)s', filename=options.log, filemode='w')

    host = socket.gethostbyname(socket.gethostname())
    feeds = Feeds(host, int(options.port))
    
    daemonino = Daemonino(feeds, options.autonomic)
    daemonino.start()
    
    listener = Listener(host, feeds, int(options.port), options.www)

    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGTERM, sigint_handler)
        
    listener.serve_forever()
