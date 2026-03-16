# Cherrypy Web server for WebGL data display
# Copyright (c) Jeremy P Bentham 2021. See http://iosoft.blog for details
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# v0.01 JPB 11/1/21  First release

import os, os.path, random, string, math, socket, threading, time, cherrypy

portnum   = 8080
fifo_name = "/tmp/adc.fifo"
adc_stream_host = os.environ.get("ADC_STREAM_HOST", "127.0.0.1")
adc_stream_port = int(os.environ.get("ADC_STREAM_PORT", "9000"))
adc_stream_timeout = float(os.environ.get("ADC_STREAM_TIMEOUT", "0.25"))
ymax = 2.0
npoints = 10000
nchans = 2
nresults = 0
directory = os.getcwd()


class AdcSocketClient(object):
    """Line-based TCP reader with auto-reconnect for ADC samples."""

    def __init__(self, host, port, timeout):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.reader = None
        self.lock = threading.Lock()
        self.last_error = ""
        self.last_connect_ts = 0.0
        self.latest_line = ""
        self.lines_read = 0
        self.reconnect_delay = 0.5
        self.stop_evt = threading.Event()
        self.worker = None

    def _close(self):
        if self.reader:
            try:
                self.reader.close()
            except Exception:
                pass
            self.reader = None
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def start(self):
        if self.worker and self.worker.is_alive():
            return
        self.stop_evt.clear()
        self.worker = threading.Thread(target=self._reader_loop, name="adc-socket-reader", daemon=True)
        self.worker.start()

    def stop(self):
        self.stop_evt.set()
        self._close()

    def _connect(self):
        self._close()
        cherrypy.log("ADC socket: connecting to %s:%d" % (self.host, self.port))
        self.sock = socket.create_connection((self.host, self.port), self.timeout)
        self.sock.settimeout(self.timeout)
        self.reader = self.sock.makefile("r", encoding="ascii", newline="\n")
        self.last_connect_ts = time.time()
        self.last_error = ""
        cherrypy.log("ADC socket: connected")

    def _reader_loop(self):
        while not self.stop_evt.is_set():
            try:
                if self.reader is None:
                    self._connect()

                line = self.reader.readline()
                if not line:
                    self.last_error = "stream closed by peer"
                    cherrypy.log("ADC socket: stream closed by peer")
                    self._close()
                    self.stop_evt.wait(self.reconnect_delay)
                    continue

                line = line.strip()
                with self.lock:
                    self.latest_line = line
                    self.lines_read += 1

            except (OSError, ValueError) as ex:
                self.last_error = str(ex)
                cherrypy.log("ADC socket: read/connect failed: %s" % self.last_error)
                self._close()
                self.stop_evt.wait(self.reconnect_delay)

    def read_line(self):
        with self.lock:
            return self.latest_line

    def status(self):
        with self.lock:
            return {
                "host": self.host,
                "port": self.port,
                "connected": self.reader is not None,
                "last_error": self.last_error,
                "last_connect_ts": self.last_connect_ts,
                "lines_read": self.lines_read,
            }


adc_socket_client = AdcSocketClient(adc_stream_host, adc_stream_port, adc_stream_timeout)

# Oscilloscope-type ADC data display
class Grapher(object):

    # Index: show oscilloscope display
    @cherrypy.expose
    def index(self):
        return cherrypy.lib.static.serve_file(directory + "/webgl_graph.html")

    # Simulated data source
    @cherrypy.expose
    def sim(self):
        global nresults
        cherrypy.response.headers['Content-Type'] = 'text/plain'
        data = npoints * [0]
        for c in range(0, npoints, nchans):
            data[c] = (math.sin((nresults*2 + c) / 20.0) + 1.2) * ymax / 4.0
            if nchans > 1:
                data[c+1] = (math.cos((nresults*2 + c) / 200.0) + 0.8) * data[c]
                data[c+1] += random.random() / 4.0
        nresults += 1
        rsp = ",".join([("%1.3f" % d) for d in data])
        return rsp

    # FIFO data source
    @cherrypy.expose
    def fifo(self):
        cherrypy.response.headers['Content-Type'] = 'text/plain'
        rsp = adc_socket_client.read_line()
        if rsp is None:
            rsp = ""
        return rsp

    # Socket data source (same output format as /fifo)
    @cherrypy.expose
    def socket(self):
        cherrypy.response.headers['Content-Type'] = 'text/plain'
        rsp = adc_socket_client.read_line()
        if rsp is None:
            rsp = ""
        return rsp

    # ADC socket source (alias used by clients)
    @cherrypy.expose
    def adc(self):
        cherrypy.response.headers['Content-Type'] = 'text/plain'
        rsp = adc_socket_client.read_line()
        if rsp is None:
            rsp = ""
        return rsp

    @cherrypy.expose
    def health(self):
        st = adc_socket_client.status()
        cherrypy.response.headers['Content-Type'] = 'text/plain'
        return (
            "adc_host={host}\n"
            "adc_port={port}\n"
            "adc_connected={connected}\n"
            "adc_last_error={last_error}\n"
            "adc_last_connect_ts={last_connect_ts}\n"
            "adc_lines_read={lines_read}\n"
        ).format(**st)

if __name__ == '__main__':
    print("ADC server configured stream target %s:%d timeout=%.3fs" %
          (adc_stream_host, adc_stream_port, adc_stream_timeout))
    adc_socket_client.start()
    cherrypy.engine.subscribe('stop', adc_socket_client.stop)
    cherrypy.config.update({"server.socket_port": portnum, "server.socket_host": "0.0.0.0"})
    conf = {
        '/': {
            'tools.staticdir.root': os.path.abspath(directory)
        }
    }
    cherrypy.quickstart(Grapher(), '/', conf)
